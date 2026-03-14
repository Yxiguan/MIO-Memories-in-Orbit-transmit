#pragma once
#include <cstdint>

namespace Game {
    bool ResolveAxisBase(std::uintptr_t &out_base);
    float *GetAxisPtr(std::uintptr_t offset);
    float *GetEnergyPtr();

    bool ReadEnergy(float &out_value);
    bool WriteEnergy(float value);
    bool ReadAxes(float &out_x, float &out_y);
    bool WriteAxes(float x, float y);
}
