// WMO translate: read a source WMO, emit Client-shaped WMO bytes.
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

#include "WmoTranslate.hpp"

#include "WmoChunks.hpp"
#include "../../common/Chunk.hpp"
#include "core/Logger.hpp"

#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>

namespace wxl::modern::assets::wmo
{
    namespace iff = wxl::modern::assets::common::iff;

    namespace
    {
        using iff::Rd16;
        using iff::Rd32;
        using iff::Rdf;
        using iff::Wr16;
        using iff::Wr32;

        // Source shader id -> the Client's EXTERIOR effect table index (RE'd in wmo_material_shaders.md PART
        // A/C against the 12340 decompile: the Client indexes s_wmoShaderMetaData/the effect tables directly
        // by this field, unclamped -- an id past the table reads a garbage handle and faults the group draw,
        // and the exterior table's own slot 6 is null (only the interior table has a two-layer composite
        // effect). A single MOMT record has no interior/exterior context of its own (the same material can be
        // drawn by both), so every id below targets what is safe for an EXTERIOR batch, the one choice that
        // is never wrong regardless of which group ends up drawing it. 0..5 are already native; the rest
        // picks the nearest trait the exterior table can express: env reflection (3/5/7/11/12/17) keeps
        // texture_1/texture_2 as-is (verified against served WMO data with _scratch/wmo_dump.py -- texture_2
        // already holds a usable second texture for this family, no slot promotion needed), everything else
        // falls to the plain diffuse/opaque base layer already sitting in texture_1. Index 23 (UnkDFShader)
        // is additionally special-cased by the caller (its base texture is promoted into texture_1 first).
        constexpr uint8_t kShaderRemapTable[24] = {
            0, 1, 2, 3, 4, 5,   // 0-5  native, unchanged
            0,                  // 6  TwoLayerDiffuse        -> exterior has no composite slot
            5,                  // 7  TwoLayerEnvMetal        -> env+metal
            0,                  // 8  TwoLayerTerrain         -> base layer
            0,                  // 9  DiffuseEmissive         -> base layer
            4,                  // 10 waterWindow (FFX)       -> opaque
            5,                  // 11 MaskedEnvMetal          -> env+metal
            5,                  // 12 EnvMetalEmissive        -> env+metal
            4,                  // 13 TwoLayerDiffuseOpaque   -> opaque base
            4,                  // 14 submarineWindow (FFX)   -> opaque
            0,                  // 15 TwoLayerDiffuseEmissive -> base layer
            0,                  // 16 DiffuseTerrain          -> base layer
            5,                  // 17 AdditiveMaskedEnvMetal  -> env+metal
            0,                  // 18 TwoLayerDiffuseMod2x    -> base layer
            0,                  // 19 TwoLayerDiffuseMod2xNA  -> base layer
            0,                  // 20 TwoLayerDiffuseAlpha    -> base layer
            0,                  // 21 Lod                     -> base layer
            0,                  // 22 Parallax                -> base layer
            0,                  // 23 UnkDFShader             -> base layer
        };

        uint32_t RemapShader(uint32_t shader)
        {
            return (shader < std::size(kShaderRemapTable)) ? kShaderRemapTable[shader] : 0;
        }

        // Group liquid id -> Client. The Client indexes LiquidType with this value directly (no arithmetic),
        // then reads that row's material without a null guard, so an id that resolves a LiquidType row but
        // has no Client material faults the liquid pass. Keep the Client-native ids (1..21), fold known
        // modern ids onto the matching Client category, and send anything else to still water. Mirrors the
        // terrain liquid remap (same id space).
        uint32_t MapWmoLiquidType(uint32_t id)
        {
            switch (id)
            {
                case 101: case 321: case 324: case 350: case 412:
                case 868: case 890: case 891: case 896:
                    return 2;  // ocean
                case 121: case 141: case 302: case 303: case 397: case 404:
                case 671: case 739: case 859: case 860: case 869: case 870:
                case 873: case 874: case 875: case 876: case 877: case 878: case 879:
                    return 7;  // lava
                case 586:
                    return 4;  // slime
                default:
                    break;
            }
            return (id >= 1 && id <= 21) ? id : 5u;  // water
        }

        // Growable MOTX blob: appends NUL-terminated texture names, dedups, returns the byte offset.
        struct MotxBuilder
        {
            static constexpr uint32_t kNotFound = std::numeric_limits<uint32_t>::max();

            std::vector<uint8_t> data;

            void Seed(const uint8_t* blob, uint32_t len) { data.assign(blob, blob + len); }

