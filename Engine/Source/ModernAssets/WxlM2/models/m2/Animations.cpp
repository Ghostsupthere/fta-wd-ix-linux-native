// Masks blend time, remaps curated high ids, rebuilds the native sequenceLookup hash table.
// Copyright (C) 2026 WarcraftXL
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

#include "Animations.hpp"

#include "core/Logger.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>
#include <vector>

namespace wxl::modern::assets::m2::animations
{
    namespace fmt = wxl::structure::m2;

    namespace
    {
        constexpr uint32_t kMaxLookupEntries = 4096;
        constexpr uint16_t kEmptyLookup = std::numeric_limits<uint16_t>::max();

        bool Align4(uint32_t value, uint32_t& out)
        {
            const uint64_t aligned = (uint64_t(value) + 3u) & ~uint64_t(3u);
            if (aligned > UINT32_MAX) return false;
            out = static_cast<uint32_t>(aligned);
            return true;
        }

        uint16_t RemappedAnimationId(uint16_t id)
        {
            switch (id)
            {
                case 564: return 37;
                case 548: return 41;
                case 556: return 42;
                case 552: return 43;
                case 554: return 44;
                case 562: return 45;
                case 572: return 39;
                case 574: return 187;
                default: return id;
            }
        }

        bool IsPrime(uint32_t value)
        {
            if (value < 2) return false;
            if (!(value & 1u)) return value == 2;
            for (uint32_t divisor = 3; uint64_t(divisor) * divisor <= value; divisor += 2)
                if (value % divisor == 0) return false;
            return true;
        }

        uint32_t LookupCapacity(const fmt::M2Header* md)
        {
            if (!md || !md->sequences.count || !md->sequences.offset) return 0;
            const auto* seqs = reinterpret_cast<const fmt::M2Sequence*>(
                md->base() + md->sequences.offset);

            // Blizzard's table is an open-addressed hash table, not a direct AnimationData-id array.
            // Its canonical capacity is the first prime at least twice the number of unique ids. This
            // reproduces the active BloodElfFemale_HD contract exactly: 322 ids -> 647 slots.
            std::vector<uint16_t> ids;
            ids.reserve(md->sequences.count);
            for (uint32_t i = 0; i < md->sequences.count; ++i)
                ids.push_back(RemappedAnimationId(seqs[i].id));
            std::sort(ids.begin(), ids.end());
            const uint32_t uniqueIds = static_cast<uint32_t>(
                std::unique(ids.begin(), ids.end()) - ids.begin());

            const uint64_t minimum = uint64_t(uniqueIds) * 2u;
            if (minimum > kMaxLookupEntries) return 0;
            for (uint32_t candidate = static_cast<uint32_t>(minimum);
                 candidate <= kMaxLookupEntries; ++candidate)
                if (IsPrime(candidate)) return candidate;
            return 0;
        }

        bool HasCuratedRemap(const fmt::M2Sequence* seqs, uint32_t count)
        {
            for (uint32_t i = 0; i < count; ++i)
                if (RemappedAnimationId(seqs[i].id) != seqs[i].id) return true;
            return false;
        }

        uint32_t ResolveLookup(const fmt::M2Sequence* seqs, uint32_t sequenceCount,
                               const uint16_t* lookup, uint32_t lookupCount, uint16_t id)
        {
            if (!lookup || !lookupCount) return UINT32_MAX;
            uint32_t slot = id % lookupCount;
            for (uint32_t probe = 0; probe < lookupCount; ++probe)
            {
                const uint16_t sequenceIndex = lookup[slot];
                if (sequenceIndex == kEmptyLookup || sequenceIndex >= sequenceCount)
                    return UINT32_MAX;
                if (seqs[sequenceIndex].id == id) return sequenceIndex;

                // Wow.exe 3.3.5a uses cumulative quadratic probing:
                // slot = (slot + n*n) % count for n = 1, 2, ...
                const uint32_t step = probe + 1u;
                slot = (slot + step * step) % lookupCount;
            }
            return UINT32_MAX;
        }

        bool PreferCandidate(const fmt::M2Sequence* seqs, uint32_t candidate, uint32_t current)
        {
            const bool candidateRemapped = RemappedAnimationId(seqs[candidate].id) != seqs[candidate].id;
            const bool currentRemapped = RemappedAnimationId(seqs[current].id) != seqs[current].id;
            if (candidateRemapped != currentRemapped) return candidateRemapped;
            if (seqs[candidate].variationIndex != seqs[current].variationIndex)
                return seqs[candidate].variationIndex == 0;
            return candidate < current;
        }

        bool BuildLookup(const fmt::M2Sequence* seqs, uint32_t sequenceCount,
                         std::vector<uint16_t>& table)
        {
            const uint32_t lookupCount = static_cast<uint32_t>(table.size());
            for (uint32_t sequenceIndex = 0; sequenceIndex < sequenceCount; ++sequenceIndex)
            {
                const uint16_t id = RemappedAnimationId(seqs[sequenceIndex].id);
                uint32_t slot = id % lookupCount;
                bool placed = false;
                for (uint32_t probe = 0; probe < lookupCount; ++probe)
                {
                    const uint16_t current = table[slot];
                    if (current == kEmptyLookup)
                    {
                        table[slot] = static_cast<uint16_t>(sequenceIndex);
                        placed = true;
                        break;
                    }
                    if (RemappedAnimationId(seqs[current].id) == id)
                    {
                        if (PreferCandidate(seqs, sequenceIndex, current))
                            table[slot] = static_cast<uint16_t>(sequenceIndex);
                        placed = true;
                        break;
                    }

                    const uint32_t step = probe + 1u;
                    slot = (slot + step * step) % lookupCount;
                }
                if (!placed) return false;
            }
            return true;
        }
    }

