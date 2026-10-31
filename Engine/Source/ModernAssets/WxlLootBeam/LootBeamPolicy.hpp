// GPL-3.0-or-later.
#pragma once
#include <cstdint>

namespace wxl::scripts::lootbeam
{
    inline constexpr unsigned kMaximumBeams = 32;
    inline constexpr float kMaximumDistance = 150.0f;
    inline bool Lootable(uint32_t type, uint32_t health, uint32_t flags)
    {
        return (type & 8) && !(type & 16) && health == 0 && (flags & 1);
    }
    inline bool HasRemainingLoot(uint32_t hint)
    {
        return hint >= 1 && hint <= 9;
    }
    // The module's per-viewer hint is authoritative, including downward quality changes and zero.
    inline uint32_t TierColour(uint32_t hint)
    {
        constexpr uint32_t colours[] = {0xD8D8D8, 0x9D9D9D, 0xFFFFFF, 0x1EFF00, 0x0070DD,
            0xA335EE, 0xFF8000, 0xE6CC80, 0xE6CC80, 0xFFCC40};
        return colours[hint <= 9 ? hint : 0];
    }
}
