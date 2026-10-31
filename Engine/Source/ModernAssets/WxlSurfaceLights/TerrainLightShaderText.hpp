#pragma once
// Shader text patches that add the surface lights to the client's terrain shaders. Imported from the
// supplied wxl-living-azeroth features/TerrainLights.cpp and TerrainLighting.cpp (Morfium's Living
// Azeroth, GPL-3.0-or-later) without changes to the assembly or the edit rules. Only the wrapper
// differs: the Windows module recognised terrain shaders by the address of the client's shader
// tables when they were created; the offline pack builder knows them by name instead
// (kTerrainVertex / kTerrainPixel below, from the same tables). The pixel block reads the shared
// light texture on pixel sampler 13 and the grid in PS c206/c212.
#include "LightPool.hpp"

#include <cstdio>
#include <regex>
#include <string>

namespace wxl::surfacelights::terrainshader
{
    constexpr int kTextureWidth = 1024;   // = modelshader::kWidth
    // The client's terrain shader sets (BLS names under Shaders\Vertex\vs_3_0 and Shaders\Pixel\ps_3_0):
    // the supplied tables were 0x80 vertex permutations of "Terrain" and the pixel sets Terrain0
    // (3), Terrain0_env (1), Terrain1 (0x20, Terrain1w overlaps), Terrain2 / _pcf (0x20) and
    // Terrain3 / _pcf (0x60).
    inline const char* const kTerrainVertex = "Terrain";
    inline const char* const kTerrainPixel[] = {"Terrain0", "Terrain0_env", "Terrain1", "Terrain1w",
                                                "Terrain2", "Terrain2_pcf", "Terrain3", "Terrain3_pcf"};
    // World position uses TEXCOORD7; pixel-light registers are checked before editing.
    inline unsigned g_packed = 0;            // vertex variants whose unread view-position output carries it
    inline unsigned g_switchedVariants = 0;  // vertex variants with point lights switched through VS c250.x

