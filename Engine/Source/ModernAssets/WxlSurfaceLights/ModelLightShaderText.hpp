#pragma once
// Vertex-shader text patches that add the surface lights to the client's M2 and WMO lit shaders.
// Imported from the supplied wxl-living-azeroth features/ModelLights.cpp (Morfium's Living Azeroth,
// GPL-3.0-or-later) without changes to the assembly text or the edit rules; only the wrapper
// differs: the code lives in a header and its former file-level state is below. The native client
// cannot run the Microsoft assembler at run time, so tools/build-wmo-shader-pack.py applies these
// edits offline (see docs/WXL-NATIVE-PORT.md, "Surface lights").
//
// The per-frame data is one A32B32G32R32F texture on vertex sampler 0, laid out by the header
// comment of the native light texture (SurfaceLightTexture.hpp).
#include "LightPool.hpp"

#include <cstdio>
#include <regex>
#include <string>
#include <vector>

namespace wxl::surfacelights::modelshader
{
    constexpr int kWidth = 1024;       // texels per row of the light texture (the shaders' defs know it)
    // The vertex-set name of the shader being patched ("Diffuse_T1_Env", "MapObjDiffuse_T1"): the edit
    // tells WMO families from M2 ones that look alike in text.
    inline std::string g_family;
    inline unsigned g_patched = 0;
    inline unsigned g_marked = 0;        // baked-colour WMO variants carrying the marker
    inline unsigned g_normalsGiven = 0;  // of those, shadow-tier-0 variants given a normal input

    // Anchors (all M2/WMO vertex families, dumps 2026-10-05). Each value is captured where the
    // stock code uses it, which holds whatever order the compiler scheduled things in:
    //  - the position (view space): the register the projection reads, `dp4 oN.x, c2, rP`;
    //  - the normal: the register the sun term reads, `dp3_sat rX.c, -c12, rN` (only lit
    //    variants have it; the rest are unlit materials and stay stock);
    //  - the light sum: what the colour write reads, `mad_sat oK, <material>, rL, c29` (M2: a
    //    copy of c28; WMO and CDiffuse: the vertex colour) or `add_sat oK, rL, c29` (WMO
    //    composite).
    inline const std::regex kPosition(R"(^\s*dp4 o\d+\.x, c2, (r\d+)\s*$)");
    inline const std::regex kSun(R"(^\s*dp3_sat r\d+\.[xyzw], -c12, (r\d+)\s*$)");
    inline const std::regex kColour(R"(^\s*(?:mad_sat o\d+(?:\.xyz)?, [rv]\d+, (r\d+), c29|add_sat o\d+(?:\.xyz)?, (r\d+), c29)\s*$)");

    // c1 = (header u of t0, v of row 0, 1 / W: one texel along the row, 0).
    inline std::string Prologue()
    {
        char d[160];
        std::snprintf(d, sizeof(d), "    def c1, %.9g, 0, %.9g, 0\n    defi i0, %d, 0, 0, 0\n    dcl_2d s0\n",
                      0.5 / kWidth, 1.0 / kWidth, kMaxPerCellCap);
        return d;
    }

    // Before the loop: the vertex's cell. Position r29 (view) -> camera-relative -> world (the
    // world-stripes debug view's transform, confirmed in-client), minus the grid's corner (t11 in
    // r20), in cells, floored, clamped to the grid (a light past its radius adds 0, so a clamped
    // cell is only wasted work); r28.y = its first entry (cell x lights per cell).
    inline std::string CellSetup()
    {
        return
            "    mov r17.x, r13.w\n"
            "    mov r17.y, r14.w\n"
            "    mov r17.z, r15.w\n"
            "    add r17.xyz, r29, -r17\n"
            "    mul r18.xyz, r17.x, r13\n"
            "    mad r18.xyz, r17.y, r14, r18\n"
            "    mad r18.xyz, r17.z, r15, r18\n"
            "    add r18.xy, r18, r16\n"
            "    add r18.xy, r18, -r20\n"
            "    mul r18.xy, r18, r20.z\n"
            "    frc r19.xy, r18\n"
            "    add r18.xy, r18, -r19\n"
            "    max r18.xy, r18, r10.y\n"
            "    add r19.x, r20.w, -r10.x\n"
            "    min r18.xy, r18, r19.x\n"
            "    mad r18.x, r18.y, r20.w, r18.x\n"
            "    mul r28.y, r18.x, r9.z\n";
    }