            bool IsValid() const
            {
                size_t i = 0;
                while (i < data.size())
                {
                    const void* end = std::memchr(data.data() + i, 0, data.size() - i);
                    if (!end)
                        return false;
                    i = static_cast<const uint8_t*>(end) - data.data() + 1;
                }
                return true;
            }

            bool IsStringOffset(uint32_t off) const
            {
                size_t i = 0;
                while (i < data.size())
                {
                    if (i == off)
                        return std::memchr(data.data() + i, 0, data.size() - i) != nullptr;

                    const void* end = std::memchr(data.data() + i, 0, data.size() - i);
                    if (!end)
                        return false;
                    i = static_cast<const uint8_t*>(end) - data.data() + 1;
                }
                return false;
            }

            uint32_t Find(std::string_view path) const
            {
                size_t i = 0;
                while (i < data.size())
                {
                    const void* endPtr = std::memchr(data.data() + i, 0, data.size() - i);
                    if (!endPtr)
                        return kNotFound;

                    const size_t end = static_cast<const uint8_t*>(endPtr) - data.data();
                    const size_t len = end - i;
                    if (len == path.size() &&
                        (len == 0 || std::memcmp(data.data() + i, path.data(), len) == 0))
                        return static_cast<uint32_t>(i);
                    i = end + 1;
                }
                return kNotFound;
            }

            uint32_t Append(std::string_view path)
            {
                if (path.find('\0') != std::string_view::npos)
                    return kNotFound;

                const uint32_t found = Find(path);
                if (found != kNotFound)
                    return found;

                if (path.size() >= std::numeric_limits<uint32_t>::max() ||
                    data.size() > std::numeric_limits<uint32_t>::max() - path.size() - 1)
                    return kNotFound;

                const uint32_t off = static_cast<uint32_t>(data.size());
                data.insert(data.end(), path.begin(), path.end());
                data.push_back(0);
                return off;
            }
        };

        // Recompute one MOBA i16 bounding box over its vertex range, reading C3Vector from MOVT.
        void FixMobaBox(uint8_t* entry, const uint8_t* movtData, uint32_t movtVerts, uint16_t start, uint16_t end)
        {
            float mn[3] = { 3.4e38f, 3.4e38f, 3.4e38f };
            float mx[3] = { -3.4e38f, -3.4e38f, -3.4e38f };
            bool any = false;

            for (uint32_t i = start; i <= end; ++i)
            {
                if (i >= movtVerts)
                    break;
                const uint8_t* v = movtData + kMovtStride * i;
                const float coord[3] = { Rdf(v), Rdf(v + 4), Rdf(v + 8) };
                if (!std::isfinite(coord[0]) || !std::isfinite(coord[1]) ||
                    !std::isfinite(coord[2]))
                    continue;

                for (int k = 0; k < 3; ++k)
                {
                    const float c = coord[k];
                    if (c < mn[k]) mn[k] = c;
                    if (c > mx[k]) mx[k] = c;
                }
                any = true;
            }

            if (!any)
                return;

            for (int k = 0; k < 3; ++k)
            {
                const double lo = std::floor(mn[k]);
                const double hi = std::ceil(mx[k]);
                const double minI16 = std::numeric_limits<int16_t>::min();
                const double maxI16 = std::numeric_limits<int16_t>::max();
                const int16_t clampedLo = static_cast<int16_t>(
                    lo < minI16 ? minI16 : (lo > maxI16 ? maxI16 : lo));
                const int16_t clampedHi = static_cast<int16_t>(
                    hi < minI16 ? minI16 : (hi > maxI16 ? maxI16 : hi));
                Wr16(entry + kMobaBBoxOffset + 2 * k, clampedLo);
                Wr16(entry + kMobaBBoxOffset + 2 * (k + 3), clampedHi);
            }
        }

        // A source no longer maintaining unknown_box at export zeroes all six fields (the relocation flag's
        // material_id_large also overlaps its last two bytes). A zeroed box culls the batch as if it sat at
        // the WMO's local origin instead of its real position; a batch genuinely that small at the origin is
        // not a realistic case, so zero is unambiguously "unmaintained", not "authored".
        bool IsMobaBoxZero(const uint8_t* entry)
        {
            for (uint32_t i = 0; i < 6; ++i)
                if (Rd16(entry + kMobaBBoxOffset + 2 * i) != 0)
                    return false;
            return true;
        }