    bool NeedsSequenceLookupExpansion(const fmt::M2Header* md)
    {
        if (!md || !md->sequences.count || !md->sequences.offset) return false;
        const auto* seqs = reinterpret_cast<const fmt::M2Sequence*>(
            md->base() + md->sequences.offset);
        const uint32_t requiredCapacity = LookupCapacity(md);
        if (!requiredCapacity || HasCuratedRemap(seqs, md->sequences.count)) return true;
        if (!md->sequenceLookup.count || !md->sequenceLookup.offset ||
            md->sequenceLookup.count > kMaxLookupEntries ||
            md->sequenceLookup.count < requiredCapacity || !IsPrime(md->sequenceLookup.count))
            return true;

        const auto* lookup = reinterpret_cast<const uint16_t*>(
            md->base() + md->sequenceLookup.offset);
        std::vector<uint16_t> lookupIds;
        lookupIds.reserve(md->sequences.count);
        uint32_t occupied = 0;
        for (uint32_t i = 0; i < md->sequenceLookup.count; ++i)
        {
            if (lookup[i] == kEmptyLookup) continue;
            if (lookup[i] >= md->sequences.count) return true;
            lookupIds.push_back(seqs[lookup[i]].id);
            ++occupied;
        }
        if (uint64_t(occupied) * 2u > md->sequenceLookup.count) return true;
        std::sort(lookupIds.begin(), lookupIds.end());
        if (std::adjacent_find(lookupIds.begin(), lookupIds.end()) != lookupIds.end()) return true;

        struct Requirement
        {
            uint16_t id;
            bool variationZero;
        };
        std::vector<Requirement> requirements;
        requirements.reserve(md->sequences.count);
        for (uint32_t i = 0; i < md->sequences.count; ++i)
            requirements.push_back({ seqs[i].id, seqs[i].variationIndex == 0 });
        std::sort(requirements.begin(), requirements.end(),
                  [](const Requirement& left, const Requirement& right) { return left.id < right.id; });
        const uint32_t requirementCount = static_cast<uint32_t>(requirements.size());
        for (uint32_t begin = 0; begin < requirementCount;)
        {
            uint32_t end = begin + 1;
            bool hasVariationZero = requirements[begin].variationZero;
            while (end < requirementCount && requirements[end].id == requirements[begin].id)
            {
                hasVariationZero = hasVariationZero || requirements[end].variationZero;
                ++end;
            }
            const uint32_t sequenceIndex = ResolveLookup(
                seqs, md->sequences.count, lookup, md->sequenceLookup.count,
                requirements[begin].id);
            if (sequenceIndex == UINT32_MAX ||
                (hasVariationZero && seqs[sequenceIndex].variationIndex != 0))
                return true;
            begin = end;
        }
        return false;
    }

    void Fix(fmt::M2Header* md)
    {
        if (!md->sequences.count || !md->sequences.offset) return;
        auto* seqs = reinterpret_cast<fmt::M2Sequence*>(md->base() + md->sequences.offset);
        for (uint32_t i = 0; i < md->sequences.count; ++i)
            seqs[i].blendTime &= 0xFFFF;
    }

    uint32_t SequenceLookupExtraBytes(const fmt::M2Header* md, uint32_t appendOffset)
    {
        const uint32_t needed = LookupCapacity(md);
        if (!needed) return 0;
        if (!NeedsSequenceLookupExpansion(md)) return 0;

        uint32_t aligned = 0;
        if (!Align4(appendOffset, aligned)) return 0;
        const uint64_t bytes = uint64_t(aligned - appendOffset) + uint64_t(needed) * 2u;
        if (bytes > UINT32_MAX || uint64_t(appendOffset) + bytes > UINT32_MAX) return 0;
        return static_cast<uint32_t>(bytes);
    }

    uint32_t ExpandSequenceLookup(fmt::M2Header* md, uint32_t appendOffset, uint32_t workSize,
                                  std::string_view name)
    {
        const uint32_t needed = LookupCapacity(md);
        if (!needed || !md || !NeedsSequenceLookupExpansion(md)) return 0;

        if (!Align4(appendOffset, appendOffset)) return 0;
        const uint64_t bytes = uint64_t(needed) * 2u;
        if (uint64_t(appendOffset) + bytes > workSize) return 0;

        auto* seqs = reinterpret_cast<fmt::M2Sequence*>(md->base() + md->sequences.offset);
        std::vector<uint16_t> table(needed, kEmptyLookup);
        if (!BuildLookup(seqs, md->sequences.count, table)) return 0;

        // Apply curated source-to-client ids only after the table is complete, while the builder can
        // still distinguish a remapped source sequence from an existing low-id sequence.
        for (uint32_t i = 0; i < md->sequences.count; ++i)
            seqs[i].id = RemappedAnimationId(seqs[i].id);

        std::memcpy(md->base() + appendOffset, table.data(), static_cast<size_t>(bytes));
        md->sequenceLookup.count  = needed;
        md->sequenceLookup.offset = appendOffset;

        if (!name.empty())
            WLOG_INFO("modern-m2: '%.*s' rebuilt sequenceLookup as %u-slot native hash table",
                      int(name.size()), name.data(), needed);
        else
            WLOG_INFO("modern-m2: rebuilt sequenceLookup as %u-slot native hash table", needed);

        return needed;
    }
}