    // Baked occlusion (lights::Bake), inside the loop once the light's strength r20.x is known: the
    // direction from the light to this vertex, back to world space (view columns r13..r15, the
    // world-stripes transform), on the octahedral map (n = d / |d|1, lower half folded), its cell
    // of 8 x 8, the tile's texel (4 cells each) and component; a vertex farther than the stored
    // reach (+ 0.5 yd, soft over 0.5 yd) gets none of this light. 4th texel r23.y = tile / tiles
    // per row (-1 = none), r16.w = the region's first row + 0.5.
    inline std::string OcclusionVS()
    {
        return
            "      if_ge r23.y, c1.w\n"
            "      mul r17.xyz, r26.x, r13\n"
            "      mad r17.xyz, r26.y, r14, r17\n"
            "      mad r17.xyz, r26.z, r15, r17\n"
            "      add r18.x, r17_abs.x, r17_abs.y\n"
            "      add r18.x, r18.x, r17_abs.z\n"
            "      rcp r18.x, r18.x\n"
            "      mul r18.xyz, -r17, r18.x\n"
            "      sge r19.xy, r18, c1.w\n"
            "      add r19.xy, r19, r19\n"
            "      add r19.xy, r19, -r10.x\n"
            "      add r19.zw, r10.x, -r18_abs.xxyx\n"
            "      mul r19.xy, r19, r19.zw\n"
            "      add r19.xy, r19, -r18\n"
            "      slt r19.z, r18.z, c1.w\n"
            "      mad r18.xy, r19, r19.z, r18\n"
            "      rcp r19.w, r12.z\n"
            "      add r18.xy, r18, r10.x\n"
            "      mul r18.xy, r18, r19.w\n"
            "      add r19.z, r19.w, r19.w\n"
            "      add r19.z, r19.z, -r12.y\n"
            "      min r18.xy, r18, r19.z\n"
            "      frc r19.xy, r18\n"
            "      add r18.xy, r18, -r19\n"
            "      mul r19.x, r18.x, r12.z\n"
            "      frc r19.y, r19.x\n"
            "      add r19.x, r19.x, -r19.y\n"
            "      mul r19.y, r19.y, r19.w\n"
            "      add r18.y, r18.y, r18.y\n"
            "      add r19.x, r19.x, r18.y\n"
            "      frc r18.z, r23.y\n"
            "      add r18.w, r23.y, -r18.z\n"
            "      add r19.x, r19.x, r10.z\n"
            "      mad r27.x, r19.x, r9.x, r18.z\n"
            "      add r18.w, r18.w, r16.w\n"
            "      mul r27.y, r18.w, r9.y\n"
            "      texldl r18, r27, s0\n"
            "      add r17.x, r19.y, -r10.y\n"
            "      add r17.y, r19.y, -r10.x\n"
            "      add r17.z, r17.y, -r10.x\n"
            "      add r17.w, r17.z, -r10.x\n"
            "      slt r17, r17_abs, r10.z\n"
            "      dp4 r17.x, r18, r17\n"
            "      add r17.x, r17.x, -r22.w\n"
            "      add r17.x, r17.x, r17.x\n"
            "      add_sat r17.x, r17.x, r10.x\n"
            "      mul r20.x, r20.x, r17.x\n"
            "      endif\n";
    }

    // Lit WMO surfaces are exteriors (interiors draw with the baked-light variants): a light inside
    // an indoor group (4th texel r23.w = 1, set only while the panel's switch is on) stays off them,
    // like off the terrain, so a chimney or cellar torch doesn't light the outside walls.
    inline std::string IndoorSkip()
    {
        return
            "      add r23.w, r10.x, -r23.w\n"
            "      mul r20.x, r20.x, r23.w\n";
    }