    inline std::string PixelBlock(const std::string& r, std::string& defs)
    {
        // c207 = (1 / W, 0, 0.5 / W, 0): texel steps along a row; c208 = (2, -1, 1 / 1.5, 0);
        // c209 = (0.7, 1 / ln 2, 0.05, 0.03); c210 = (1.5: header row + texel centre, 0, 0, 0);
        // c211 = (the knee's minimum 0.8, 0.001).
        // ps_3_0 reads at most one constant register per instruction: 0.7 and 0.03 share c209.
        // Same light as the surface cover's (SurfaceCover.cpp, Lamps): size ~1 yd (falloff on
        // sqrt(d^2 + 1)), like the client's 1 / (0.7 d + 0.03 d^2), windowed to 0 at the radius,
        // wrapped Lambert. The stock lighting is the vertex colour v0 (x 2 in the stock PS); ours fills
        // the headroom it leaves softly: l' = l + (1 - l)(1 - e^-lamps), applied as colour x l' / l.
        // Only the pixel's grid cell's lights: its cell from the world position v9 (clamped to the
        // grid: a light past its radius adds 0), entries from cell x lights per cell, each pointing
        // at its light's first texel; an empty entry or the cell's capacity ends the loop.
        // c213 = (4, 7.999, 2, 0.25), c214 = (0, 1, 2, 3): the occlusion lookup's steps.
        char d[448];
        std::snprintf(d, sizeof(d),
                      "    def c207, %.9g, 0, %.9g, 0\n    def c208, 2, -1, 0.666666687, 0\n"
                      "    def c209, 0.7, 1.44269502, 0.05, 0.03\n    def c210, 1.5, 0, 0, 0\n    def c211, 0.8, 0.001, 0, 0\n"
                      "    def c213, 4, 7.99900007, 2, 0.25\n    def c214, 0, 1, 2, 3\n"
                      "    defi i0, %d, 0, 0, 0\n    dcl_2d s13\n",
                      1.0 / kTextureWidth, 0.5 / kTextureWidth, kMaxPerCellCap);
        defs = d;
        return
            // Native terrain triangle normal: no moisture/climate grid or extra texture pass.
            "\n    dsx r25.xyz, v9"
            "\n    dsy r26.xyz, v9"
            "\n    crs r24.xyz, r25, r26"
            "\n    cmp r24.xyz, r24.z, r24, -r24"
            "\n    dp3 r24.w, r24, r24"
            "\n    max r24.w, r24.w, c211.y"
            "\n    rsq r24.w, r24.w"
            "\n    mul r24.xyz, r24, r24.w"
            // The cell: r22.y = its first entry.
            "\n    add r22.xy, v9, -c206"
            "\n    mul r22.xy, r22, c206.z"
            "\n    frc r22.zw, r22.xyxy"
            "\n    add r22.xy, r22, -r22.zwzw"
            "\n    max r22.xy, r22, c202.y"
            "\n    min r22.xy, r22, c212.z"
            "\n    mad r22.x, r22.y, c206.w, r22.x"
            "\n    mul r22.y, r22.x, c212.y"
            "\n    mov r23.xyz, c202.y"
            "\n    mov r22.x, c202.y"
            "\n    mov r21, c202.y"
            "\n    rep i0"
            "\n      break_ge r22.x, c212.y"
            // Entry e = first + i at (column, 1 + row) of the W-wide rows.
            "\n      add r18.x, r22.y, r22.x"
            "\n      mul r18.x, r18.x, c207.x"
            "\n      frc r18.y, r18.x"
            "\n      add r18.z, r18.x, -r18.y"
            "\n      add r21.x, r18.y, c207.z"
            "\n      add r18.z, r18.z, c210.x"
            "\n      mul r21.y, r18.z, c212.x"
            "\n      texldl r20, r21, s13"
            "\n      break_lt r20.z, c202.z"
            "\n      mov r21.xy, r20"
            "\n      texldl r20, r21, s13"
            "\n      add r21.x, r21.x, c207.x"
            "\n      texldl r19, r21, s13"
            "\n      add r21.x, r21.x, c207.x"
            "\n      texldl r11, r21, s13"
            "\n      add r21.x, r21.x, c207.x"
            "\n      texldl r10, r21, s13"
            "\n      add r20.xyz, r20, -v9"
            "\n      dp3 r18.x, r20, r20"
            "\n      add r18.y, r18.x, c202.x"
            "\n      max r18.x, r18.x, c211.y"
            "\n      rsq r18.z, r18.x"
            "\n      rcp r18.w, r18.z"
            "\n      mul_sat r17.x, r18.w, r20.w"
            "\n      mad r17.x, r17.x, -r17.x, c202.x"
            "\n      mul r17.x, r17.x, r17.x"
            // The falloff shape: the window to the light's power (colour texel .w).
            "\n      pow r17.x, r17.x, r19.w"
            "\n      mul r20.xyz, r20, r18.z"
            "\n      dp3 r17.y, r24, r20"
            "\n      add r17.y, r17.y, c202.z"
            "\n      mul_sat r17.y, r17.y, c208.z"
            // The spot cone: saturate((cos to the axis - cos outer) x scale); point lights: cos outer -2.
            "\n      dp3 r17.w, -r20, r11"
            "\n      add r17.w, r17.w, -r11.w"
            "\n      mul_sat r17.w, r17.w, r10.x"
            "\n      mul r17.y, r17.y, r17.w"
            "\n      rsq r17.z, r18.y"
            "\n      rcp r17.w, r17.z"
            "\n      mad r16.x, r17.w, c209.w, c209.x"
            "\n      mul r16.x, r16.x, r17.w"
            "\n      rcp r16.x, r16.x"
            "\n      mul r16.x, r16.x, r17.x"
            "\n      mul r16.x, r16.x, r17.y"
            // Baked occlusion (lights::Bake; ModelLights.cpp OcclusionVS has the same steps): direction
            // light -> pixel on the 8 x 8 octahedral map, the tile's texel and component (4th texel
            // .y = tile / tiles per row, -1 = none; c212.w = the region's first row + 0.5), and no light
            // past the stored reach (+ 0.5 yd, soft over 0.5 yd).
            "\n      if_ge r10.y, c202.y"
            "\n      add r11.x, r20_abs.x, r20_abs.y"
            "\n      add r11.x, r11.x, r20_abs.z"
            "\n      rcp r11.x, r11.x"
            "\n      mul r11.xyz, -r20, r11.x"
            "\n      cmp r12.xy, r11, c202.x, -c202.x"
            "\n      add r12.zw, c202.x, -r11_abs.xxyx"
            "\n      mul r12.xy, r12, r12.zw"
            "\n      add r12.xy, r12, -r11"
            "\n      cmp r12.z, r11.z, c202.y, c202.x"
            "\n      mad r11.xy, r12, r12.z, r11"
            "\n      add r11.xy, r11, c202.x"
            "\n      mul r11.xy, r11, c213.x"
            "\n      min r11.xy, r11, c213.y"
            "\n      frc r12.xy, r11"
            "\n      add r11.xy, r11, -r12"
            "\n      mul r12.x, r11.x, c213.w"
            "\n      frc r12.y, r12.x"
            "\n      add r12.x, r12.x, -r12.y"
            "\n      mul r12.y, r12.y, c213.x"
            "\n      mad r12.x, r11.y, c213.z, r12.x"
            "\n      frc r11.z, r10.y"
            "\n      add r11.w, r10.y, -r11.z"
            "\n      add r12.x, r12.x, c202.z"
            "\n      mad r21.x, r12.x, c207.x, r11.z"
            "\n      add r11.w, r11.w, c212.w"
            "\n      mul r21.y, r11.w, c212.x"
            "\n      texldl r13, r21, s13"
            "\n      add r14, r12.y, -c214"
            "\n      add r14, -r14_abs, c202.z"
            "\n      cmp r14, r14, c202.x, c202.y"
            "\n      dp4 r13.x, r13, r14"
            "\n      add r13.x, r13.x, -r18.w"
            "\n      add r13.x, r13.x, r13.x"
            "\n      add_sat r13.x, r13.x, c202.x"
            "\n      mul r16.x, r16.x, r13.x"
            "\n      endif"
            // Indoor lights (4th texel .w = 1, only while the panel's switch is on) stay off the terrain.
            "\n      add r10.w, c202.x, -r10.w"
            "\n      mul r16.x, r16.x, r10.w"
            "\n      mad r23.xyz, r19, r16.x, r23"
            "\n      add r22.x, r22.x, c202.x"
            "\n    endrep"
            // l = min(2 v0, 1) (the stock lighting); t = l + lamps, added like the client's own lights;
            // above a knee (max(0.8, brightest channel of l)) the brightest channel of t is compressed
            // smoothly (knee + room (1 - e^(-over / room))) and all three scaled alike, so the flame's
            // hue stays; without lamps t = l. Then colour x t / max(l, 0.05) (exp is 2^x).
            "\n    add r15.xyz, v0, v0"
            "\n    min r15.xyz, r15, c202.x"
            "\n    add r14.xyz, r15, r23"
            "\n    max r13.x, r14.x, r14.y"
            "\n    max r13.x, r13.x, r14.z"
            "\n    max r13.y, r15.x, r15.y"
            "\n    max r13.y, r13.y, r15.z"
            "\n    max r13.y, r13.y, c211.x"
            "\n    add r13.z, r13.x, -r13.y"
            "\n    max r13.z, r13.z, c202.y"
            "\n    add r13.w, c202.x, -r13.y"
            "\n    max r13.w, r13.w, c211.y"
            "\n    rcp r12.x, r13.w"
            "\n    mul r12.y, r13.z, r12.x"
            "\n    mul r12.y, r12.y, -c209.y"
            "\n    exp r12.y, r12.y"
            "\n    add r12.y, c202.x, -r12.y"
            "\n    mul r12.y, r12.y, r13.w"
            "\n    add r12.z, r13.x, -r13.z"
            "\n    add r12.z, r12.z, r12.y"
            "\n    max r12.w, r13.x, c211.y"
            "\n    rcp r12.w, r12.w"
            "\n    mul r12.z, r12.z, r12.w"
            "\n    mul r14.xyz, r14, r12.z"
            "\n    max r15.xyz, r15, c209.z"
            "\n    rcp r13.x, r15.x"
            "\n    rcp r13.y, r15.y"
            "\n    rcp r13.z, r15.z"
            "\n    mul r14.xyz, r14, r13"
            "\n    mul " + r + ".xyz, " + r + ", r14";
    }


