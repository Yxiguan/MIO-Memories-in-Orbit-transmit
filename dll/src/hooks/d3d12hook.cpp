#include <framework/stdafx.h>
#include "d3d12hook.h"
#include <kiero.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx12.h>
#include <cstdint>
#include <fstream>
#include <sstream>


// Debug
#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")

typedef HRESULT(__stdcall *PresentFunc)(IDXGISwapChain *pSwapChain, UINT SyncInterval, UINT Flags);
PresentFunc oPresent = nullptr;

typedef void(__stdcall *ExecuteCommandListsFunc)(ID3D12CommandQueue *pCommandQueue, UINT NumCommandLists, ID3D12CommandList *const *ppCommandLists);
ExecuteCommandListsFunc oExecuteCommandLists = nullptr;

typedef HRESULT(__stdcall *ResizeBuffers)(IDXGISwapChain3 *pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
ResizeBuffers oResizeBuffers;

typedef HRESULT(__stdcall *SignalFunc)(ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 value);
SignalFunc oSignal = nullptr;

HWND window;
WNDPROC oWndProc;

struct FrameContext
{
    ID3D12CommandAllocator *CommandAllocator;
    UINT64 FenceValue; // In imgui original code // i didn't use it
    ID3D12Resource *g_mainRenderTargetResource = {};
    D3D12_CPU_DESCRIPTOR_HANDLE g_mainRenderTargetDescriptor = {};
};

// Data
static int const NUM_FRAMES_IN_FLIGHT = 3;
// static FrameContext*                g_frameContext[NUM_FRAMES_IN_FLIGHT] = {};
//  Modified
FrameContext *g_frameContext;
static UINT g_frameIndex = 0;
static UINT g_fenceValue = 0;

// static int const                    NUM_BACK_BUFFERS = 3; // original
static int NUM_BACK_BUFFERS = -1;
static ID3D12Device *g_pd3dDevice = nullptr;
static ID3D12DescriptorHeap *g_pd3dRtvDescHeap = nullptr;
static ID3D12DescriptorHeap *g_pd3dSrvDescHeap = nullptr;
static ID3D12CommandQueue *g_pd3dCommandQueue = nullptr;
static ID3D12GraphicsCommandList *g_pd3dCommandList = nullptr;
static ID3D12Fence *g_fence = nullptr;
static HANDLE g_fenceEvent = nullptr;
static UINT64 g_fenceLastSignaledValue = 0;
static IDXGISwapChain3 *g_pSwapChain = nullptr;
static bool g_SwapChainOccluded = false;
static HANDLE g_hSwapChainWaitableObject = nullptr;
// static ID3D12Resource*              g_mainRenderTargetResource[NUM_BACK_BUFFERS] = {}; // Original
// static D3D12_CPU_DESCRIPTOR_HANDLE  g_mainRenderTargetDescriptor[NUM_BACK_BUFFERS] = {}; // Original
static UINT g_ResizeWidth = 0, g_ResizeHeight = 0;

struct TeleportEntry
{
    float x = 0.0f;
    float y = 0.0f;
    std::string note;
};

struct TeleportState
{
    bool menu_open = true;
    float current_x = 0.0f;
    float current_y = 0.0f;
    bool last_read_ok = false;
    std::vector<TeleportEntry> entries;
    int selected_index = -1;
    bool loaded = false;
    char pending_note[128] = {};
};

static TeleportState g_teleport;

constexpr std::uintptr_t kBaseAddress = 0x10EFF48;
constexpr std::uintptr_t kOffsetX = 0x18;
constexpr std::uintptr_t kOffsetY = 0x1C;
constexpr const char *kGameModuleName = "mio.exe";

constexpr const char *kTeleportFileName = "TeleportLocations.txt";

void CreateRenderTarget()
{
    if (!g_pSwapChain || !g_pd3dDevice || !g_pd3dRtvDescHeap || !g_frameContext || NUM_BACK_BUFFERS <= 0)
        return;

    // Получаем размер дескриптора RTV и начальный дескриптор
    SIZE_T rtvDescriptorSize = g_pd3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_pd3dRtvDescHeap->GetCPUDescriptorHandleForHeapStart();

    // Сначала инициализируем дескрипторы
    for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
    {
        g_frameContext[i].g_mainRenderTargetDescriptor = rtvHandle;
        rtvHandle.ptr += rtvDescriptorSize;
    }

    // Затем создаем render target views
    for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
    {
        ID3D12Resource *pBackBuffer = nullptr;
        if (SUCCEEDED(g_pSwapChain->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer))))
        {
            g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, g_frameContext[i].g_mainRenderTargetDescriptor);
            g_frameContext[i].g_mainRenderTargetResource = pBackBuffer;
        }
        else
        {
            LOG_ERROR("Failed to get back buffer %d", i);
            if (pBackBuffer)
            {
                pBackBuffer->Release();
                pBackBuffer = nullptr;
            }
        }
    }
}

