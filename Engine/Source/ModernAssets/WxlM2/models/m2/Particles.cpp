// Compacts the source particle emitter and scopes the source alpha-key cutoff at draw time.
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

#include "Particles.hpp"

#include "Contract.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#ifndef WXL_HOST
#include "game/m2/M2.hpp" // PushAlphaRef: live-engine, DLL-only draw path
#endif

namespace wxl::modern::assets::m2::particles
{
    namespace fmt = wxl::structure::m2;

    namespace
    {
        constexpr uint32_t kStrideClient = 0x1DC;
        constexpr uint32_t kStrideSource = 0x1EC;
        constexpr uint32_t kTextureIdOff = 0x16;
        constexpr uint32_t kFlagMultiTexture = 0x10000000;
        constexpr uint16_t kPrimaryTextureMask = 0x1F;
        constexpr uint32_t kBlendOff = 0x28;
        constexpr uint32_t kTexRowsOff = 0x30;
        constexpr uint32_t kTexColsOff = 0x32;
        constexpr uint32_t kHeadCellOff = 0x13C;
        constexpr uint32_t kTailCellOff = 0x14C;

        constexpr uint32_t kFlagCompressedGravity = 0x800000;
        constexpr uint32_t kGravityTrackOff = 0x84;
        constexpr uint32_t kGravityValCountOff = 0x90;
        constexpr uint32_t kGravityValOfsOff = 0x94;
        constexpr float kGravityMagUnit = 0.04238648f;
        constexpr float kSourceAlphaKeyRef = 0.5f;

        struct Plan
        {
            std::vector<uint8_t> emitters;
            std::vector<uint8_t> tail;
            uint32_t appendOffset = 0;
        };

        uint32_t HeaderBytes(fmt::M2Header const* md)
        {
            return md->globalFlags & fmt::kFlagUseTextureCombinerCombos
                ? sizeof(fmt::M2Header) : offsetof(fmt::M2Header, textureCombinerCombos);
        }

        bool Fits(uint32_t offset, uint32_t count, uint32_t stride, uint32_t size)
        {
            return !count || (offset && uint64_t(offset) + uint64_t(count) * stride <= size);
        }

        struct RecordSpan
        {
            fmt::M2Array array;
            uint32_t stride;
        };

        std::array<RecordSpan, 14> Records(fmt::M2Header const* md)
        {
            return {{{md->sequences, sizeof(fmt::M2Sequence)}, {md->bones, sizeof(fmt::M2CompBone)},
                     {md->vertices, 48}, {md->colors, 40}, {md->textures, sizeof(fmt::M2Texture)},
                     {md->textureWeights, 20}, {md->textureTransforms, 60}, {md->materials, 4},
                     {md->attachments, 40}, {md->events, 36}, {md->lights, 156}, {md->cameras, 0x74},
                     {md->ribbonEmitters, sizeof(fmt::M2Ribbon)}, {md->particleEmitters, kStrideSource}}};
        }

        bool Intersects(uint64_t begin, uint64_t end, RecordSpan const& span)
        {
            return span.array.count && begin < uint64_t(span.array.offset) +
                uint64_t(span.array.count) * span.stride && span.array.offset < end;
        }

        bool ValidateRecords(fmt::M2Header const* md, uint32_t size)
        {
            auto const records = Records(md);
            for (size_t index = 0; index < records.size(); ++index)
            {
                auto const& span = records[index];
                if (!span.array.count) continue;
                if (span.array.offset < HeaderBytes(md) ||
                    !Fits(span.array.offset, span.array.count, span.stride, size)) return false;
                uint64_t const end = uint64_t(span.array.offset) + uint64_t(span.array.count) * span.stride;
                for (size_t other = index + 1; other < records.size(); ++other)
                    if (Intersects(span.array.offset, end, records[other])) return false;
            }
            return true;
        }

        bool Readable(fmt::M2Header const* md, uint32_t offset, uint32_t count,
                      uint32_t stride, uint32_t size)
        {
            if (!count) return true;
            if (!Fits(offset, count, stride, size) || offset < HeaderBytes(md)) return false;
            // Other themes compact cameras/records before or after this stage. They cannot own a particle leaf.
            uint64_t const end = uint64_t(offset) + uint64_t(count) * stride;
            for (auto const& span : Records(md))
                if (Intersects(offset, end, span)) return false;
            return true;
        }