    // At the top of the loop (counter r28.z): this cell's entry r28.y + r28.z at texel (column,
    // 1 + row) of the W-wide rows; an empty entry ends the list; r27.xy = the light's first texel.
    inline std::string EntryFetch()
    {
        return
            "      break_ge r28.z, r9.z\n"
            "      add r17.x, r28.y, r28.z\n"
            "      mul r17.x, r17.x, r9.x\n"
            "      frc r17.y, r17.x\n"
            "      add r17.z, r17.x, -r17.y\n"
            "      add r27.x, r17.y, c1.x\n"
            "      add r17.z, r17.z, r10.x\n"
            "      add r17.z, r17.z, r10.z\n"
            "      mul r27.y, r17.z, r9.y\n"
            "      texldl r26, r27, s0\n"
            "      break_lt r26.z, r10.z\n"
            "      mov r27.xy, r26\n";
    }

    // Before the colour write, inside `if valid`: the header, then our lights summed into r31
    // from the position r29 and the normal r30 (the terrain's and cover's light: size ~1 yd,
    // 1 / (0.7 s + 0.03 s^2) on s = sqrt(d^2 + 1), windowed to 0 at the radius, wrapped Lambert,
    // spot cone), each moved world -> camera-relative -> view first. Then folded into the stock
    // light sum `l` like on the terrain: t = l + ours; above a knee (max(0.8, brightest channel
    // of l)) the brightest channel of t is compressed smoothly and all three scaled alike, so
    // the hue stays. Last, the debug views (weights from t5): ours only, this cell's count / its
    // capacity in red, world stripes frac(xy / 4). Temps r9..r31 are free here (stock uses up to
    // r8; r9..r12 held the switched stock colours, read before this point).
    // `extra` goes in the loop right after the light's strength (r20.x) is known: IndoorSkip() on
    // WMO surfaces, else empty.
    inline std::string LampBlock(const std::string& l, const std::string& extra)
    {
        return std::string() +
            "    mov r27, c1\n"
            "    texldl r28, r27, s0\n"
            "    if_gt r28.x, c1.w\n"
            "    add r27.x, r27.x, r27.z\n"
            "    texldl r16, r27, s0\n"
            "    add r27.x, r27.x, r27.z\n"
            "    texldl r13, r27, s0\n"
            "    add r27.x, r27.x, r27.z\n"
            "    texldl r14, r27, s0\n"
            "    add r27.x, r27.x, r27.z\n"
            "    texldl r15, r27, s0\n"
            "    add r27.x, r27.x, r27.z\n"
            "    add r27.x, r27.x, r27.z\n"
            "    texldl r9, r27, s0\n"
            "    add r27.x, r27.x, r27.z\n"
            "    texldl r10, r27, s0\n"
            "    add r27.x, r27.x, r27.z\n"
            "    texldl r11, r27, s0\n"
            "    add r27.x, r27.x, r27.z\n"
            "    texldl r12, r27, s0\n"
            "    add r27.x, r27.x, r27.z\n"
            "    add r27.x, r27.x, r27.z\n"
            "    texldl r20, r27, s0\n"
            + CellSetup() +
            "    mov r31.xyz, r10.y\n"
            "    mov r28.z, r10.y\n"
            "    mov r27, c1\n"
            "    rep i0\n"
            + EntryFetch() +
            "      texldl r26, r27, s0\n"
            "      add r27.x, r27.x, r9.x\n"
            "      texldl r25, r27, s0\n"
            "      add r27.x, r27.x, r9.x\n"
            "      texldl r24, r27, s0\n"
            "      add r27.x, r27.x, r9.x\n"
            "      texldl r23, r27, s0\n"
            "      add r19.xyz, r26, -r16\n"
            "      mov r19.w, r10.x\n"
            "      dp4 r18.x, r19, r13\n"
            "      dp4 r18.y, r19, r14\n"
            "      dp4 r18.z, r19, r15\n"
            "      add r26.xyz, r18, -r29\n"
            "      dp3 r17.x, r24, r13\n"
            "      dp3 r17.y, r24, r14\n"
            "      dp3 r17.z, r24, r15\n"
            "      mov r24.xyz, r17\n"
            "      dp3 r22.x, r26, r26\n"
            "      add r22.y, r22.x, r10.x\n"
            "      rsq r22.z, r22.x\n"
            "      rcp r22.w, r22.z\n"
            "      mul_sat r21.x, r22.w, r26.w\n"
            "      mad r21.x, r21.x, -r21.x, r10.x\n"
            "      mul r21.x, r21.x, r21.x\n"
            "      pow r21.x, r21.x, r25.w\n"
            "      mul r26.xyz, r26, r22.z\n"
            "      dp3 r21.y, r30, r26\n"
            "      add r21.y, r21.y, r10.z\n"
            "      mul_sat r21.y, r21.y, r10.w\n"
            "      dp3 r21.w, -r26, r24\n"
            "      add r21.w, r21.w, -r24.w\n"
            "      mul_sat r21.w, r21.w, r23.x\n"
            "      mul r21.y, r21.y, r21.w\n"
            "      rsq r21.z, r22.y\n"
            "      rcp r21.w, r21.z\n"
            "      mad r20.x, r21.w, r11.y, r11.x\n"
            "      mul r20.x, r20.x, r21.w\n"
            "      rcp r20.x, r20.x\n"
            "      mul r20.x, r20.x, r21.x\n"
            "      mul r20.x, r20.x, r21.y\n"
            + OcclusionVS() + extra +
            "      mad r31.xyz, r25, r20.x, r31\n"
            "      add r28.z, r28.z, r10.x\n"
            "    endrep\n"
            "    mov r17.x, r13.w\n"
            "    mov r17.y, r14.w\n"
            "    mov r17.z, r15.w\n"
            "    add r17.xyz, r29, -r17\n"
            "    mul r18.xyz, r17.x, r13\n"
            "    mad r18.xyz, r17.y, r14, r18\n"
            "    mad r18.xyz, r17.z, r15, r18\n"
            "    add r18.xyz, r18, r16\n"
            "    mul r18.xy, r18, r12.z\n"
            "    frc r18.xy, r18\n"
            "    mov r18.z, r10.y\n"
            "    mul r19.x, r28.z, r12.w\n"
            "    mov r19.yz, r10.y\n"
            "    mov r27, c1\n"
            "    add r27.x, r27.x, r27.z\n"
            "    add r27.x, r27.x, r27.z\n"
            "    add r27.x, r27.x, r27.z\n"
            "    add r27.x, r27.x, r27.z\n"
            "    add r27.x, r27.x, r27.z\n"
            "    texldl r20, r27, s0\n"
            "    add r21.xyz, " + l + ", r31\n"
            "    max r22.x, r21.x, r21.y\n"
            "    max r22.x, r22.x, r21.z\n"
            "    max r22.y, " + l + ".x, " + l + ".y\n"
            "    max r22.y, r22.y, " + l + ".z\n"
            "    max r22.y, r22.y, r12.x\n"
            "    add r22.z, r22.x, -r22.y\n"
            "    max r22.z, r22.z, r10.y\n"
            "    add r22.w, r10.x, -r22.y\n"
            "    max r22.w, r22.w, r12.y\n"
            "    rcp r23.x, r22.w\n"
            "    mul r23.y, r22.z, r23.x\n"
            "    mul r23.y, r23.y, -r11.z\n"
            "    exp r23.y, r23.y\n"
            "    add r23.y, r10.x, -r23.y\n"
            "    mul r23.y, r23.y, r22.w\n"
            "    add r23.z, r22.x, -r22.z\n"
            "    add r23.z, r23.z, r23.y\n"
            "    max r23.w, r22.x, r12.y\n"
            "    rcp r23.w, r23.w\n"
            "    mul r23.z, r23.z, r23.w\n"
            "    mul " + l + ".xyz, r21, r23.z\n"
            "    add r24.x, r10.x, -r20.w\n"
            "    mul " + l + ".xyz, " + l + ", r24.x\n"
            "    mad " + l + ".xyz, r31, r20.x, " + l + "\n"
            "    mad " + l + ".xyz, r19, r20.y, " + l + "\n"
            "    mad " + l + ".xyz, r18, r20.z, " + l + "\n"
            "    endif\n"
            ;
    }