void CleanupRenderTarget()
{
    if (g_frameContext && NUM_BACK_BUFFERS > 0)
    {
        for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
        {
            if (g_frameContext[i].g_mainRenderTargetResource)
            {
                g_frameContext[i].g_mainRenderTargetResource->Release();
                g_frameContext[i].g_mainRenderTargetResource = nullptr;
            }
        }
    }
}

void WaitForLastSubmittedFrame()
{
    FrameContext *frameCtx = &g_frameContext[g_frameIndex % NUM_FRAMES_IN_FLIGHT];

    UINT64 fenceValue = frameCtx->FenceValue;
    if (fenceValue == 0)
        return; // No fence was signaled

    frameCtx->FenceValue = 0;
    if (g_fence->GetCompletedValue() >= fenceValue)
        return;

    g_fence->SetEventOnCompletion(fenceValue, g_fenceEvent);
    WaitForSingleObject(g_fenceEvent, INFINITE);
}

static std::string GetTeleportFilePath()
{
    char module_path[MAX_PATH] = {};
    DWORD len = GetModuleFileNameA(nullptr, module_path, MAX_PATH);
    if (len == 0 || len == MAX_PATH)
        return std::string(kTeleportFileName);

    std::string path(module_path);
    auto pos = path.find_last_of("\\/");
    if (pos == std::string::npos)
        return std::string(kTeleportFileName);

    return path.substr(0, pos + 1) + kTeleportFileName;
}

static void LoadTeleportEntries()
{
    if (g_teleport.loaded)
        return;

    g_teleport.loaded = true;
    g_teleport.entries.clear();
    g_teleport.selected_index = -1;

    std::ifstream file(GetTeleportFilePath());
    if (!file.is_open())
        return;

    std::string line;
    while (std::getline(file, line))
    {
        if (line.empty())
            continue;

        std::istringstream iss(line);
        TeleportEntry entry;
        if (!(iss >> entry.x >> entry.y))
            continue;

        std::string remaining;
        if (std::getline(iss, remaining))
        {
            auto pos = remaining.find_first_not_of(' ');
            if (pos != std::string::npos)
                entry.note = remaining.substr(pos);
        }

        g_teleport.entries.push_back(entry);
    }

    if (!g_teleport.entries.empty())
        g_teleport.selected_index = 0;
}

static void SaveTeleportEntries()
{
    std::ofstream file(GetTeleportFilePath(), std::ios::trunc);
    if (!file.is_open())
        return;

    for (const auto &entry : g_teleport.entries)
    {
        file << entry.x << ' ' << entry.y;
        if (!entry.note.empty())
            file << ' ' << entry.note;
        file << '\n';
    }
}

