// Reshapes a source MD20 image onto the client M2 contract before the native parser reads it.
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

#include "Downport.hpp"

#include "common/Config.hpp"
#include "core/Logger.hpp"
#include "structure/m2/M2Format.hpp"

#include "Animations.hpp"
#include "Cameras.hpp"
#include "Contract.hpp"
#include "Particles.hpp"
#include "Ribbons.hpp"
#include "Textures.hpp"

#include "../../common/Text.hpp"

#include <limits>
#include <string_view>

namespace wxl::modern::assets::m2::downport
{
    namespace fmt = wxl::structure::m2;
    namespace text = wxl::modern::assets::common::text;

    namespace
    {
        // In the client runtime these bits are not passive model features: CM2Shared's destructor
        // treats them as ownership markers and calls SMemFree on the pointer-fixed arrays at
        // header + 0x84 (textureCombos) and header + 0x9C (textureTransformCombos).  A downported
        // image keeps those arrays inside its single file buffer, so carrying the modern meanings
        // of these bits into v264 makes the client free an interior pointer while collecting the
        // model.  Preserve the compatible flags (notably 0x8 texture-combiner combos), but never
        // advertise these two native ownership contracts for a self-contained transformed image.
        constexpr uint32_t kClientRuntimeOwnedArrayFlags = 0x20u | 0x40u;
        // Retail's 0x2000 bit selects chunked animation files; 0x200000 marks the upgraded/chunked
        // model layout. By the time this source-version downport runs, MD21 and .skel data have been
        // merged into one self-contained MD20 image. Neither source storage contract is valid in the
        // final 3.3.5 model (notably when a static equipment model becomes a spell missile).
        constexpr uint32_t kSourceChunkedAnimationFlag = 0x2000u;
        constexpr uint32_t kSourceChunkedModelFlag = 0x200000u;
        constexpr uint32_t kMaxSequenceLookupEntries = 4096u;

        bool Fits(const fmt::M2Array& array, uint32_t stride, uint32_t size)
        {
            if (!array.count) return true;
            if (!array.offset || !stride) return false;
            return uint64_t(array.offset) + uint64_t(array.count) * stride <= size;
        }

        bool AddChecked(uint32_t left, uint32_t right, uint32_t& out)
        {
            const uint64_t sum = uint64_t(left) + uint64_t(right);
            if (sum > std::numeric_limits<uint32_t>::max()) return false;
            out = static_cast<uint32_t>(sum);
            return true;
        }

        /**
         * @brief Validates every source array touched by the downport before any in-place mutation.
         */
        bool ValidateSource(const fmt::M2Header* md, uint32_t size)
        {
            if (!md || size < sizeof(fmt::M2Header)) return false;
            if (md->numSkinProfiles > 32 ||
                md->globalLoops.count > 0xFFFEu ||
                md->sequences.count > 0x7FFFu ||
                md->sequenceLookup.count > kMaxSequenceLookupEntries)
                return false;

            if (!Fits(md->globalLoops, 4, size) ||
                !Fits(md->sequences, sizeof(fmt::M2Sequence), size) ||
                !Fits(md->sequenceLookup, sizeof(int16_t), size) ||
                !Fits(md->bones, sizeof(fmt::M2CompBone), size) ||
                !Fits(md->boneLookup, sizeof(uint16_t), size) ||
                !Fits(md->textures, sizeof(fmt::M2Texture), size) ||
                !Fits(md->materials, 4, size) ||
                !Fits(md->textureTransforms, 0x3C, size) ||
                !Fits(md->textureUnitLookup, sizeof(int16_t), size) ||
                !Fits(md->cameras, 0x74, size) ||
                !Fits(md->ribbonEmitters, sizeof(fmt::M2Ribbon), size) ||
                !Fits(md->particleEmitters, 0x1EC, size))
                return false;

            // Ribbon compaction walks these nested index arrays after moving each emitter. Validate them
            // here so a wrapped count cannot turn its later clamp loop into an out-of-bounds walk.
            if (md->ribbonEmitters.count)
            {
                const auto* ribbons = reinterpret_cast<const fmt::M2Ribbon*>(
                    md->base() + md->ribbonEmitters.offset);
                for (uint32_t i = 0; i < md->ribbonEmitters.count; ++i)
                    if (!Fits(ribbons[i].textureIndices, sizeof(uint16_t), size) ||
                        !Fits(ribbons[i].materialIndices, sizeof(uint16_t), size))
                        return false;
            }

            // Particle atlas dimensions are multiplied as an int16 cell count by the compactor. Keep the
            // product in that positive range instead of allowing integer overflow on hostile input.
            if (md->particleEmitters.count)
            {
                const uint8_t* particles = md->base() + md->particleEmitters.offset;
                for (uint32_t i = 0; i < md->particleEmitters.count; ++i)
                {
                    const uint8_t* particle = particles + uint64_t(i) * 0x1ECu;
                    const uint64_t cells = uint64_t(Rd16(particle + 0x30)) *
                                           uint64_t(Rd16(particle + 0x32));
                    if (cells > static_cast<uint64_t>(std::numeric_limits<int16_t>::max()))
                        return false;
                }
            }
            return true;
        }

