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

#pragma once

#include "structure/m2/M2Format.hpp"

#include <cstdint>
#include <string_view>

/**
 * @brief Masks source blend time and rebuilds/remaps sequenceLookup for the 3.3.5 client.
 *
 * sequenceLookup is not indexed directly by AnimationData id. Wow.exe hashes the id modulo the table
 * size and uses cumulative quadratic probing for collisions. Rebuilding therefore emits the first
 * prime-sized table at least twice the number of unique ids, matching Blizzard-authored character
 * models, and maps every embedded sequence id regardless of how high that id is.
 */
namespace wxl::modern::assets::m2::animations
{
    /**
     * @brief Reports whether sequenceLookup is missing, unsafe, stale after remapping, or mis-hashed.
     */
    bool NeedsSequenceLookupExpansion(const wxl::structure::m2::M2Header* md);

    /**
     * @brief Masks blendTime; curated id remaps are applied while rebuilding the lookup.
     * @param md  Model header (pre-parse, offsets model-relative).
     */
    void Fix(wxl::structure::m2::M2Header* md);

    /**
     * @brief Extra bytes needed for a native quadratic-probe hash table covering every embedded id.
     * @return 0 when the existing lookup already satisfies the native contract (or on invalid input).
     */
    uint32_t SequenceLookupExtraBytes(const wxl::structure::m2::M2Header* md,
                                      uint32_t appendOffset);

    /**
     * @brief Rebuilds sequenceLookup at appendOffset and applies curated source-id remaps.
     *
     * A remapped source sequence wins a collision with an existing low-id sequence. Otherwise
     * variation zero is preferred, followed by the lowest sequence index.
     * @return Hash-table slot count written, or 0 on failure / no-op.
     */
    uint32_t ExpandSequenceLookup(wxl::structure::m2::M2Header* md, uint32_t appendOffset,
                                  uint32_t workSize, std::string_view name = {});
}