    inline void EditTerrainLightSwitch(std::string& src)
    {
        // The stock point-light code (terrain VS dumps 2026-10-04): the 3 lights' colours c29, c32, c35,
        // each read once as `mul rN.xyz, rM.c, cK`. Temps r10..r12 and c250 are free in every variant
        // (they use up to r6 and c48).
        static const std::regex colourRead(R"(\bc(29|32|35)\b)");
        if (!std::regex_search(src, colourRead)) return;
        src = std::regex_replace(src, std::regex(R"(\bc29\b)"), "r10");
        src = std::regex_replace(src, std::regex(R"(\bc32\b)"), "r11");
        src = std::regex_replace(src, std::regex(R"(\bc35\b)"), "r12");
        // After the declarations: the colours, scaled by the switch.
        std::string::size_type at = src.find('\n');
        while (at != std::string::npos && at + 1 < src.size())
        {
            const std::string::size_type end = src.find('\n', at + 1);
            std::string line = src.substr(at + 1, end == std::string::npos ? std::string::npos : end - at - 1);
            const auto first = line.find_first_not_of(" \t");
            if (first != std::string::npos && line.compare(first, 3, "dcl") != 0 && line.compare(first, 3, "def") != 0 && line.compare(first, 2, "//") != 0) break;
            at = end;
        }
        if (at == std::string::npos) return;
        src.insert(at + 1,
                   "    mov r10, c29\n    mul r10.xyz, r10, c250.x\n"
                   "    mov r11, c32\n    mul r11.xyz, r11, c250.x\n"
                   "    mov r12, c35\n    mul r12.xyz, r12, c250.x\n");
        ++g_switchedVariants;
    }