        bool RepairsTextureLoops(std::string_view name)
        {
            // The compatibility issue was observed on attached retail equipment effects. Keep the
            // pre-parse rewrite scoped to that asset family: rewriting character or creature clocks
            // can change animation semantics that are unrelated to equipment texture transforms.
            static const bool enabled = wxl::config::Env("WXL_COLLECTION_TEXTURE_ANIMATION_LOOPS", true);
            return enabled && text::StartsWithCI(name, "item\\objectcomponents\\");
        }
    }

    /**
     * @brief Reports whether a buffer is an MD20 image in the source version range this module reshapes.
     * @param buffer  Model bytes.
     * @param size    Buffer length.
     * @return True if the image is in the source range.
     */
    bool IsConvertible(const void* buffer, uint32_t size)
    {
        if (!buffer || size < sizeof(fmt::M2Header)) return false;
        const auto* md = static_cast<const fmt::M2Header*>(buffer);
        if (md->magic != fmt::kMagicMD20) return false; // chunked container or not an .m2
        return md->version >= kSourceVersionMin && md->version <= kSourceVersionMax;
    }

    /**
     * @brief Logs a model's header (version, flags, counts).
     * @param buffer  Model bytes.
     * @param size    Buffer length.
     */
    void Inspect(const void* buffer, uint32_t size)
    {
        if (!buffer || size < sizeof(fmt::M2Header)) return;
        const auto* md = static_cast<const fmt::M2Header*>(buffer);
        if (md->magic != fmt::kMagicMD20)
        {
            const uint8_t* p = static_cast<const uint8_t*>(buffer);
            WLOG_INFO("modern-m2: non-MD20 image (magic %02X%02X%02X%02X), skipped", p[0], p[1], p[2], p[3]);
            return;
        }
        WLOG_INFO("modern-m2: MD20 v=%u size=%u flags=0x%X | seq=%u bones=%u verts=%u skins=%u colors=%u tex=%u mat=%u cam=%u ribbon=%u particle=%u",
                  md->version, size, md->globalFlags,
                  md->sequences.count, md->bones.count, md->vertices.count, md->numSkinProfiles,
                  md->colors.count, md->textures.count, md->materials.count,
                  md->cameras.count, md->ribbonEmitters.count, md->particleEmitters.count);
    }

    /**
     * @brief Computes the byte count the reshaped image needs.
     * @param buffer  Source model bytes.
     * @param size    Source length.
     * @return Required size including appended compatibility tables, or zero for malformed source data.
     */
    uint32_t WorkSize(const void* buffer, uint32_t size, std::string_view name)
    {
        if (!IsConvertible(buffer, size)) return size;
        const auto* md = static_cast<const fmt::M2Header*>(buffer);
        if (!ValidateSource(md, size)) return 0;

        uint32_t workSize = size;
        if (textures::NeedsCoordCombos(md) && !AddChecked(workSize, 2, workSize))
            return 0;

        if (RepairsTextureLoops(name))
        {
            // Texture repair aligns its appended array to four bytes.
            if (workSize > std::numeric_limits<uint32_t>::max() - 3u) return 0;
            const uint32_t loopExtra =
                textures::TextureTransformLoopRepairExtraBytes(md, size, workSize, name);
            if (!AddChecked(workSize, loopExtra, workSize)) return 0;
        }

        uint32_t particleSize = 0;
        if (!particles::WorkSize(md, size, workSize, particleSize)) return 0;
        workSize = particleSize;

        const bool lookupGrow = animations::NeedsSequenceLookupExpansion(md);
        const uint32_t lookupExtra = animations::SequenceLookupExtraBytes(md, workSize);
        if (lookupGrow && !lookupExtra) return 0;
        if (!AddChecked(workSize, lookupExtra, workSize)) return 0;
        return workSize;
    }

