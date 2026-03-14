#include <framework/stdafx.h>
#include "Memory.h"
#include "Offsets.h"

namespace Game {
    bool ResolveAxisBase(std::uintptr_t &out_base)
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

    float *GetAxisPtr(std::uintptr_t offset)
    {
        std::uintptr_t resolved_base = 0;
        if (!ResolveAxisBase(resolved_base))
            return nullptr;

        auto base_ptr = reinterpret_cast<std::uint8_t *>(resolved_base);
        return reinterpret_cast<float *>(base_ptr + offset);
    }

    float *GetEnergyPtr()
    {
        HMODULE module = GetModuleHandleA(kGameModuleName);
        if (!module)
            return nullptr;

        auto base_ptr = reinterpret_cast<std::uint8_t *>(module);
        return reinterpret_cast<float *>(base_ptr + kEnergyAddress);
    }

    bool ReadEnergy(float &out_value)
    {
        auto ptr = GetEnergyPtr();
        if (!ptr)
            return false;

        __try
        {
            out_value = *ptr;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool WriteEnergy(float value)
    {
        auto ptr = GetEnergyPtr();
        if (!ptr)
            return false;

        __try
        {
            *ptr = value;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ReadAxes(float &out_x, float &out_y)
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

    bool WriteAxes(float x, float y)
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
}
