// Pure appearance-selection rules shared by the native bridge and its tests.
#pragma once

#include <cstdint>

namespace wxl::customizer
{
    constexpr bool HasVariableFaceSets(uint32_t race)
    {
        return race == 9 || race == 22 || race == 23 || race == 26;
    }

    template <typename IsValidSection>
    bool ResolveSkinFace(uint32_t race, uint32_t skin, uint32_t preferredFace,
        IsValidSection const& valid, uint32_t& selectedFace)
    {
        if (race > 255 || skin > 255 || preferredFace > 255 ||
            !valid(0, 0, skin) || !valid(4, 0, skin))
            return false;

        if (valid(1, preferredFace, skin))
        {
            selectedFace = preferredFace;
            return true;
        }

        // Custom Goblin, Worgen, Void Elf and Eredar palettes may have different face sets. Keep
        // the face when possible; otherwise select a valid face so the current
        // face cannot hide an entire skin family.
        if (!HasVariableFaceSets(race))
            return false;

        for (uint32_t face = 0; face < 256; ++face)
            if (valid(1, face, skin))
            {
                selectedFace = face;
                return true;
            }

        return false;
    }
}