    /**
     * @brief Reshapes an already-copied image in place onto the client contract.
     * @param work      Work buffer holding the copied source bytes.
     * @param origSize  Length of the copied source.
     * @param workSize  Total work-buffer length.
     * @return True on success; false for malformed/unsupported input or an undersized work buffer.
     */
    bool ProcessInPlace(void* work, uint32_t origSize, uint32_t workSize, std::string_view name)
    {
        if (!work || workSize < origSize) return false;

        // work holds the copied source bytes; gate on them (the version is rewritten to the client
        // value at the end, so a second pass over the same buffer is a no-op).
        auto* md = reinterpret_cast<fmt::M2Header*>(work);
        if (md->magic != fmt::kMagicMD20) return false;
        if (md->version < kSourceVersionMin || md->version > kSourceVersionMax) return false;
        if (!ValidateSource(md, origSize)) return false;

        const bool grow = textures::NeedsCoordCombos(md);
        uint32_t appendOffset = origSize;
        if (grow && !AddChecked(appendOffset, 2, appendOffset)) return false;

        if (RepairsTextureLoops(name) &&
            appendOffset > std::numeric_limits<uint32_t>::max() - 3u)
            return false;
        const uint32_t loopExtra = RepairsTextureLoops(name)
            ? textures::TextureTransformLoopRepairExtraBytes(md, origSize, appendOffset, name) : 0;
        uint32_t particleOffset = 0;
        if (!AddChecked(appendOffset, loopExtra, particleOffset)) return false;
        uint32_t lookupOffset = 0;
        if (!particles::WorkSize(md, origSize, particleOffset, lookupOffset)) return false;

        const bool lookupGrow = animations::NeedsSequenceLookupExpansion(md);
        const uint32_t lookupExtra = animations::SequenceLookupExtraBytes(md, lookupOffset);
        if (lookupGrow && !lookupExtra) return false;

        uint32_t requiredSize = 0;
        if (!AddChecked(lookupOffset, lookupExtra, requiredSize) || workSize < requiredSize)
            return false; // caller under-sized the buffer

        // Each theme compacts within the original body size (the body keeps its byte positions).
        cameras::Compact(md);
        if (!particles::Compact(md, origSize, workSize, particleOffset))
            return false;
        ribbons::Compact(md, origSize);
        animations::Fix(md);

        if (grow)
            textures::SynthCoordCombos(md, origSize); // the appended entry lives at the original tail

        if (RepairsTextureLoops(name))
        {
            const uint32_t repaired = textures::RepairTextureTransformLoops(md, origSize, appendOffset, workSize, name);
            if (repaired)
                WLOG_INFO("modern-m2: '%.*s' repaired %u pre-parse texture-transform loop(s)",
                          int(name.size()), name.data(), repaired);
            if (loopExtra && !repaired) return false;
        }

        appendOffset = lookupOffset;

        // Rebuild sequenceLookup using the client's native quadratic-probe hash contract. The rebuild
        // applies curated id remaps and safely handles embedded ids above the table's slot count.
        if (animations::NeedsSequenceLookupExpansion(md) &&
            !animations::ExpandSequenceLookup(md, appendOffset, workSize, name))
            return false;

        md->globalFlags &= ~(kClientRuntimeOwnedArrayFlags | kSourceChunkedAnimationFlag |
                             kSourceChunkedModelFlag);

        // Stage, do not finalize: the body now matches the client contract, but the version carries the
        // staging bit so the DLL recognizes a compacted image and finalizes it (the client accepts only the
        // exact native version, so a staged image is inert until the DLL clears the bit).
        md->version = fmt::kClientVersion | kStagedVersionBit;
        return true;
    }
}