        bool Append(Plan& plan, uint8_t const* source, uint32_t bytes, uint32_t alignment, uint32_t& offset)
        {
            uint64_t const begin = (uint64_t(plan.appendOffset) + plan.tail.size() + alignment - 1) &
                                   ~uint64_t(alignment - 1);
            uint64_t const end = begin + bytes;
            if (end > UINT32_MAX || end - plan.appendOffset > kMaximumConversionBytes) return false;
            size_t const relative = size_t(begin - plan.appendOffset);
            plan.tail.resize(size_t(end - plan.appendOffset), 0);
            if (bytes) std::memcpy(plan.tail.data() + relative, source, bytes);
            offset = uint32_t(begin);
            return true;
        }

        bool Atlas(fmt::M2Header const* md, uint32_t size, uint8_t const* source,
                   uint8_t* destination, uint32_t track, uint32_t cells, Plan& plan)
        {
            uint32_t const count = Rd32(source + track + 8);
            uint32_t const offset = Rd32(source + track + 12);
            if (!count) return true;
            if (count > 0x1000 || !Readable(md, offset, count, 2, size)) return false;
            if (cells <= 1) return true;

            bool changed = false;
            std::vector<uint8_t> keys(count * 2);
            std::memcpy(keys.data(), md->base() + offset, keys.size());
            for (uint32_t index = 0; index < count; ++index)
            {
                int16_t key;
                std::memcpy(&key, keys.data() + index * 2, 2);
                if (key >= int32_t(cells))
                {
                    key = int16_t(key % cells);
                    std::memcpy(keys.data() + index * 2, &key, 2);
                    changed = true;
                }
            }
            if (!changed) return true;
            uint32_t privateOffset = 0;
            if (!Append(plan, keys.data(), uint32_t(keys.size()), 2, privateOffset)) return false;
            Wr32(destination + track + 12, privateOffset);
            return true;
        }

        bool Gravity(fmt::M2Header const* md, uint32_t size, uint8_t const* source,
                     uint8_t* destination, Plan& plan)
        {
            uint32_t const flags = Rd32(source + 4);
            if (!(flags & kFlagCompressedGravity)) return true;
            int16_t const globalSequence = int16_t(Rd16(source + kGravityTrackOff + 2));
            uint32_t const outerCount = Rd32(source + kGravityValCountOff);
            uint32_t const outerOffset = Rd32(source + kGravityValOfsOff);
            if (outerCount)
            {
                if (outerCount > 0x1000 || !Readable(md, outerOffset, outerCount, 8, size)) return false;
                uint32_t privateOuter = 0;
                if (!Append(plan, md->base() + outerOffset, outerCount * 8, 4, privateOuter)) return false;
                for (uint32_t animation = 0; animation < outerCount; ++animation)
                {
                    uint8_t const* descriptor = md->base() + outerOffset + animation * 8;
                    uint32_t const count = Rd32(descriptor);
                    uint32_t const offset = Rd32(descriptor + 4);
                    if (!count) continue;
                    if (count > 0x1000 || !Readable(md, offset, count, 4, size)) return false;
                    if (globalSequence < 0 &&
                        (animation >= md->sequences.count ||
                         !(Rd32(md->base() + md->sequences.offset +
                                animation * sizeof(fmt::M2Sequence) + 12) & 0x20)))
                        return false; // Referenced external keys are not part of this model image.

                    std::vector<uint8_t> converted(count * 4);
                    for (uint32_t index = 0; index < count; ++index)
                    {
                        uint8_t const* key = md->base() + offset + index * 4;
                        float const dx = int8_t(key[0]) / 128.0f;
                        float const dy = int8_t(key[1]) / 128.0f;
                        int16_t zraw;
                        std::memcpy(&zraw, key + 2, 2);
                        float const planar = dx * dx + dy * dy;
                        float zc = std::sqrt(planar < 1.0f ? 1.0f - planar : 0.0f);
                        float magnitude = zraw * kGravityMagUnit;
                        if (magnitude < 0.0f)
                        {
                            zc = -zc;
                            magnitude = -magnitude;
                        }
                        // Retain current WXL's scalar compatibility result, including its horizontal approximation.
                        float const scalar = -(zc * magnitude);
                        std::memcpy(converted.data() + index * 4, &scalar, 4);
                    }
                    uint32_t privateKeys = 0;
                    if (!Append(plan, converted.data(), uint32_t(converted.size()), 4, privateKeys)) return false;
                    Wr32(plan.tail.data() + privateOuter - plan.appendOffset + animation * 8 + 4, privateKeys);
                }
                Wr32(destination + kGravityValOfsOff, privateOuter);
            }
            Wr32(destination + 4, flags & ~kFlagCompressedGravity);
            return true;
        }