        // MOBA material-id relocation: source builds put the id at +0x0A behind a flag at +0x16; the Client
        // reads +0x17. Move it, clear the flag, rebuild the bbox. A batch whose bbox already reads zero gets
        // the same bbox rebuild even without the relocation flag, since a source can leave unknown_box
        // unmaintained independently of using the large material id. Returns the count relocated.
        uint32_t FixMobaChunk(uint8_t* mobaData, uint32_t mobaLen, const uint8_t* movtData, uint32_t movtLen)
        {
            const uint32_t n         = mobaLen / kMobaEntryStride;
            const uint32_t movtVerts = movtLen / kMovtStride;
            uint32_t relocated = 0;

            for (uint32_t i = 0; i < n; ++i)
            {
                uint8_t* e = mobaData + i * kMobaEntryStride;
                const bool reloc = (e[kMobaFlagOffset] & kMobaRelocFlag) != 0;
                if (reloc)
                {
                    e[kMobaMatIdOffset] = e[kMobaModernMatOff];
                    e[kMobaFlagOffset] =
                        static_cast<uint8_t>(e[kMobaFlagOffset] & ~kMobaRelocFlag);
                    ++relocated;
                }

                if (reloc || IsMobaBoxZero(e))
                {
                    const uint16_t start = Rd16(e + kMobaMinIndexOff);
                    const uint16_t end   = Rd16(e + kMobaMaxIndexOff);
                    FixMobaBox(e, movtData, movtVerts, start, end);
                }
            }
            return relocated;
        }
    }

