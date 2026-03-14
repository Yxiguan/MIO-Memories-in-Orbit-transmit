#pragma once
#include <cstdint>

namespace Game {
    constexpr std::uintptr_t kBaseAddress = 0x10EFF48;
    constexpr std::uintptr_t kOffsetX = 0x18;
    constexpr std::uintptr_t kOffsetY = 0x1C;
    constexpr std::uintptr_t kEnergyAddress = 0x11119A8;
    constexpr float kEnergyLockValue = 100.0f;
    constexpr const char *kGameModuleName = "mio.exe";
    constexpr const char *kTeleportFileName = "TeleportLocations.txt";
}