static bool ResolveAxisBase(std::uintptr_t &out_base)
{
    HMODULE module = GetModuleHandleA(kGameModuleName);
    if (!module)
        return false;

    auto base_ptr = reinterpret_cast<std::uint8_t *>(module);
    auto pointer_addr = reinterpret_cast<std::uintptr_t *>(base_ptr + kBaseAddress);

    __try
    {
        out_base = *pointer_addr;
        return out_base != 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static float *GetAxisPtr(std::uintptr_t offset)
{
    std::uintptr_t resolved_base = 0;
    if (!ResolveAxisBase(resolved_base))
        return nullptr;

    auto base_ptr = reinterpret_cast<std::uint8_t *>(resolved_base);
    return reinterpret_cast<float *>(base_ptr + offset);
}

static bool ReadAxes(float &out_x, float &out_y)
{
    auto x_ptr = GetAxisPtr(kOffsetX);
    auto y_ptr = GetAxisPtr(kOffsetY);

    if (!x_ptr || !y_ptr)
        return false;

    __try
    {
        out_x = *x_ptr;
        out_y = *y_ptr;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static bool WriteAxes(float x, float y)
{
    auto x_ptr = GetAxisPtr(kOffsetX);
    auto y_ptr = GetAxisPtr(kOffsetY);

    if (!x_ptr || !y_ptr)
        return false;

    __try
    {
        *x_ptr = x;
        *y_ptr = y;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static void UpdateTeleportState()
{
    LoadTeleportEntries();
    g_teleport.last_read_ok = ReadAxes(g_teleport.current_x, g_teleport.current_y);
}

static void AddTeleportEntry()
{
    if (!g_teleport.last_read_ok)
        return;

    TeleportEntry entry;
    entry.x = g_teleport.current_x;
    entry.y = g_teleport.current_y;
    entry.note = g_teleport.pending_note;
    g_teleport.entries.push_back(entry);
    g_teleport.selected_index = static_cast<int>(g_teleport.entries.size()) - 1;
    g_teleport.pending_note[0] = '\0';
    SaveTeleportEntries();
}

static void DeleteSelectedEntry()
{
    if (g_teleport.selected_index < 0 || g_teleport.selected_index >= static_cast<int>(g_teleport.entries.size()))
        return;

    g_teleport.entries.erase(g_teleport.entries.begin() + g_teleport.selected_index);

    if (g_teleport.entries.empty())
        g_teleport.selected_index = -1;
    else if (g_teleport.selected_index >= static_cast<int>(g_teleport.entries.size()))
        g_teleport.selected_index = static_cast<int>(g_teleport.entries.size()) - 1;

    SaveTeleportEntries();
}

static void TeleportToSelected()
{
    if (g_teleport.selected_index < 0 || g_teleport.selected_index >= static_cast<int>(g_teleport.entries.size()))
        return;

    const auto &entry = g_teleport.entries[g_teleport.selected_index];
    WriteAxes(entry.x, entry.y);
}

static void DrawTeleportMenu()
{
    if (!g_teleport.menu_open)
        return;

    ImGui::Begin("Teleport", &g_teleport.menu_open);
    ImGui::Text("当前坐标: X=%.3f  Y=%.3f", g_teleport.current_x, g_teleport.current_y);
    ImGui::Text("读取状态: %s", g_teleport.last_read_ok ? "正常" : "失败");

    ImGui::InputText("备注", g_teleport.pending_note, sizeof(g_teleport.pending_note));

    if (ImGui::Button("保存当前坐标"))
        AddTeleportEntry();

    ImGui::SameLine();
    if (ImGui::Button("传送到选中"))
        TeleportToSelected();

    ImGui::SameLine();
    if (ImGui::Button("删除选中"))
        DeleteSelectedEntry();

    ImGui::Text("F6 传送选中 | F7 显示或隐藏菜单");

    if (ImGui::BeginListBox("坐标列表", ImVec2(-FLT_MIN, 200.0f)))
    {
        for (int i = 0; i < static_cast<int>(g_teleport.entries.size()); ++i)
        {
            const auto &entry = g_teleport.entries[i];
            char label[192] = {};
            if (!entry.note.empty())
                sprintf_s(label, "[%d] X=%.3f Y=%.3f (%s)", i + 1, entry.x, entry.y, entry.note.c_str());
            else
                sprintf_s(label, "[%d] X=%.3f Y=%.3f", i + 1, entry.x, entry.y);
            const bool selected = (g_teleport.selected_index == i);
            if (ImGui::Selectable(label, selected))
                g_teleport.selected_index = i;

            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndListBox();
    }

    ImGui::End();
}

static void HandleHotkeys()
{
    if (GetAsyncKeyState(VK_F6) & 1)
        TeleportToSelected();

    if (GetAsyncKeyState(VK_F7) & 1)
        g_teleport.menu_open = !g_teleport.menu_open;
}

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT APIENTRY WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam))
        return true;

    return CallWindowProc(oWndProc, hwnd, uMsg, wParam, lParam);
}

void InitImGui()
{
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    ImFont *font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttc", 18.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());
    if (font)
        io.FontDefault = font;

    ImGui::StyleColorsLight();

    ImGui_ImplWin32_Init(window);

    ImGui_ImplDX12_Init(g_pd3dDevice, NUM_FRAMES_IN_FLIGHT,
                        DXGI_FORMAT_R8G8B8A8_UNORM,
                        g_pd3dSrvDescHeap,
                        g_pd3dSrvDescHeap->GetCPUDescriptorHandleForHeapStart(),
                        g_pd3dSrvDescHeap->GetGPUDescriptorHandleForHeapStart());
}

HRESULT __fastcall hkPresent(IDXGISwapChain3 *pSwapChain, UINT SyncInterval, UINT Flags)
{
    static bool init = false;

    if (!init)
    {
        // Ждем пока не получим CommandQueue через ExecuteCommandLists
        if (!g_pd3dCommandQueue)
            return oPresent(pSwapChain, SyncInterval, Flags);

        if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D12Device), (void **)&g_pd3dDevice)))
        {
            DXGI_SWAP_CHAIN_DESC sdesc;
            pSwapChain->GetDesc(&sdesc);
            window = sdesc.OutputWindow;
            NUM_BACK_BUFFERS = sdesc.BufferCount;

            // SRV Heap
            {
                D3D12_DESCRIPTOR_HEAP_DESC desc = {};
                desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                desc.NumDescriptors = 1;
                desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                if (FAILED(g_pd3dDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_pd3dSrvDescHeap))))
                    return oPresent(pSwapChain, SyncInterval, Flags);
            }

            // RTV Heap
            {
                D3D12_DESCRIPTOR_HEAP_DESC desc = {};
                desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
                desc.NumDescriptors = NUM_BACK_BUFFERS;
                desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
                desc.NodeMask = 1;
                if (FAILED(g_pd3dDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_pd3dRtvDescHeap))))
                    return oPresent(pSwapChain, SyncInterval, Flags);
            }

            // Command Allocator
            ID3D12CommandAllocator *allocator;
            if (FAILED(g_pd3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))))
                return oPresent(pSwapChain, SyncInterval, Flags);

            // Command List
            if (FAILED(g_pd3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&g_pd3dCommandList))))
            {
                allocator->Release();
                return oPresent(pSwapChain, SyncInterval, Flags);
            }
            g_pd3dCommandList->Close();

            // Frame Contexts
            g_frameContext = new FrameContext[NUM_BACK_BUFFERS];
            if (!g_frameContext)
            {
                allocator->Release();
                return oPresent(pSwapChain, SyncInterval, Flags);
            }

            for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
            {
                g_frameContext[i].CommandAllocator = allocator;
                g_frameContext[i].FenceValue = 0;
            }

            // Fence & Events
            if (FAILED(g_pd3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence))))
            {
                allocator->Release();
                return oPresent(pSwapChain, SyncInterval, Flags);
            }

            g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            if (g_fenceEvent == nullptr)
            {
                allocator->Release();
                return oPresent(pSwapChain, SyncInterval, Flags);
            }

            g_hSwapChainWaitableObject = pSwapChain->GetFrameLatencyWaitableObject();
            g_pSwapChain = pSwapChain;

            // Create RenderTarget
            CreateRenderTarget();

            // Hook window procedure
            oWndProc = (WNDPROC)SetWindowLongPtr(window, GWLP_WNDPROC, (__int3264)(LONG_PTR)WndProc);

            // Initialize ImGui last, after all DirectX objects are created
            InitImGui();

            init = true;
        }
        return oPresent(pSwapChain, SyncInterval, Flags);
    }

    // Проверяем все необходимые объекты
    if (!g_pd3dCommandQueue || !g_pd3dDevice || !g_frameContext || !g_pd3dSrvDescHeap)
        return oPresent(pSwapChain, SyncInterval, Flags);

    // Обработка изменения размера
    if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
    {
        WaitForLastSubmittedFrame();
        CleanupRenderTarget();
        g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
        g_ResizeWidth = g_ResizeHeight = 0;
        CreateRenderTarget();
    }

    // Обработка переключения окна
    UpdateTeleportState();
    HandleHotkeys();

    // 开始新帧
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGui::GetIO().MouseDrawCursor = g_teleport.menu_open;

    DrawTeleportMenu();

    // Получаем текущий back buffer
    UINT backBufferIdx = g_pSwapChain->GetCurrentBackBufferIndex();
    FrameContext &frameCtx = g_frameContext[backBufferIdx];

    // Сброс command allocator
    frameCtx.CommandAllocator->Reset();

    // Подготовка к рендерингу
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = g_frameContext[backBufferIdx].g_mainRenderTargetResource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

    // Выполнение команд рендеринга
    g_pd3dCommandList->Reset(frameCtx.CommandAllocator, nullptr);
    g_pd3dCommandList->ResourceBarrier(1, &barrier);
    g_pd3dCommandList->OMSetRenderTargets(1, &g_frameContext[backBufferIdx].g_mainRenderTargetDescriptor, FALSE, nullptr);
    g_pd3dCommandList->SetDescriptorHeaps(1, &g_pd3dSrvDescHeap);

    // Рендеринг ImGui
    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_pd3dCommandList);

    // Возврат ресурса в состояние present
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    g_pd3dCommandList->ResourceBarrier(1, &barrier);
    g_pd3dCommandList->Close();

    // Выполнение command list
    g_pd3dCommandQueue->ExecuteCommandLists(1, reinterpret_cast<ID3D12CommandList *const *>(&g_pd3dCommandList));

    return oPresent(pSwapChain, SyncInterval, Flags);
}