    // At the first instruction: the stock point lights' colours (c17..c20, renamed r9..r12),
    // scaled by 1 - t0.x. An unbound or empty texture reads 0, i.e. the stock lights stay on.
    inline std::string StockSwitch()
    {
        std::string s = "    mov r16, c1\n    texldl r16, r16, s0\n";
        for (int k = 0; k < 4; ++k)
        {
            const std::string r = "r" + std::to_string(9 + k);
            s += "    mov " + r + ", c" + std::to_string(17 + k) + "\n    mad " + r + ".xyz, " + r + ", -r16.x, " + r + "\n";
        }
        return s;
    }

    // Where the first instruction starts (after the version line, declarations and definitions).
    inline std::string::size_type FirstInstruction(const std::string& src)
    {
        std::string::size_type at = src.find('\n');
        while (at != std::string::npos && at + 1 < src.size())
        {
            const std::string::size_type end = src.find('\n', at + 1);
            const std::string line = src.substr(at + 1, end == std::string::npos ? std::string::npos : end - at - 1);
            const auto first = line.find_first_not_of(" \t");
            if (first != std::string::npos && line.compare(first, 3, "dcl") != 0 && line.compare(first, 3, "def") != 0 && line.compare(first, 2, "//") != 0)
                return at + 1;
            at = end;
        }
        return std::string::npos;
    }

