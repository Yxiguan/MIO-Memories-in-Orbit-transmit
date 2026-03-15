#include <framework/stdafx.h>
#include "Cheats.h"
#include <game/Memory.h>
#include <game/Offsets.h>
#include <windows.h>
#include <fstream>
#include <sstream>
#include <cstring>

namespace Features {
    TeleportState g_teleport;
    EnergyState g_energy;
    FlyState g_fly;

    static bool ApplyEnergyPatchAt(std::uintptr_t offset, std::uint8_t *original_bytes, bool &applied, bool enable)
    {
        HMODULE module = GetModuleHandleA(Game::kGameModuleName);
        if (!module)
            return false;

        auto base_ptr = reinterpret_cast<std::uint8_t *>(module);
        auto patch_ptr = base_ptr + offset;

        DWORD old_protect = 0;
        if (!VirtualProtect(patch_ptr, Game::kEnergyPatchSize, PAGE_EXECUTE_READWRITE, &old_protect))
            return false;

        if (enable)
        {
            if (!applied)
                std::memcpy(original_bytes, patch_ptr, Game::kEnergyPatchSize);

            std::memset(patch_ptr, 0x90, Game::kEnergyPatchSize);
            applied = true;
        }
        else
        {
            if (applied)
            {
                std::memcpy(patch_ptr, original_bytes, Game::kEnergyPatchSize);
                applied = false;
            }
        }

        DWORD temp = 0;
        VirtualProtect(patch_ptr, Game::kEnergyPatchSize, old_protect, &temp);
        FlushInstructionCache(GetCurrentProcess(), patch_ptr, Game::kEnergyPatchSize);
        return true;
    }

    static bool ApplyEnergyPatch(bool enable)
    {
        bool primary_ok = ApplyEnergyPatchAt(Game::kEnergyPatchOffset, g_energy.original_bytes, g_energy.patch_applied, enable);
        bool secondary_ok = ApplyEnergyPatchAt(Game::kEnergyPatchOffsetAlt, g_energy.original_bytes_alt, g_energy.patch_applied_alt, enable);
        return primary_ok && secondary_ok;
    }

    static std::string GetTeleportFilePath()
    {
        char module_path[MAX_PATH] = {};
        DWORD len = GetModuleFileNameA(nullptr, module_path, MAX_PATH);
        if (len == 0 || len == MAX_PATH)
            return std::string(Game::kTeleportFileName);

        std::string path(module_path);
        auto pos = path.find_last_of("\\/");
        if (pos == std::string::npos)
            return std::string(Game::kTeleportFileName);

        return path.substr(0, pos + 1) + Game::kTeleportFileName;
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

    static void UpdateFlyMovement()
    {
        if (!g_fly.enabled)
        {
            g_fly.has_anchor = false;
            return;
        }

        float x = 0.0f;
        float y = 0.0f;
        if (!Game::ReadAxes(x, y))
            return;

        if (!g_fly.has_anchor)
        {
            g_fly.anchor_x = x;
            g_fly.anchor_y = y;
            g_fly.has_anchor = true;
        }

        float step = g_fly.step;
        if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
            step *= g_fly.speed_multiplier;

        if (GetAsyncKeyState('W') & 0x8000)
            g_fly.anchor_y += step;
        if (GetAsyncKeyState('S') & 0x8000)
            g_fly.anchor_y -= step;
        if (GetAsyncKeyState('A') & 0x8000)
            g_fly.anchor_x -= step;
        if (GetAsyncKeyState('D') & 0x8000)
            g_fly.anchor_x += step;

        Game::WriteAxes(g_fly.anchor_x, g_fly.anchor_y);
    }

    void UpdateTeleportState()
    {
        LoadTeleportEntries();
        g_teleport.last_read_ok = Game::ReadAxes(g_teleport.current_x, g_teleport.current_y);
        g_energy.last_read_ok = Game::ReadEnergy(g_energy.current_value);

        if (g_energy.lock_enabled)
        {
            if (!g_energy.patch_applied || !g_energy.patch_applied_alt)
                ApplyEnergyPatch(true);
        }
        else if (g_energy.patch_applied || g_energy.patch_applied_alt)
        {
            ApplyEnergyPatch(false);
        }

        UpdateFlyMovement();
    }

    void AddTeleportEntry()
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

    void DeleteSelectedEntry()
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

    void TeleportToSelected()
    {
        if (g_teleport.selected_index < 0 || g_teleport.selected_index >= static_cast<int>(g_teleport.entries.size()))
            return;

        const auto &entry = g_teleport.entries[g_teleport.selected_index];
        Game::WriteAxes(entry.x, entry.y);
    }

    void HandleHotkeys()
    {
        if (GetAsyncKeyState(VK_F6) & 1)
            TeleportToSelected();

        if (GetAsyncKeyState(VK_F7) & 1)
            g_teleport.menu_open = !g_teleport.menu_open;
    }
}