void __fastcall hkExecuteCommandLists(ID3D12CommandQueue *pCommandQueue, UINT NumCommandLists, ID3D12CommandList *const *ppCommandLists)
{
    if (!g_pd3dCommandQueue)
    {
        g_pd3dCommandQueue = pCommandQueue;
    }

    oExecuteCommandLists(pCommandQueue, NumCommandLists, ppCommandLists);
}

HRESULT __fastcall hkResizeBuffers(IDXGISwapChain3 *pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
    // Проверяем готовность к изменению размера
    if (!g_pd3dDevice || !g_pSwapChain)
    {
        LOG_ERROR("Cannot resize - DirectX objects not initialized");
        return oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
    }

    if (g_pd3dDevice)
    {
        ImGui_ImplDX12_InvalidateDeviceObjects();
    }

    CleanupRenderTarget();

    // Сохраняем новое количество буферов
    NUM_BACK_BUFFERS = BufferCount;

    // Вызываем оригинальную функцию
    HRESULT result = oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);

    if (SUCCEEDED(result))
    {
        // Пересоздаем наши ресурсы только если успешно изменили размер
        CreateRenderTarget();
        if (g_pd3dDevice)
        {
            ImGui_ImplDX12_CreateDeviceObjects();
        }
    }
    else
    {
        LOG_ERROR("ResizeBuffers failed with error code: 0x%X", result);
    }

    return result;
}