    bool TranslateWmoRoot(std::span<const uint8_t> in, const ResolveCtx& rc, std::vector<uint8_t>& out)
    {
        out.clear();
        if (in.size() > std::numeric_limits<uint32_t>::max())
            return false;

        const uint32_t size = static_cast<uint32_t>(in.size());
        const iff::Reader reader(in);

        // Pass 1 validates the complete chunk stream and locates the fields used by the conversion. Unknown
        // chunks are not evidence of a modern file: stock/custom WotLK WMOs may legitimately carry chunks
        // this module does not know, and those files must be served byte-for-byte.
        iff::Chunk motxC{}, momtC{}, modiC{}, moddC{}, mosiC{};
        bool modernMarker = false;
        bool duplicateInput = false;
        bool rootShape = true;
        uint32_t chunkIndex = 0;
        const bool validChunks = reader.ForEach([&](const iff::Chunk& c)
        {
            if ((chunkIndex == 0 && c.magic != kMVER) ||
                (chunkIndex == 1 && c.magic != kMOHD))
                rootShape = false;
            ++chunkIndex;

            if (IsRootModernMarker(c.magic))
                modernMarker = true;

            auto captureOnce = [&](iff::Chunk& dst)
            {
                if (dst.data)
                    duplicateInput = true;
                else
                    dst = c;
            };

            if (c.magic == kMOTX)       captureOnce(motxC);
            else if (c.magic == kMOMT)  captureOnce(momtC);
            else if (c.magic == kMODI)  captureOnce(modiC);
            else if (c.magic == kMODD)  captureOnce(moddC);
            else if (c.magic == kMOSI)  captureOnce(mosiC);
            return true;
        });

        if (!validChunks || chunkIndex < 2 || !rootShape || duplicateInput)
            return false;

        // A source-only chunk is the strongest signal. Modern MOMT encodings are also accepted so stripped
        // exports remain usable: a shader outside the native table or a non-zero texture reference outside
        // the MOTX blob is a FileDataID encoding. Runtime data alone is never a signal.
        bool modernEncoding = false;
        if (momtC.data)
        {
            const uint32_t records = momtC.size / kMomtStride;
            for (uint32_t i = 0; i < records && !modernEncoding; ++i)
            {
                const uint8_t* m = momtC.data + static_cast<size_t>(i) * kMomtStride;
                if (Rd32(m + kMomtShaderOffset) > kMaxNativeShader)
                {
                    modernEncoding = true;
                    break;
                }

                for (uint32_t t = 0; t < kMomtTexCount; ++t)
                {
                    const uint32_t off = Rd32(m + kMomtTexOffsets[t]);
                    if (off != 0 && (!motxC.data || off >= motxC.size))
                    {
                        modernEncoding = true;
                        break;
                    }
                }
            }
        }

        if (!modernMarker && !modernEncoding)
            return false;

        // Once translation is positively selected, reject malformed record arrays instead of rebuilding a
        // plausible-looking file around truncated data.
        if ((momtC.data && (momtC.size % kMomtStride) != 0) ||
            (modiC.data && (modiC.size % sizeof(uint32_t)) != 0) ||
            (moddC.data && (moddC.size % 0x28) != 0) ||
            (mosiC.data && mosiC.size != sizeof(uint32_t)))
            return false;

        // Build the final MOTX blob and resolve FileDataID texture references in MOMT against it.
        MotxBuilder motx;
        if (motxC.data)
            motx.Seed(motxC.data, motxC.size);
        if (!motx.IsValid())
            return false;

        std::vector<uint8_t> mats;
        uint32_t matCount = 0, fdidResolved = 0, fdidFallback = 0, collapsedShaders = 0;
        bool buildFailed = false;
        if (momtC.data)
        {
            mats.assign(momtC.data, momtC.data + momtC.size);
            matCount = momtC.size / kMomtStride;

            uint32_t fallbackOff = 0xFFFFFFFFu; // first resolved tex1 doubles as per-WMO fallback

            uint32_t dbgShader = 0; std::string dbgPath[2]; // probe: what we bind on material 0

            for (uint32_t i = 0; i < matCount; ++i)
            {
                uint8_t* m = mats.data() + i * kMomtStride;
                if (i == 0) dbgShader = Rd32(m + kMomtShaderOffset);

                memset(m + kMomtRunTimeOffset, 0, kMomtRunTimeLen);

                // Shader 23 keeps its real diffuse in texture_2 (texture_1 is a shared effects map used
                // across many unrelated materials); promote it before the remap below picks its target.
                const uint32_t shader = Rd32(m + kMomtShaderOffset);
                if (shader == kShaderDFSurface)
                    Wr32(m + kMomtTexOffsets[0], Rd32(m + kMomtTexOffsets[1]));

                // Every source shader goes through the same nearest-match table, not just ids past 6: a
                // "modern" source previously had ALL its shaders forced to 0 unconditionally, flattening
                // natively-renderable env/specular/metal materials (1/2/3/5) to plain diffuse too.
                const uint32_t remapped = RemapShader(shader);
                if (remapped != shader)
                {
                    Wr32(m + kMomtShaderOffset, remapped);
                }

                for (uint32_t t = 0; t < kMomtTexCount; ++t)
                {
                    const uint32_t off = Rd32(m + kMomtTexOffsets[t]);
                    if (motx.IsStringOffset(off))
                        continue; // genuine in-blob offset

                    // Out of MOTX bounds -> a FileDataID; resolve to a path or fall back.
                    std::string path;
                    const bool resolved = rc.resolve && rc.resolve(rc.user, off, path) && !path.empty();
                    uint32_t newOff;
                    if (resolved)
                    {
                        newOff = motx.Append(path);
                        ++fdidResolved;
                    }
                    else
                    {
                        if (fallbackOff == MotxBuilder::kNotFound)
                            fallbackOff = motx.Append(kFallbackTexture);
                        newOff = fallbackOff;
                        ++fdidFallback;
                    }
                    if (newOff == MotxBuilder::kNotFound)
                    {
                        buildFailed = true;
                        break;
                    }
                    Wr32(m + kMomtTexOffsets[t], newOff);
                    if (i == 0 && t < 2)
                        dbgPath[t] = resolved ? path : std::string("<fdid ") + std::to_string(off) + ">";
                }
                if (buildFailed)
                    break;

                if (fallbackOff == MotxBuilder::kNotFound)
                {
                    const uint32_t tex1 = Rd32(m + kMomtTexOffsets[0]);
                    if (motx.IsStringOffset(tex1))
                        fallbackOff = tex1;
                }

                if (remapped != shader)
                    ++collapsedShaders;
            }
            WLOG_DEBUG("wmo-mat: mats=%u collapsed=%u fdid=%u fallback=%u mat0 shader=%u tex1=%s tex2=%s",
                       matCount, collapsedShaders, fdidResolved, fdidFallback, dbgShader,
                       dbgPath[0].c_str(), dbgPath[1].c_str());
        }
        if (buildFailed)
            return false;

        // Doodad FileDataID -> name. Retail WMOs reference their doodad models by FileDataID (MODI) with an
        // EMPTY MODN, and each MODD def's name index points into MODI, not MODN. The Client only reads names
        // from MODN, so resolve every MODI FileDataID to a path, rebuild MODN, and remap each MODD def's name
        // index to that path's byte offset. Without it the doodads have no model
        constexpr uint32_t kModdStride = 0x28;
        MotxBuilder modn;
        std::vector<uint8_t> modd;
        bool rebuiltDoodads = false;
        if (modiC.data && moddC.data && moddC.size >= kModdStride)
        {
            modn.data.push_back(0); // offset 0 = empty name; unresolved doodads load nothing
            const uint32_t nModi = modiC.size / 4;
            std::vector<uint32_t> nameOff(nModi, 0);
            uint32_t resolved = 0;
            for (uint32_t k = 0; k < nModi; ++k)
            {
                const uint32_t fdid = Rd32(modiC.data + k * 4);
                std::string path;
                if (fdid && rc.resolve && rc.resolve(rc.user, fdid, path) && !path.empty())
                {
                    nameOff[k] = modn.Append(path);
                    if (nameOff[k] == MotxBuilder::kNotFound ||
                        nameOff[k] > 0x00FFFFFFu)
                    {
                        buildFailed = true;
                        break;
                    }
                    ++resolved;
                }
            }
            if (buildFailed)
                return false;
            modd.assign(moddC.data, moddC.data + moddC.size);
            const uint32_t nDefs = moddC.size / kModdStride;
            for (uint32_t i = 0; i < nDefs; ++i)
            {
                uint8_t* e = modd.data() + i * kModdStride;
                const uint32_t v   = Rd32(e);
                const uint32_t idx = v & 0x00FFFFFFu;   // modern: index into MODI
                const uint32_t off = (idx < nModi) ? nameOff[idx] : 0;
                Wr32(e, (v & 0xFF000000u) | (off & 0x00FFFFFFu)); // keep doodad flags (high byte)
            }
            rebuiltDoodads = true;
            WLOG_DEBUG("wmo-doodad: MODI %u (resolved %u) -> MODN names, %u defs (blob %u B)",
                       nModi, resolved, nDefs, uint32_t(modn.data.size()));
        }

        // Newer roots store the skybox model as a FileDataID in MOSI. Convert it to the native MOSB
        // NUL-terminated path when the resolver knows it; otherwise retain an existing MOSB fallback.
        std::vector<uint8_t> skyboxName;
        bool rebuiltSkybox = false;
        if (mosiC.data)
        {
            const uint32_t fdid = Rd32(mosiC.data);
            std::string path;
            if (fdid && rc.resolve && rc.resolve(rc.user, fdid, path) && !path.empty() &&
                path.find('\0') == std::string::npos &&
                path.size() < std::numeric_limits<uint32_t>::max())
            {
                skyboxName.assign(path.begin(), path.end());
                skyboxName.push_back(0);
                rebuiltSkybox = true;
            }
        }

        // A MOTX must always exist (loader base pointer); an empty blob still needs one NUL.
        if (motx.data.empty())
            motx.data.push_back(0);

        // Pass 2: rebuild as a whitelist in canonical order. MOTX/MOMT carry rebuilt payloads; missing
        // mandatory chunks are synthesized empty to keep the positional walk aligned; MCVP trails.
        out.clear();
        if (static_cast<size_t>(size) > std::numeric_limits<size_t>::max() - 16 ||
            motx.data.size() > std::numeric_limits<size_t>::max() - size - 16)
            return false;
        out.reserve(static_cast<size_t>(size) + motx.data.size() + 16);

        for (uint32_t magic : kCanonicalRoot)
        {
            if (magic == kMOTX) { iff::Emit(out, kMOTX, motx.data.data(), static_cast<uint32_t>(motx.data.size())); continue; }
            if (magic == kMOMT) { iff::Emit(out, kMOMT, mats.data(), static_cast<uint32_t>(mats.size())); continue; }
            if (magic == kMOHD)
            {
                iff::Chunk c{};
                if (reader.Find(kMOHD, c) && c.size >= 0x3E)
                {
                    std::vector<uint8_t> mohd(c.data, c.data + c.size);
                    Wr16(mohd.data() + 0x3C, uint16_t(Rd16(mohd.data() + 0x3C) & ~uint16_t(0x8)));
                    iff::Emit(out, kMOHD, mohd.data(), static_cast<uint32_t>(mohd.size()));
                }
                else if (c.data) iff::Emit(out, kMOHD, c.data, c.size);
                else             iff::Emit(out, kMOHD, nullptr, 0);
                continue;
            }
            if (rebuiltDoodads && magic == kMODN) { iff::Emit(out, kMODN, modn.data.data(), static_cast<uint32_t>(modn.data.size())); continue; }
            if (rebuiltDoodads && magic == kMODD) { iff::Emit(out, kMODD, modd.data(), static_cast<uint32_t>(modd.size())); continue; }
            if (rebuiltSkybox && magic == kMOSB) { iff::Emit(out, kMOSB, skyboxName.data(), static_cast<uint32_t>(skyboxName.size())); continue; }
            iff::Chunk c{};
            if (reader.Find(magic, c)) iff::Emit(out, magic, c.data, c.size);
            else                       iff::Emit(out, magic, nullptr, 0);
        }

        iff::Chunk mcvp{};
        if (reader.Find(kMCVP, mcvp))
            iff::Emit(out, kMCVP, mcvp.data, mcvp.size);

        if (out.size() > std::numeric_limits<uint32_t>::max())
        {
            out.clear();
            return false;
        }
        return true;
    }