    // Every line matching `re`: its start offset and first non-empty capture (empty when the
    // pattern has no groups).
    struct Hit { std::string::size_type at; std::string reg; };
    inline std::vector<Hit> Lines(const std::string& src, const std::regex& re)
    {
        std::vector<Hit> out;
        std::string::size_type at = 0;
        while (at < src.size())
        {
            std::string::size_type end = src.find('\n', at);
            if (end == std::string::npos) end = src.size();
            const std::string line = src.substr(at, end - at);
            std::smatch m;
            if (std::regex_match(line, m, re))
            {
                std::string reg;
                for (size_t g = 1; g < m.size(); ++g)
                    if (m[g].matched) { reg = m[g].str(); break; }
                out.push_back({ at, reg });
            }
            at = end + 1;
        }
        return out;
    }

    // WMO surfaces with baked light: the WMO variants without a sun term pass the baked vertex
    // colour straight on (`mov oColour, vColour`, all 315 in the dumps). Interior groups draw with
    // them (owner's bind probe + marker, Deadmines 2026-10-06). The baked colour already holds the
    // WMO's own torches, so ours FILL UP to it instead of stacking (owner + design 2026-10-06):
    //  - the same light loop as on lit surfaces, the normal from c31..c33 (the variants without a
    //    normal get distance-only light);
    //  - the baked light (the vertex colour, half scale: the pixel shader doubles it) dips with each
    //    flame's flicker by that flame's share of the spot's brightness (its light / the baked);
    //  - per channel, ours (x the panel's scale, halved) adds only what exceeds the baked light, so
    //    an orange torch over its own orange glow adds ~nothing, a dark corner or another colour gets
    //    it in full;
    //  - the terrain's knee on the brightest channel, then debug views (ours only, the t0.z marker,
    //    the t0.y normal check).
    // Normals: the shadow-tier-0 variants (indices 0..28 of each family) declare none, while the same
    // families at tiers 1/2 read one from the same WMO geometry, so the vertex data has it: those
    // get it declared (`injected`). A normal that reads as zero (data without one after all) leaves
    // that surface on distance-only light: r9.w = 1 when the normal is usable.
    inline std::string BakedBlock(const std::string& vc, const std::string& oc, const std::string& vn, bool injected)
    {
        std::string b = std::string() +
            "    mov r27, c1\n"
            "    texldl r28, r27, s0\n"
            "    if_gt r28.x, c1.w\n"
            "    add r27.x, r27.x, r27.z\n"
            "    texldl r16, r27, s0\n"
            "    add r27.x, r27.x, r27.z\n"
            "    texldl r13, r27, s0\n"
            "    add r27.x, r27.x, r27.z\n"
            "    texldl r14, r27, s0\n"
            "    add r27.x, r27.x, r27.z\n"
            "    texldl r15, r27, s0\n"
            "    add r27.x, r27.x, r27.z\n"
            "    add r27.x, r27.x, r27.z\n"
            "    texldl r9, r27, s0\n"
            "    add r27.x, r27.x, r27.z\n"
            "    texldl r10, r27, s0\n"
            "    add r27.x, r27.x, r27.z\n"
            "    texldl r11, r27, s0\n"
            "    add r27.x, r27.x, r27.z\n"
            "    texldl r12, r27, s0\n"
            "    add r27.x, r27.x, r27.z\n"
            "    add r27.x, r27.x, r27.z\n"
            "    texldl r20, r27, s0\n"
            "";
        if (!vn.empty()) b += std::string() +
            "    dp3 r30.x, c31, " + vn + "\n"
            "    dp3 r30.y, c32, " + vn + "\n"
            "    dp3 r30.z, c33, " + vn + "\n"
            "    dp3 r17.w, r30, r30\n"
            "    slt r9.w, r12.y, r17.w\n"
            "    max r17.w, r17.w, r12.y\n"
            "    rsq r17.w, r17.w\n"
            "    mul r30.xyz, r30, r17.w\n"
            "";
        b += std::string() +
            "    dp3 r30.w, " + vc + ", r10.x\n"
            "    add r30.w, r30.w, r30.w\n"
            "    add r30.w, r30.w, r12.y\n"
            "    rcp r29.w, r30.w\n"
            + CellSetup() +
            "    mov r31.xyz, r10.y\n"
            "    mov r28.x, r10.y\n"
            "    mov r28.z, r10.y\n"
            "    mov r27, c1\n"
            "    rep i0\n"
            + EntryFetch() +
            "      texldl r26, r27, s0\n"
            "      add r27.x, r27.x, r9.x\n"
            "      texldl r25, r27, s0\n"
            "      add r27.x, r27.x, r9.x\n"
            "      texldl r24, r27, s0\n"
            "      add r27.x, r27.x, r9.x\n"
            "      texldl r23, r27, s0\n"
            "      add r19.xyz, r26, -r16\n"
            "      mov r19.w, r10.x\n"
            "      dp4 r18.x, r19, r13\n"
            "      dp4 r18.y, r19, r14\n"
            "      dp4 r18.z, r19, r15\n"
            "      add r26.xyz, r18, -r29\n"
            "      dp3 r17.x, r24, r13\n"
            "      dp3 r17.y, r24, r14\n"
            "      dp3 r17.z, r24, r15\n"
            "      mov r24.xyz, r17\n"
            "      dp3 r22.x, r26, r26\n"
            "      add r22.y, r22.x, r10.x\n"
            "      rsq r22.z, r22.x\n"
            "      rcp r22.w, r22.z\n"
            "      mul_sat r21.x, r22.w, r26.w\n"
            "      mad r21.x, r21.x, -r21.x, r10.x\n"
            "      mul r21.x, r21.x, r21.x\n"
            "      pow r21.x, r21.x, r25.w\n"
            "      mul r26.xyz, r26, r22.z\n"
            "";
        if (!vn.empty()) b += std::string() +
            "      dp3 r21.y, r30, r26\n"
            "      add r21.y, r21.y, r10.z\n"
            "      mul_sat r21.y, r21.y, r10.w\n"
            "      add r21.y, r21.y, -r10.x\n"
            "      mad r21.y, r21.y, r9.w, r10.x\n"
            "";
        else b += std::string() +
            "      mov r21.y, r10.x\n"
            "";
        b += std::string() +
            "      dp3 r21.w, -r26, r24\n"
            "      add r21.w, r21.w, -r24.w\n"
            "      mul_sat r21.w, r21.w, r23.x\n"
            "      mul r21.y, r21.y, r21.w\n"
            "      rsq r21.z, r22.y\n"
            "      rcp r21.w, r21.z\n"
            "      mad r20.x, r21.w, r11.y, r11.x\n"
            "      mul r20.x, r20.x, r21.w\n"
            "      rcp r20.x, r20.x\n"
            "      mul r20.x, r20.x, r21.x\n"
            "      mul r20.x, r20.x, r21.y\n"
            + OcclusionVS() +
            "      mul r19.xyz, r25, r20.x\n"
            "      add r31.xyz, r31, r19\n"
            "      dp3 r19.w, r19, r10.x\n"
            "      mul_sat r19.w, r19.w, r29.w\n"
            "      mad r28.x, r23.z, r19.w, r28.x\n"
            "      add r28.z, r28.z, r10.x\n"
            "    endrep\n"
            "    min r28.x, r28.x, r10.x\n"
            "    add r28.x, r10.x, -r28.x\n"
            "    mul r17.xyz, " + vc + ", r28.x\n"
            "    mov r27, c1\n"
            "    add r27.x, r27.x, r27.z\n"
            "    add r27.x, r27.x, r27.z\n"
            "    add r27.x, r27.x, r27.z\n"
            "    add r27.x, r27.x, r27.z\n"
            "    add r27.x, r27.x, r27.z\n"
            "    add r27.x, r27.x, r27.z\n"
            "    add r27.x, r27.x, r27.z\n"
            "    add r27.x, r27.x, r27.z\n"
            "    add r27.x, r27.x, r27.z\n"
            "    add r27.x, r27.x, r27.z\n"
            "    texldl r22, r27, s0\n"
            "    mul r18.xyz, r31, r22.x\n"
            "    mul r18.xyz, r18, r10.z\n"
            "    add r21.xyz, r18, -r17\n"
            "    max r21.xyz, r21, r10.y\n"
            "    add r21.xyz, r17, r21\n"
            "    max r22.x, r21.x, r21.y\n"
            "    max r22.x, r22.x, r21.z\n"
            "    max r22.y, r17.x, r17.y\n"
            "    max r22.y, r22.y, r17.z\n"
            "    max r22.y, r22.y, r12.x\n"
            "    add r22.z, r22.x, -r22.y\n"
            "    max r22.z, r22.z, r10.y\n"
            "    add r22.w, r10.x, -r22.y\n"
            "    max r22.w, r22.w, r12.y\n"
            "    rcp r23.x, r22.w\n"
            "    mul r23.y, r22.z, r23.x\n"
            "    mul r23.y, r23.y, -r11.z\n"
            "    exp r23.y, r23.y\n"
            "    add r23.y, r10.x, -r23.y\n"
            "    mul r23.y, r23.y, r22.w\n"
            "    add r23.z, r22.x, -r22.z\n"
            "    add r23.z, r23.z, r23.y\n"
            "    max r23.w, r22.x, r12.y\n"
            "    rcp r23.w, r23.w\n"
            "    mul r23.z, r23.z, r23.w\n"
            "    mul r17.xyz, r21, r23.z\n"
            "    mov r27, c1\n"
            "    add r27.x, r27.x, r27.z\n"
            "    add r27.x, r27.x, r27.z\n"
            "    add r27.x, r27.x, r27.z\n"
            "    add r27.x, r27.x, r27.z\n"
            "    add r27.x, r27.x, r27.z\n"
            "    texldl r20, r27, s0\n"
            "    add r24.x, r10.x, -r20.x\n"
            "    mul r17.xyz, r17, r24.x\n"
            "    mad r17.xyz, r18, r20.x, r17\n"
            "    mov r17.w, " + vc + ".w\n"
            "    mov r27, c1\n"
            "    texldl r28, r27, s0\n"
            "    mad r17.yz, r17, -r28.z, r17\n"
            "    if_gt r28.y, c1.w\n"
            "";
        // Normal check (debug view): blue = its own normal, green = given one (injected), red = none
        // usable (distance-only light). Half scale, like the baked colour.
        if (vn.empty()) b += std::string() +
            "    mov r17.x, r10.x\n"
            "    mov r17.yz, r10.y\n"
            "";
        else if (injected) b += std::string() +
            "    add r17.x, r10.x, -r9.w\n"
            "    mov r17.y, r9.w\n"
            "    mov r17.z, r10.y\n"
            "";
        else b += std::string() +
            "    mov r17.xy, r10.y\n"
            "    mov r17.z, r10.x\n"
            "";
        b += std::string() +
            "    mul r17.xyz, r17, r10.z\n"
            "    endif\n"
            "    else\n"
            "    mov r17, " + vc + "\n"
            "    endif\n"
            "    mov " + oc + ", r17\n"
            "";
        return b;
    }