        bool Build(fmt::M2Header const* md, uint32_t size, uint32_t appendOffset, Plan& plan)
        {
            if (!md || size < offsetof(fmt::M2Header, textureCombinerCombos) || appendOffset < size) return false;
            if (size < HeaderBytes(md)) return false;
            plan.appendOffset = appendOffset;
            if (!md->particleEmitters.count) return true;
            if (!ValidateRecords(md, size)) return false;

            plan.emitters.resize(size_t(md->particleEmitters.count) * kStrideClient);
            for (uint32_t index = 0; index < md->particleEmitters.count; ++index)
            {
                uint8_t const* source = md->base() + md->particleEmitters.offset + size_t(index) * kStrideSource;
                uint8_t* destination = plan.emitters.data() + size_t(index) * kStrideClient;
                std::memcpy(destination, source, kStrideClient);

                // The native renderer has one particle texture. Modern multi-texture emitters pack three
                // five-bit indices; retain their primary atlas rather than sampling the model's diffuse.
                // Native random-atlas bit 0x10000 is independent and still uses the complete texture WORD.
                uint16_t texture = Rd16(source + kTextureIdOff);
                if (Rd32(source + 4) & kFlagMultiTexture) texture &= kPrimaryTextureMask;
                if (md->textures.count && texture >= md->textures.count) texture = 0;
                Wr16(destination + kTextureIdOff, texture);

                uint8_t& blend = destination[kBlendOff];
                if (blend == 7) blend = 3;
                else if (blend > 7) blend = 4;

                uint32_t const cells = uint32_t(Rd16(source + kTexRowsOff)) * Rd16(source + kTexColsOff);
                if (cells > uint32_t(INT16_MAX) ||
                    !Atlas(md, size, source, destination, kHeadCellOff, cells, plan) ||
                    !Atlas(md, size, source, destination, kTailCellOff, cells, plan) ||
                    !Gravity(md, size, source, destination, plan))
                    return false;
            }
            return true;
        }
    }

    bool WorkSize(fmt::M2Header const* md, uint32_t fileSize, uint32_t appendOffset, uint32_t& requiredSize)
    {
        Plan plan;
        if (!Build(md, fileSize, appendOffset, plan)) return false;
        requiredSize = appendOffset + uint32_t(plan.tail.size());
        return true;
    }

    bool Compact(fmt::M2Header* md, uint32_t fileSize, uint32_t workSize, uint32_t appendOffset)
    {
        Plan plan;
        if (!Build(md, fileSize, appendOffset, plan) ||
            uint64_t(appendOffset) + plan.tail.size() > workSize)
            return false;
        // Validation and all original reads finish before any shared body byte is changed.
        if (!plan.emitters.empty())
            std::memcpy(md->base() + md->particleEmitters.offset, plan.emitters.data(), plan.emitters.size());
        if (!plan.tail.empty())
            std::memcpy(md->base() + appendOffset, plan.tail.data(), plan.tail.size());
        return true;
    }

#ifndef WXL_HOST
    /**
     * @brief Lowers the alpha-key cutoff to the source coverage midpoint for an alpha-key batch of a
     *        downported model.
     * @param a           Batch alpha/material setup arguments.
     * @param downported  True if the model was reshaped by this module.
     */
    void OnSetupBatchAlpha(wxl::events::M2SetupBatchAlphaArgs const& a, bool downported)
    {
        if (downported && a.blendMode == kBlendAlphaKey)
            wxl::game::m2::PushAlphaRef(kSourceAlphaKeyRef);
    }
#endif
}