    bool TranslateWmoGroup(std::span<const uint8_t> in, std::vector<uint8_t>& out)
    {
        out.clear();
        const uint8_t* buf = in.data();
        const size_t size = in.size();

        if (!buf || size < 12 || size > std::numeric_limits<uint32_t>::max() || Rd32(buf) != kMVER)
            return false;

        const uint32_t mverPayload = Rd32(buf + 4);
        if (static_cast<size_t>(mverPayload) > size - 8)
            return false;
        const size_t mverLen = 8 + static_cast<size_t>(mverPayload);
        const size_t afterMver = mverLen;
        if (size - afterMver < 8 || Rd32(buf + afterMver) != kMOGP)
            return false;

        const size_t mogpHdr  = afterMver;
        const size_t mogpData = mogpHdr + 8;
        const uint32_t mogpSize = Rd32(buf + mogpHdr + 4);
        if (static_cast<size_t>(mogpSize) > size - mogpData ||
            mogpSize < kMogpHeaderSize)
            return false;
        const size_t mogpEnd = mogpData + static_cast<size_t>(mogpSize);
        if (mogpEnd != size)
            return false;
        const size_t subStart = mogpData + kMogpHeaderSize;

        // Walk the sub-chunk region. Keep the first occurrence of each mandatory chunk and the first MOTV /
        // first MOCV; collect optional kept chunks in source order. A 2nd MOTV/MOCV and unknowns are dropped.
        const iff::Reader gr(in.subspan(subStart, mogpEnd - subStart));

        iff::Chunk mopy{}, movi{}, movt{}, monr{}, motv{}, moba{}, mpy2{};
        iff::Chunk molr{}, modr{}, mobn{}, mobr{}, mocv{}, mliq{};
        iff::Chunk motv2{}, mocv2{};
        bool hasMOBN = false, hasMOBR = false, hasMOCV = false, hasMOLR = false, hasMODR = false, hasMLIQ = false;
        bool seenMOTV = false;
        bool modernMarker = false;

        const bool validChunks = gr.ForEach([&](const iff::Chunk& c)
        {
            if (IsGroupModernMarker(c.magic))
                modernMarker = true;

            switch (c.magic)
            {
                case kMOPY: if (!mopy.data) mopy = c; break;
                case kMPY2: if (!mpy2.data) mpy2 = c; break;
                case kMOVI: if (!movi.data) movi = c; break;
                case kMOVT: if (!movt.data) movt = c; break;
                case kMONR: if (!monr.data) monr = c; break;
                case kMOBA: if (!moba.data) moba = c; break;
                case kMOTV: if (!seenMOTV) { motv = c; seenMOTV = true; } else if (!motv2.data) motv2 = c; break;
                case kMOCV: if (!hasMOCV) { hasMOCV = true; mocv = c; } else if (!mocv2.data) mocv2 = c; break;
                case kMOLR: if (!hasMOLR) { hasMOLR = true; molr = c; } break;
                case kMODR: if (!hasMODR) { hasMODR = true; modr = c; } break;
                case kMOBN: if (!hasMOBN) { hasMOBN = true; mobn = c; } break;
                case kMOBR: if (!hasMOBR) { hasMOBR = true; mobr = c; } break;
                case kMLIQ: if (!hasMLIQ) { hasMLIQ = true; mliq = c; } break;
                default: break; // dropped
            }
            return true;
        });
        if (!validChunks)
            return false;

        // A native group is not translated simply because it starts with MOPY. Require either a source-only
        // chunk or the newer MOBA material-id relocation encoding.
        bool hasMobaRelocation = false;
        if (moba.data)
        {
            const uint32_t entries = moba.size / kMobaEntryStride;
            for (uint32_t i = 0; i < entries; ++i)
            {
                if ((moba.data[static_cast<size_t>(i) * kMobaEntryStride + kMobaFlagOffset] &
                     kMobaRelocFlag) != 0)
                {
                    hasMobaRelocation = true;
                    break;
                }
            }
        }
        if (!modernMarker && !hasMobaRelocation)
            return false;

        // Validate every fixed-stride array touched below. Partial trailing records are corruption, not
        // something the translator can safely repair.
        if ((mopy.data && (mopy.size % 2) != 0) ||
            (mpy2.data && (mpy2.size % 4) != 0) ||
            (movi.data && (movi.size % 2) != 0) ||
            (movt.data && (movt.size % kMovtStride) != 0) ||
            (monr.data && (monr.size % kMovtStride) != 0) ||
            (motv.data && (motv.size % 8) != 0) ||
            (motv2.data && (motv2.size % 8) != 0) ||
            (moba.data && (moba.size % kMobaEntryStride) != 0) ||
            (mocv.data && (mocv.size % kMocvStride) != 0) ||
            (mocv2.data && (mocv2.size % kMocvStride) != 0))
            return false;

        if ((!mopy.data && !mpy2.data) || !movi.data || !movt.data || !monr.data ||
            !motv.data || !moba.data || (movi.size % 6) != 0)
            return false;

        const uint32_t vertexCount = movt.size / kMovtStride;
        const uint32_t triangleCount = movi.size / 6;
        if (monr.size < static_cast<uint64_t>(vertexCount) * kMovtStride ||
            motv.size < static_cast<uint64_t>(vertexCount) * 8 ||
            (mopy.data && mopy.size < static_cast<uint64_t>(triangleCount) * 2) ||
            (!mopy.data && mpy2.size < static_cast<uint64_t>(triangleCount) * 4) ||
            (mocv.data && mocv.size < static_cast<uint64_t>(vertexCount) * kMocvStride))
            return false;

        // Vertex colors (MOCV) are emitted verbatim: the native lighting is preserved, never rewritten.

        // Build the rebuilt sub-chunk region: the six mandatory chunks (source bytes, else empty), then
        // the kept optional chunks in the native gate order.
        std::vector<uint8_t> subRegion;
        subRegion.reserve(mogpSize);

        const bool injMOPY = !mopy.data, injMOVI = !movi.data, injMOVT = !movt.data;
        const bool injMONR = !monr.data, injMOTV = !motv.data, injMOBA = !moba.data;

        // Track MOVT/MOBA payload positions inside subRegion for the MOBA fix.
        size_t movtPayloadOff = 0, mobaPayloadOff = 0;
        uint32_t movtPayloadLen = 0, mobaPayloadLen = 0;

        auto emitMandatory = [&](const iff::Chunk& c, uint32_t magic, size_t* poff, uint32_t* plen)
        {
            const size_t payloadOff = subRegion.size() + 8;
            const uint32_t payloadLen = c.data ? c.size : 0;
            if (c.data) iff::EmitRaw(subRegion, c);
            else        iff::Emit(subRegion, magic, nullptr, 0);
            if (poff) *poff = payloadOff;
            if (plen) *plen = payloadLen;
        };

        // MOPY: from source, or synthesized from MPY2 (4B/tri {u16 flags, u16 mat} -> 2B/tri {u8, u8}).
        // 0xFFFF mat (collision-only) maps to 0xFF; triangle count is identical so MOVI still indexes 1:1.
        std::vector<uint8_t> synthMopy;
        if (!mopy.data && mpy2.data)
        {
            const uint32_t tris = mpy2.size / 4;
            synthMopy.resize(static_cast<size_t>(tris) * 2);
            for (uint32_t t = 0; t < tris; ++t)
            {
                const uint8_t* s = mpy2.data + t * 4;
                const uint16_t fl  = Rd16(s);
                const uint16_t mat = Rd16(s + 2);
                synthMopy[t * 2]     = static_cast<uint8_t>(fl & 0xFF);
                synthMopy[t * 2 + 1] = (mat == 0xFFFF) ? 0xFF : static_cast<uint8_t>(mat & 0xFF);
            }
        }

        if (!synthMopy.empty())
            iff::Emit(subRegion, kMOPY, synthMopy.data(), static_cast<uint32_t>(synthMopy.size()));
        else
            emitMandatory(mopy, kMOPY, nullptr, nullptr);
        emitMandatory(movi, kMOVI, nullptr, nullptr);
        emitMandatory(movt, kMOVT, &movtPayloadOff, &movtPayloadLen);
        emitMandatory(monr, kMONR, nullptr, nullptr);
        emitMandatory(motv, kMOTV, nullptr, nullptr);
        emitMandatory(moba, kMOBA, &mobaPayloadOff, &mobaPayloadLen);

        // Optional chunks in the fixed order the native positional parser consumes them, regardless of their
        // order in the source group: MOLR, MODR, MOBN, MOBR, MOCV, MLIQ. MOBN precedes MOBR (the BSP branch
        // reads the pair together). A different order desyncs the cursor so later chunks (e.g. MLIQ) read from
        // the wrong offset and the liquid build faults on a null vertex array.
        if (hasMOLR) iff::EmitRaw(subRegion, molr);
        if (hasMODR) iff::EmitRaw(subRegion, modr);
        const bool emitBSP = hasMOBN && hasMOBR;
        if (emitBSP) iff::EmitRaw(subRegion, mobn);
        if (emitBSP) iff::EmitRaw(subRegion, mobr);
        if (hasMOCV) iff::EmitRaw(subRegion, mocv);
        if (hasMLIQ) iff::EmitRaw(subRegion, mliq);

        // Native two-layer (shader 6) needs a 2nd texcoord + 2nd color set: the 2nd MOCV's alpha blends the
        // two texture layers. Emit them last in the native order (MOTV2 then MOCV2), only when each is a full
        // per-vertex set, else the engine's vertex-fill reads past a short set and faults.
        const uint32_t nVerts2 = movt.data ? (movt.size / kMovtStride) : 0;
        const bool emitMOTV2 = motv2.data && nVerts2 && motv2.size >= nVerts2 * 8;
        const bool emitMOCV2 = mocv2.data && nVerts2 && mocv2.size >= nVerts2 * kMocvStride;
        if (emitMOTV2) iff::EmitRaw(subRegion, motv2);
        if (emitMOCV2) iff::EmitRaw(subRegion, mocv2);

        // MOBA material-id fix on the freshly emitted MOBA/MOVT payloads. Injected MOBA has no entries.
        if (mobaPayloadLen != 0)
            FixMobaChunk(subRegion.data() + mobaPayloadOff, mobaPayloadLen,
                         subRegion.data() + movtPayloadOff, movtPayloadLen);

        // Assemble: MVER verbatim, MOGP wrapper + fixed 0x44 header (patched), then the rebuilt region.
        constexpr size_t kGroupWrapperSize = 8 + kMogpHeaderSize;
        if (subRegion.size() > std::numeric_limits<uint32_t>::max() - kMogpHeaderSize ||
            mverLen > std::numeric_limits<uint32_t>::max() - kGroupWrapperSize ||
            subRegion.size() >
                std::numeric_limits<uint32_t>::max() - mverLen - kGroupWrapperSize)
            return false;

        out.clear();
        out.reserve(mverLen + 8 + kMogpHeaderSize + subRegion.size());

        out.insert(out.end(), buf, buf + mverLen);

        const size_t mogpHeaderStart = out.size();
        out.insert(out.end(), buf + mogpHdr, buf + mogpData + kMogpHeaderSize);

        uint8_t* outMogpData = out.data() + mogpHeaderStart + 8;

        // Recompute group flags: clear ONLY the flags the Client gates chunk consumption on, re-set them for
        // the chunks actually emitted, and preserve every other bit. Clearing an unmanaged bit a native group
        // legitimately sets makes the positional parser misgate and build a corrupt group.
        const uint32_t oldFlags = Rd32(outMogpData + kMogpFlagsOffset);
        uint32_t newFlags = oldFlags & ~kGrpGatedFlags;
        if (emitBSP) newFlags |= kGrpFlagBSP;
        if (hasMOCV) newFlags |= kGrpFlagMOCV;
        if (hasMOLR) newFlags |= kGrpFlagMOLR;
        if (hasMODR) newFlags |= kGrpFlagMODR;
        if (hasMLIQ) newFlags |= kGrpFlagMLIQ;
        if (emitMOTV2) newFlags |= kGrpFlagMOTV2;
        if (emitMOCV2) newFlags |= kGrpFlagMOCV2;
        Wr32(outMogpData + kMogpFlagsOffset, newFlags);

        // Remap the liquid id to one the Client can fully resolve. Only groups that carry liquid feed the
        // liquid pass, so leave the field untouched otherwise.
        bool liquidRemapped = false;
        if (hasMLIQ)
        {
            const uint32_t rawLiquid  = Rd32(outMogpData + kMogpGroupLiquidOffset);
            const uint32_t safeLiquid = MapWmoLiquidType(rawLiquid);
            if (safeLiquid != rawLiquid)
            {
                Wr32(outMogpData + kMogpGroupLiquidOffset, safeLiquid);
                liquidRemapped = true;
            }
        }

        // Injected empty MOBA means zero batches; zero the header counts to match.
        if (injMOBA)
        {
            Wr16(outMogpData + kMogpTransBatchCountOffset, 0);
            Wr16(outMogpData + kMogpIntBatchCountOffset,   0);
            Wr16(outMogpData + kMogpExtBatchCountOffset,   0);
        }

        const uint32_t newMogpSize =
            kMogpHeaderSize + static_cast<uint32_t>(subRegion.size());
        Wr32(out.data() + mogpHeaderStart + 4, newMogpSize);

        out.insert(out.end(), subRegion.begin(), subRegion.end());

        // No-op: nothing injected, no flag change, region byte-identical to the source span.
        const bool injected = injMOPY || injMOVI || injMOVT || injMONR || injMOTV || injMOBA;
        const size_t origSubLen = mogpEnd - subStart;
        if (!injected && !liquidRemapped && newFlags == oldFlags &&
            subRegion.size() == origSubLen && memcmp(subRegion.data(), buf + subStart, origSubLen) == 0)
            return false;

        return true;
    }
}