    inline bool BakedRule(std::string& src, std::string& why)
    {
        why = "unlit (no sun term)";
        if (g_family.compare(0, 6, "MapObj") != 0) return false;
        static const std::regex colourOut(R"(dcl_color (o\d+))"), colourIn(R"(dcl_color (v\d+))"), normalIn(R"(dcl_normal (v\d+))");
        std::smatch o, v, n;
        if (!std::regex_search(src, o, colourOut) || !std::regex_search(src, v, colourIn)) return false;
        const std::string oc = o[1].str(), vc = v[1].str();
        std::string vn = std::regex_search(src, n, normalIn) ? n[1].str() : std::string();
        const std::vector<Hit> copy = Lines(src, std::regex("^\\s*mov " + oc + ", " + vc + "\\s*$"));
        const std::vector<Hit> pos = Lines(src, kPosition);
        if (copy.size() != 1 || pos.size() != 1) { why = "baked: anchors"; return false; }
        if (pos[0].at > copy[0].at) { why = "baked: position after colour"; return false; }
        // No normal declared (shadow tier 0): declare one on the next free input register.
        bool injected = false;
        std::string declareNormal;
        if (vn.empty())
        {
            static const std::regex input(R"(dcl_\w+ v(\d+))");
            int highest = -1;
            for (std::sregex_iterator it(src.begin(), src.end(), input), stop; it != stop; ++it)
                highest = std::max(highest, std::stoi((*it)[1].str()));
            if (highest + 1 <= 15)
            {
                vn = "v" + std::to_string(highest + 1);
                declareNormal = "    dcl_normal " + vn + "\n";
                injected = true;
                ++g_normalsGiven;
            }
        }
        std::string::size_type end = src.find('\n', copy[0].at);
        end = end == std::string::npos ? src.size() : end + 1;
        src.replace(copy[0].at, end - copy[0].at, BakedBlock(vc, oc, vn, injected));
        src.insert(pos[0].at, "    mov r29.xyz, " + pos[0].reg + "\n");
        src.insert(src.find('\n') + 1, Prologue() + declareNormal);
        ++g_marked;
        return true;
    }