    inline std::string::size_type FirstInstructionTerrain(const std::string& src)
    {
        // After the version line, the first line that isn't a declaration, a definition or empty.
        std::string::size_type at = src.find('\n');
        while (at != std::string::npos && at + 1 < src.size())
        {
            const std::string::size_type end = src.find('\n', at + 1);
            std::string line = src.substr(at + 1, end == std::string::npos ? std::string::npos : end - at - 1);
            const auto first = line.find_first_not_of(" \t");
            if (first != std::string::npos)
            {
                line = line.substr(first);
                if (line.rfind("dcl", 0) != 0 && line.rfind("def", 0) != 0 && line.rfind("//", 0) != 0) return at + 1;
            }
            at = end;
        }
        return std::string::npos;
    }


    inline bool EditTerrainVertex(std::string& src, std::string& why)
    {
        if (src.find("vs_3_0") == std::string::npos) { why = "not vs_3_0"; return false; }
        if (src.find("dcl_position v0") == std::string::npos) { why = "no position input"; return false; }
        if (src.find("dcl_texcoord7") != std::string::npos) { why = "texcoord7 in use"; return false; }
        if (std::regex_search(src, std::regex(R"(\b(r1[0-2]|c250)\b)")))
        { why = "light-switch registers in use"; return false; }
        const auto at = FirstInstructionTerrain(src);
        if (at == std::string::npos) { why = "no instructions"; return false; }
        // Output registers in use.
        static const std::regex outputDcl(R"(dcl_\w+ o(\d+))");
        int highest = -1, declared = 0;
        for (auto it = std::sregex_iterator(src.begin(), src.end(), outputDcl); it != std::sregex_iterator(); ++it)
        {
            ++declared;
            const int n = std::stoi((*it)[1].str());
            highest = n > highest ? n : highest;
        }
        // Full world position for surface-light direction, attenuation and terrain normal.
        if (declared < 11)
        {
            const std::string o = "o" + std::to_string(highest + 1);
            src.insert(at, "    mov " + o + ".xyz, v0.xyz\n");
            src.insert(src.find('\n') + 1, "    dcl_texcoord7 " + o + ".xyz\n");
            EditTerrainLightSwitch(src); // the stock point lights through a switch
            return true;
        }
        // All outputs in use: take over the unread view-position output (texcoord3 .xyz).
        static const std::regex viewDcl(R"(dcl_texcoord3 (o\d+)\.xyz\b)");
        std::smatch m;
        if (!std::regex_search(src, m, viewDcl)) { why = "all outputs in use and no view-position output to take over"; return false; }
        const std::string o = m[1].str();
        const std::regex viewWrite("\n([ \t]*)mov " + o + R"(\.xyz, r\d+[ \t]*\n)");
        std::smatch w;
        if (!std::regex_search(src, w, viewWrite)) { why = "view-position output written unexpectedly"; return false; }
        src.replace(static_cast<size_t>(w.position(0)), static_cast<size_t>(w.length(0)), "\n" + w[1].str() + "mov " + o + ".xyz, v0.xyz\n");
        src.replace(static_cast<size_t>(m.position(0)), static_cast<size_t>(m.length(0)), "dcl_texcoord7 " + o + ".xyz");
        EditTerrainLightSwitch(src);
        ++g_packed;
        return true;
    }

    inline bool EditTerrainPixel(std::string& src, std::string& why)
    {
        if (src.find("ps_3_0") == std::string::npos) { why = "not ps_3_0"; return false; }
        if (std::regex_search(src, std::regex(R"(\b(v9|s13|i0|r(1[0-9]|2[0-9]|3[01])|c(20[0-9]|21[0-4]))\b)")))
        { why = "surface-light registers in use"; return false; }
        // Terrain0's reflection variants fold a colour multiplier into the fog subtraction.
        // Insert before that operation so both the existing colour and our lights retain it.
        static const std::regex fog(R"((\n[ \t]*)(?:add|mad) (r\d+)\.xyz, \2, (?:\2\.w, )?-c2[ \t]*\n[ \t]*mad oC0\.xyz, v\d+\.x, \2, c2)");
        std::smatch m;
        if (!std::regex_search(src, m, fog)) { why = "no fog ending"; return false; }
        const std::string r = m[2].str();
        // Point lights modify the surface colour before the native fog blend.
        std::string lightDefs;
        const std::string lightBlock = PixelBlock(r, lightDefs);
        src.insert(static_cast<size_t>(m.position(0)), lightBlock);
        src.insert(src.find('\n') + 1, "    def c202, 1, 0, 0.5, 0\n" + lightDefs + "    dcl_texcoord7 v9.xyz\n");
        return true;
    }


}