HRESULT __fastcall hkSignal(ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 value)
{
    if (g_pd3dCommandQueue != nullptr && queue == g_pd3dCommandQueue)
    {
        g_fence = fence;
        g_fenceValue = value;
    }
    return oSignal(queue, fence, value);
    ;
}

bool InitD3D12Hook()
{
    LOG_INFO("Waiting for process initialization...");

    HANDLE d3d12Module = nullptr;
    HANDLE dxgiModule = nullptr;

    while (true)
    {
        d3d12Module = GetModuleHandleA("d3d12.dll");
        dxgiModule = GetModuleHandleA("dxgi.dll");

        if (d3d12Module && dxgiModule)
            break;

        if (WaitForSingleObject(GetCurrentProcess(), 1000) != WAIT_TIMEOUT)
        {
            LOG_ERROR("Process terminated while waiting for DirectX");
            return false;
        }

        LOG_INFO("Waiting for DirectX modules...");
    }

    LOG_INFO("DirectX modules found, initializing hooks...");

    try
    {
        auto kieroStatus = kiero::init(kiero::RenderType::D3D12);
        if (kieroStatus != kiero::Status::Success)
        {
            LOG_ERROR("Failed to initialize kiero");
            return false;
        }

        bool hooks_success = true;

        if (kiero::bind(54, (void **)&oExecuteCommandLists, hkExecuteCommandLists) != kiero::Status::Success)
        {
            LOG_ERROR("Failed to hook ExecuteCommandLists");
            hooks_success = false;
        }

        if (kiero::bind(58, (void **)&oSignal, hkSignal) != kiero::Status::Success)
        {
            LOG_ERROR("Failed to hook Signal");
            hooks_success = false;
        }

        if (kiero::bind(140, (void **)&oPresent, hkPresent) != kiero::Status::Success)
        {
            LOG_ERROR("Failed to hook Present");
            hooks_success = false;
        }

        if (kiero::bind(145, (void **)&oResizeBuffers, hkResizeBuffers) != kiero::Status::Success)
        {
            LOG_ERROR("Failed to hook ResizeBuffers");
            hooks_success = false;
        }

        if (!hooks_success)
        {
            LOG_ERROR("Failed to create one or more hooks");
            kiero::shutdown();
            return false;
        }

        LOG_INFO("D3D12 successfully hooked using kiero");
        return true;
    }
    catch (...)
    {
        LOG_ERROR("Exception during hook initialization");
        kiero::shutdown();
        return false;
    }
}