    inline bool EditVertex(std::string& src, std::string& why)
    {
        if (src.find("vs_3_0") == std::string::npos) { why = "not vs_3_0"; return false; }
        static const std::regex clash(R"(\b(i0|aL|c1|r9|r1[0-9]|r2[0-9]|r3[01])\b)");
        if (std::regex_search(src, clash)) { why = "uses a register the patch needs"; return false; }
        const std::vector<Hit> sun = Lines(src, kSun);
        if (sun.empty()) return BakedRule(src, why);
        const std::vector<Hit> pos = Lines(src, kPosition), colour = Lines(src, kColour);
        if (pos.size() != 1 || sun.size() != 1 || colour.size() != 1)
        {
            char b[96];
            std::snprintf(b, sizeof(b), "anchors: position %u, sun %u, colour %u",
                          static_cast<unsigned>(pos.size()), static_cast<unsigned>(sun.size()), static_cast<unsigned>(colour.size()));
            why = b;
            return false;
        }
        // Position and normal must be captured before the colour write reads our sum; their own order
        // varies (4 M2 variants compute the sun term first). Inserted from the bottom up, so the
        // earlier offsets stay valid.
        if (!(pos[0].at < colour[0].at && sun[0].at < colour[0].at)) { why = "anchors out of order"; return false; }
        const bool wmo = g_family.compare(0, 6, "MapObj") == 0;
        src.insert(colour[0].at, LampBlock(colour[0].reg, wmo ? IndoorSkip() : std::string()));
        const std::string savePos = "    mov r29.xyz, " + pos[0].reg + "\n", saveNormal = "    mov r30.xyz, " + sun[0].reg + "\n";
        if (sun[0].at > pos[0].at) { src.insert(sun[0].at, saveNormal); src.insert(pos[0].at, savePos); }
        else                       { src.insert(pos[0].at, savePos); src.insert(sun[0].at, saveNormal); }

        static const std::regex stockColour(R"(\bc(17|18|19|20)\b)");
        if (std::regex_search(src, stockColour))
        {
            for (int k = 0; k < 4; ++k)
                src = std::regex_replace(src, std::regex("\\bc" + std::to_string(17 + k) + "\\b"), "r" + std::to_string(9 + k));
            const std::string::size_type at = FirstInstruction(src);
            if (at == std::string::npos) { why = "no first instruction"; return false; }
            src.insert(at, StockSwitch());
        }
        src.insert(src.find('\n') + 1, Prologue());
        ++g_patched;
        return true;
    }

}
