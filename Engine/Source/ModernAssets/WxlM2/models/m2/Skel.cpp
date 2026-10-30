// .skel merge: splice a Legion+ split-skeleton file's bones/sequences/attachments back into an MD20 image.
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

#include "Skel.hpp"

#include "Contract.hpp"
#include "core/Logger.hpp"
#include "structure/m2/M2Format.hpp"

#include <cstddef>
#include <limits>

namespace wxl::modern::assets::m2::skel
{
    namespace fmt = wxl::structure::m2;

    namespace
    {
        // Chunk names are stored in memory order (not reversed) in the whole M2 family, .skel included.
        constexpr uint32_t Magic(char a, char b, char c, char d)
        {
            return uint32_t(uint8_t(a)) | (uint32_t(uint8_t(b)) << 8) |
                   (uint32_t(uint8_t(c)) << 16) | (uint32_t(uint8_t(d)) << 24);
        }
        constexpr uint32_t kSKS1 = Magic('S', 'K', 'S', '1');
        constexpr uint32_t kSKB1 = Magic('S', 'K', 'B', '1');
        constexpr uint32_t kSKA1 = Magic('S', 'K', 'A', '1');

        // M2Header array-field offsets a .skel chunk can populate.
        constexpr uint32_t kHdrGlobalLoops      = offsetof(fmt::M2Header, globalLoops);
        constexpr uint32_t kHdrSequences        = offsetof(fmt::M2Header, sequences);
        constexpr uint32_t kHdrSequenceLookup   = offsetof(fmt::M2Header, sequenceLookup);
        constexpr uint32_t kHdrBones            = offsetof(fmt::M2Header, bones);
        constexpr uint32_t kHdrBoneLookup       = offsetof(fmt::M2Header, boneLookup);
        constexpr uint32_t kHdrAttachments      = offsetof(fmt::M2Header, attachments);
        constexpr uint32_t kHdrAttachmentLookup = offsetof(fmt::M2Header, attachmentLookup);

        bool IsArrayEmpty(const uint8_t* md20, uint32_t hdrOff) { return Rd32(md20 + hdrOff) == 0; }

        struct ArrayImport
        {
            uint32_t headerOffset;
            uint32_t count;
            uint32_t localOffset;
            uint32_t stride;
        };

        bool FitsPayload(const ArrayImport& array, uint32_t payloadSize, uint32_t dataFloor)
        {
            if (!array.count) return true;
            if (!array.localOffset || array.localOffset < dataFloor || !array.stride) return false;
            return uint64_t(array.localOffset) + uint64_t(array.count) * array.stride <= payloadSize;
        }

        // Points the M2Array at `hdrOff` at `base + localOff` with `count` entries.
        void SetArray(std::vector<uint8_t>& md20, uint32_t hdrOff, uint32_t count, uint32_t base, uint32_t localOff)
        {
            Wr32(md20.data() + hdrOff,     count);
            Wr32(md20.data() + hdrOff + 4, base + localOff);
        }

        // Appends a source payload once, then imports only arrays still absent from the MD20 header.
        // Returning false means an array that would have been imported was malformed.
        template <size_t N>
        bool ImportPayload(std::vector<uint8_t>& md20, const uint8_t* payload, uint32_t payloadSize,
                           uint32_t dataFloor, const ArrayImport (&arrays)[N],
                           uint32_t maxMergedBytes, bool& touched)
        {
            bool needsPayload = false;
            for (const auto& array : arrays)
            {
                if (!IsArrayEmpty(md20.data(), array.headerOffset) || !array.count) continue;
                if (!FitsPayload(array, payloadSize, dataFloor)) return false;
                needsPayload = true;
            }
            if (!needsPayload) return true;

            if (md20.size() > maxMergedBytes || payloadSize > maxMergedBytes - md20.size())
                return false;

            const uint32_t base = static_cast<uint32_t>(md20.size());
            md20.insert(md20.end(), payload, payload + payloadSize);
            for (const auto& array : arrays)
            {
                if (!IsArrayEmpty(md20.data(), array.headerOffset) || !array.count) continue;
                SetArray(md20, array.headerOffset, array.count, base, array.localOffset);
            }
            touched = true;
            return true;
        }
    }

    bool Merge(std::span<const uint8_t> skelFile, std::vector<uint8_t>& md20,
               uint32_t maxMergedBytes)
    {
        if (md20.size() < sizeof(fmt::M2Header) ||
            md20.size() > std::numeric_limits<uint32_t>::max() ||
            md20.size() > maxMergedBytes ||
            skelFile.size() > std::numeric_limits<uint32_t>::max() ||
            Rd32(md20.data()) != fmt::kMagicMD20)
            return false;

        const uint8_t* b = skelFile.data();
        const size_t n = skelFile.size();
        std::vector<uint8_t> merged = md20;
        bool touched = false;

        for (size_t o = 0; o < n; )
        {
            if (n - o < 8) return false;

            const uint32_t tag = Rd32(b + o);
            const uint32_t sz  = Rd32(b + o + 4);
            if (static_cast<size_t>(sz) > n - o - 8) return false;
            const uint8_t* payload = b + o + 8;

            // SKS1 header (32 bytes): global_loops M2Array@0, sequences M2Array@8, sequence_lookups M2Array@16.
            if (tag == kSKS1)
            {
                if (sz < 32) return false;
                const ArrayImport arrays[] = {
                    { kHdrGlobalLoops,    Rd32(payload +  0), Rd32(payload +  4), 4 },
                    { kHdrSequences,      Rd32(payload +  8), Rd32(payload + 12), sizeof(fmt::M2Sequence) },
                    { kHdrSequenceLookup, Rd32(payload + 16), Rd32(payload + 20), sizeof(int16_t) }
                };
                if (!ImportPayload(merged, payload, sz, 32, arrays, maxMergedBytes, touched)) return false;
            }
            // SKB1 header (16 bytes): bones M2Array@0, key_bone_lookup M2Array@8.
            else if (tag == kSKB1)
            {
                if (sz < 16) return false;
                const ArrayImport arrays[] = {
                    { kHdrBones,      Rd32(payload + 0), Rd32(payload +  4), sizeof(fmt::M2CompBone) },
                    { kHdrBoneLookup, Rd32(payload + 8), Rd32(payload + 12), sizeof(uint16_t) }
                };
                if (!ImportPayload(merged, payload, sz, 16, arrays, maxMergedBytes, touched)) return false;
            }
            // SKA1 header (16 bytes): attachments M2Array@0, attachment_lookup_table M2Array@8.
            else if (tag == kSKA1)
            {
                if (sz < 16) return false;
                constexpr uint32_t kAttachmentStride = 0x28;
                const ArrayImport arrays[] = {
                    { kHdrAttachments,      Rd32(payload + 0), Rd32(payload +  4), kAttachmentStride },
                    { kHdrAttachmentLookup, Rd32(payload + 8), Rd32(payload + 12), sizeof(uint16_t) }
                };
                if (!ImportPayload(merged, payload, sz, 16, arrays, maxMergedBytes, touched)) return false;
            }

            o += 8 + static_cast<size_t>(sz);
        }

        if (touched)
        {
            md20.swap(merged);
            wxl::core::log::Printf("modern-m2 skel: merged (md20 now %zu bytes)", md20.size());
        }
        return touched;
    }
}
