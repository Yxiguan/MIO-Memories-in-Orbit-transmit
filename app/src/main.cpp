//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#include "stdafx.h"
#include <TlHelp32.h>

static constexpr WORD kEmbeddedDllId = 101;
static const wchar_t* kWindowClassName = L"MioInjectorWindow";

static DWORD FindProcessId(const wchar_t* processName)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;

    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);

    if (!Process32FirstW(snapshot, &entry))
    {
        CloseHandle(snapshot);
        return 0;
    }

    DWORD pid = 0;
    do
    {
        if (_wcsicmp(entry.szExeFile, processName) == 0)
        {
            pid = entry.th32ProcessID;
            break;
        }
    } while (Process32NextW(snapshot, &entry));

    CloseHandle(snapshot);
    return pid;
}

static bool ExtractEmbeddedDll(std::wstring& outPath)
{
    HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(kEmbeddedDllId), RT_RCDATA);
    if (!resource)
        return false;

    HGLOBAL resourceData = LoadResource(nullptr, resource);
    if (!resourceData)
        return false;

    DWORD resourceSize = SizeofResource(nullptr, resource);
    void* resourcePtr = LockResource(resourceData);
    if (!resourcePtr || resourceSize == 0)
        return false;

    wchar_t tempPath[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, tempPath) == 0)
        return false;

    outPath = std::wstring(tempPath) + L"mio_inject.dll";
    DeleteFileW(outPath.c_str());

    HANDLE file = CreateFileW(outPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    DWORD written = 0;
    BOOL writeOk = WriteFile(file, resourcePtr, resourceSize, &written, nullptr);
    CloseHandle(file);

    return writeOk && written == resourceSize;
}

static void CleanupTempDllOnExit(const std::wstring& path, HANDLE process)
{
    if (path.empty())
        return;

    if (process)
    {
        WaitForSingleObject(process, INFINITE);
        CloseHandle(process);
    }

    if (!DeleteFileW(path.c_str()))
        MoveFileExW(path.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
}

static bool InjectDll(DWORD pid, const std::wstring& dllPath, HANDLE& outProcess)
{
    if (pid == 0 || dllPath.empty())
        return false;

    outProcess = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ | SYNCHRONIZE, FALSE, pid);
    if (!outProcess)
        return false;

    size_t bytes = (dllPath.size() + 1) * sizeof(wchar_t);
    void* remoteMemory = VirtualAllocEx(outProcess, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteMemory)
    {
        CloseHandle(outProcess);
        outProcess = nullptr;
        return false;
    }

    if (!WriteProcessMemory(outProcess, remoteMemory, dllPath.c_str(), bytes, nullptr))
    {
        VirtualFreeEx(outProcess, remoteMemory, 0, MEM_RELEASE);
        CloseHandle(outProcess);
        outProcess = nullptr;
        return false;
    }

    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (!kernel32)
    {
        VirtualFreeEx(outProcess, remoteMemory, 0, MEM_RELEASE);
        CloseHandle(outProcess);
        outProcess = nullptr;
        return false;
    }

    FARPROC loadLibrary = GetProcAddress(kernel32, "LoadLibraryW");
    if (!loadLibrary)
    {
        VirtualFreeEx(outProcess, remoteMemory, 0, MEM_RELEASE);
        CloseHandle(outProcess);
        outProcess = nullptr;
        return false;
    }

    HANDLE thread = CreateRemoteThread(outProcess, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(loadLibrary), remoteMemory, 0, nullptr);
    if (!thread)
    {
        VirtualFreeEx(outProcess, remoteMemory, 0, MEM_RELEASE);
        CloseHandle(outProcess);
        outProcess = nullptr;
        return false;
    }

    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    VirtualFreeEx(outProcess, remoteMemory, 0, MEM_RELEASE);
    return true;
}

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

static HWND CreateStatusWindow(HINSTANCE instance)
{
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kWindowClassName;

    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        0,
        kWindowClassName,
        L"MIO 注入器",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        420,
        160,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (hwnd)
    {
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
    }

    return hwnd;
}

_Use_decl_annotations_
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    HWND hwnd = CreateStatusWindow(hInstance);
    if (!hwnd)
    {
        MessageBoxW(nullptr, L"无法创建窗口", L"注入失败", MB_ICONERROR | MB_OK);
        return 1;
    }

    SetWindowTextW(hwnd, L"MIO 注入器 - 等待启动 mio.exe...");

    bool injected = false;
    ULONGLONG lastCheck = 0;
    DWORD targetPid = 0;
    HANDLE targetProcess = nullptr;
    MSG msg = {};

    while (msg.message != WM_QUIT)
    {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        ULONGLONG now = GetTickCount64();
        if (now - lastCheck >= 1000)
        {
            lastCheck = now;
            if (!injected)
            {
                targetPid = FindProcessId(L"mio.exe");
            }

            if (targetPid != 0 && !injected)
            {
                SetWindowTextW(hwnd, L"MIO 注入器 - 已检测到 mio.exe，准备注入...");
                Sleep(3000);

                std::wstring tempDllPath;
                if (!ExtractEmbeddedDll(tempDllPath))
                {
                    MessageBoxW(hwnd, L"释放临时 DLL 失败", L"注入失败", MB_ICONERROR | MB_OK);
                    DestroyWindow(hwnd);
                    continue;
                }

                if (!InjectDll(targetPid, tempDllPath, targetProcess))
                {
                    CleanupTempDllOnExit(tempDllPath, targetProcess);
                    MessageBoxW(hwnd, L"注入失败，请确认权限充足", L"注入失败", MB_ICONERROR | MB_OK);
                    DestroyWindow(hwnd);
                    continue;
                }

                injected = true;
                SetWindowTextW(hwnd, L"MIO 注入器 - 注入成功，等待游戏退出以清理");

                HANDLE cleanerThread = CreateThread(nullptr, 0, [](LPVOID param) -> DWORD {
                    auto* payload = static_cast<std::pair<std::wstring, HANDLE>*>(param);
                    CleanupTempDllOnExit(payload->first, payload->second);
                    delete payload;
                    return 0;
                }, new std::pair<std::wstring, HANDLE>(tempDllPath, targetProcess), 0, nullptr);

                if (cleanerThread)
                    CloseHandle(cleanerThread);

                MessageBoxW(hwnd, L"注入成功", L"完成", MB_ICONINFORMATION | MB_OK);
                DestroyWindow(hwnd);
            }
        }

        Sleep(10);
    }

    return 0;
}
