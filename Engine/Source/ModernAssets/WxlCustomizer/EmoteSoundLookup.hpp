// Bounds-safe lookup for races outside the 12340 emote-sound cache. GPLv3.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace wxl::customizer::emotes
{
    constexpr uint32_t kNativeRaceCount = 22;
    constexpr uint32_t kMaximumRace = 255;
    constexpr uint32_t kMaximumRows = 1000000;
    constexpr uint32_t kNoVariant = std::numeric_limits<uint32_t>::max();
    constexpr uint32_t kTraceLimit = 12;

    inline bool ShouldTraceSilly(uint32_t emote, uint32_t race, std::atomic<uint32_t>& count)
    {
        if (emote != 329 || (race != 9 && race != 14 && race != 22))
            return false;
        // Saturate instead of fetch_add: long sessions cannot wrap and resume logging.
        uint32_t used = count.load(std::memory_order_relaxed);
        while (used < kTraceLimit)
            if (count.compare_exchange_weak(used, used + 1, std::memory_order_relaxed))
                return true;
        return false;
    }

    struct SoundRow
    {
        uint32_t id;
        uint32_t emote;
        uint32_t race;
        uint32_t sex;
        uint32_t soundKit;
    };
    static_assert(sizeof(SoundRow) == 20);

    inline bool IsExtendedIdentity(uint32_t race, uint32_t sex)
    {
        return race >= kNativeRaceCount && race <= kMaximumRace && sex < 2;
    }

    inline uint32_t FindExtendedSound(SoundRow const* rows, uint32_t count,
        uint32_t emote, uint32_t race, uint32_t sex)
    {
        if (!rows || count > kMaximumRows || !IsExtendedIdentity(race, sex))
            return 0;
        uint32_t result = 0;
        // Last matching row wins, exactly as in the native cache initializer.
        for (uint32_t index = 0; index < count; ++index)
            if (rows[index].emote == emote && rows[index].race == race && rows[index].sex == sex)
                result = rows[index].soundKit;
        // The native SoundEntries cache lookup uses signed 32-bit comparisons.
        return result <= static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ? result : 0;
    }

    template <typename Original, typename Extended>
    uint32_t Dispatch(uint32_t emote, uint32_t race, uint32_t sex,
        uint32_t unavailable, Original original, Extended extended)
    {
        // Leave all original races and their native rejection behavior untouched.
        if (race < kNativeRaceCount)
            return original(emote, race, sex);
        if (!IsExtendedIdentity(race, sex))
            return unavailable;
        return extended(emote, race, sex);
    }

    template <typename GetCache, typename GetCount, typename Select>
    uint32_t PickVariant(uint32_t soundKit, GetCache getCache, GetCount getCount, Select select)
    {
        if (!soundKit || soundKit > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()))
            return kNoVariant;
        auto* cache = getCache(soundKit);
        if (!cache)
            return kNoVariant;
        uint32_t const count = getCount(cache);
        if (!count || count > 10) // SoundEntries.dbc has ten filename slots.
            return kNoVariant;
        uint32_t variant = 0;
        // Preserve the game's weighted/no-repeat selection and emote channel arguments.
        select(cache, 2U, &variant, -1, 5U);
        return variant < count ? variant : kNoVariant;
    }
}
