#pragma once
#include <string>
#include <vector>

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

struct EnergyState
{
    bool lock_enabled = false;
    float current_value = 0.0f;
    bool last_read_ok = false;
};

struct FlyState
{
    bool enabled = false;
    float step = 1.0f;
    float speed_multiplier = 3.0f;
    bool has_anchor = false;
    float anchor_x = 0.0f;
    float anchor_y = 0.0f;
};

namespace Features {
    extern TeleportState g_teleport;
    extern EnergyState g_energy;
    extern FlyState g_fly;

    void UpdateTeleportState();
    void HandleHotkeys();
    
    // Commands used by UI
    void AddTeleportEntry();
    void DeleteSelectedEntry();
    void TeleportToSelected();
}