void ReleaseD3D12Hook()
{
    kiero::shutdown();

    if (g_pd3dDevice)
    {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }

    if (g_pd3dCommandQueue && g_fence && g_fenceEvent)
    {
        WaitForLastSubmittedFrame();
    }

    // Clean up render targets
    CleanupRenderTarget();

    // Release command allocators
    if (g_frameContext)
    {
        for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
        {
            if (g_frameContext[i].CommandAllocator)
            {
                g_frameContext[i].CommandAllocator->Release();
                g_frameContext[i].CommandAllocator = nullptr;
            }
        }
        delete[] g_frameContext;
        g_frameContext = nullptr;
    }

    if (g_pd3dCommandList)
    {
        g_pd3dCommandList->Release();
        g_pd3dCommandList = nullptr;
    }

    if (g_pd3dCommandQueue)
    {
        g_pd3dCommandQueue->Release();
        g_pd3dCommandQueue = nullptr;
    }

    // Close handles before releasing resources
    if (g_fenceEvent)
    {
        CloseHandle(g_fenceEvent);
        g_fenceEvent = nullptr;
    }

    if (g_hSwapChainWaitableObject)
    {
        CloseHandle(g_hSwapChainWaitableObject);
        g_hSwapChainWaitableObject = nullptr;
    }

    if (g_pd3dRtvDescHeap)
    {
        g_pd3dRtvDescHeap->Release();
        g_pd3dRtvDescHeap = nullptr;
    }

    if (g_pd3dSrvDescHeap)
    {
        g_pd3dSrvDescHeap->Release();
        g_pd3dSrvDescHeap = nullptr;
    }

    if (g_fence)
    {
        g_fence->Release();
        g_fence = nullptr;
    }

    if (oWndProc && window)
    {
        SetWindowLongPtr(window, GWLP_WNDPROC, (LONG_PTR)oWndProc);
        oWndProc = nullptr;
    }

    g_pd3dDevice = nullptr;
    g_pSwapChain = nullptr;
    window = nullptr;

    NUM_BACK_BUFFERS = -1;
    g_frameIndex = 0;
    g_fenceValue = 0;
}
