// Grass-motion shader text and settings, adapted from wxl-render-modern
// `src/grass/GrassMotion.cpp` and `GrassSettings.hpp`. Copyright (C) 2026 WarcraftXL, GPLv3.
// The injection block and the two anchors are unchanged; the Windows hooks, runtime shader
// assembly and device-vtable detours are not imported (the native client patches the
// engine's grass vertex shader offline, see tools/build-wmo-shader-pack.py).
#pragma once
#include <cstring>
#include <string>

namespace wxl::grass
{
    // Constants live in the engine-free registers c14..c22 (point-light registers of the
    // dormant permutations), written once per frame by the runtime.
    namespace text
    {
        inline constexpr const char* kMotionInject =
                    "    mov r2.x, c22.x\n"
                    "    if_gt r2.x, c13.x\n"          // Uniform off switch avoids wave/repulsion work.
                    "    dp3 r2.x, c0, r0\n"            // worldPos.x = R^T row0 . viewPos
                    "    dp3 r2.y, c1, r0\n"            // worldPos.y
                    "    add r2.xy, r2, c14\n"          // + cam.xy -> worldPos.xy
                    "    mov r4.xy, r2\n"               // save worldPos.xy for the wind phases
                    "    dp3 r3.z, c2, r0\n"            // worldPos.z = R^T row2 . viewPos
                    "    add r3.z, r3.z, c14.z\n"       // + cam.z -> worldPos.z
                    "    add r3.z, r3.z, -c16.z\n"      // height above the player's ground
                    "    add r3.z, r3.z, -c15.z\n"      // - minHeight
                    "    mul_sat r4.z, r3.z, c15.x\n"   // repulsion cone weight
                    "    add r2.xy, r2, -c16\n"         // d = worldPos.xy - player.xy
                    "    mul r2.z, r2.x, r2.x\n"
                    "    mad r2.z, r2.y, r2.y, r2.z\n"  // dist^2
                    "    add r2.z, r2.z, c15.w\n"       // + eps
                    "    rsq r2.w, r2.z\n"              // invDist
                    "    mul r3.x, r2.z, r2.w\n"        // dist
                    "    mul_sat r3.y, r3.x, c15.y\n"   // t = saturate(dist/R)
                    "    mad r3.y, c17.x, r3.y, c17.y\n"// strength = center + (edge-center)*t
                    "    slt r3.z, r3.x, c17.z\n"       // inside = dist < R
                    "    mul r3.y, r3.y, r3.z\n"        // strength *= inside
                    "    mul r3.y, r3.y, r4.z\n"        // * cone weight
                    "    mul r3.y, r3.y, r2.w\n"        // * invDist (d * this = unit dir * magnitude)
                    "    mul r2.xy, r2, r3.y\n"         // repulsion offset (world)
                    "    add r5.z, c13.y, -v3.y\n"      // wind weight = 1 - uv.y (0 at the blade base)
                    "    add r5.z, r5.z, c21.z\n"       // - anchor: the base band never moves
                    "    mul_sat r5.z, r5.z, c21.w\n"   // renormalize over the moving span, clamped
                    "    mul r5.z, r5.z, r5.z\n"        // squared: bending profile, stiff base to loose tip
                    "    mov r5.w, c13.y\n"             // 1 (an instruction reads at most one c# register)
                    "    mad r5.w, r0.z, c20.w, r5.w\n" // 1 + viewZ * distanceFade
                    "    rcp r5.w, r5.w\n"
                    "    mul r5.z, r5.z, r5.w\n"        // wind weight *= view-distance fade
                    "    mul r3.x, r4.x, c20.x\n"       // per-blade hash from worldPos
                    "    mad r3.x, r4.y, c20.y, r3.x\n"
                    "    frc r3.x, r3.x\n"              // rand in [0,1)
                    "    mad r2.z, r3.x, c21.y, c21.x\n"// amplitude scale = base + span*rand
                    "    mul r5.z, r5.z, r2.z\n"        // wind weight *= amplitude variance
                    "    mul r3.x, r3.x, c20.z\n"       // jitter angle
                    "    mul r3.y, r4.x, c18.z\n"
                    "    mad r3.y, r4.y, c18.w, r3.y\n" // worldPos . k1
                    "    add r3.y, r3.y, r3.x\n"        // + jitter
                    "    add r3.y, r3.y, c17.w\n"       // + phase1 -> angle1
                    "    mul r3.z, r4.x, c19.z\n"
                    "    mad r3.z, r4.y, c19.w, r3.z\n" // worldPos . k2
                    "    add r3.z, r3.z, r3.x\n"        // + jitter
                    "    add r3.z, r3.z, c14.w\n"       // + phase2 -> angle2
                    "    sincos r4.xy, r3.y\n"          // r4.y = sin(angle1) (worldPos.xy consumed)
                    "    add r4.w, r4.y, c16.w\n"       // + lean (constant downwind bias)
                    "    sincos r5.xy, r3.z\n"          // r5.y = sin(angle2); r5.zw untouched
                    "    mul r3.zw, c18.xxxy, r4.wwww\n"     // A1 * (sin1 + lean)
                    "    mad r3.zw, c19.xxxy, r5.yyyy, r3\n" // + A2 * sin2
                    "    mul r3.zw, r3, r5.zzzz\n"           // * wind weight
                    "    add r2.xy, r2, r3.zwzw\n"      // total world offset = repulsion + wind
                    "    slt r4.x, c13.x, v2.w\n"       // alpha > 0
                    "    slt r4.y, v2.w, c13.y\n"       // alpha < 1
                    "    mul r4.x, r4.x, r4.y\n"        // interior alpha = rigid marker
                    "    add r4.x, c13.y, -r4.x\n"      // endpoint alpha = flexible
                    "    mul r2.xy, r2, r4.x\n"         // rigid props receive no motion
                    "    mul r3.xyz, c0, r2.x\n"        // rotate back into view: R * offset
                    "    mad r3.xyz, c1, r2.y, r3.xyz\n"
                    "    add r0.xyz, r0, r3\n"          // viewPos += view-space offset
                    "    endif\n";

        // Patches one live (group 0) grass vertex shader's disassembly: the motion block after
        // the view-space transform, and the alpha-endpoint restore for rigid-prop markers.
        // Returns "" when an anchor is missing (the shader is then left stock).
        inline std::string BuildGrassMotionText(const std::string& source)
        {
            std::string src = source;
            size_t at = src.find("add r0, r0, c3");
            if (at == std::string::npos) return {};
            at = src.find('\n', at);
            if (at == std::string::npos) return {};
            src.insert(at + 1, kMotionInject);
            constexpr const char* kAlphaPass = "mov o1.w, v2.w";
            at = src.find(kAlphaPass);
            if (at == std::string::npos) return {};
            src.replace(at, std::strlen(kAlphaPass),
                "add r2.x, v2.w, v2.w\n"
                "    sge o1.w, r2.x, c13.y");
            return src;
        }
    }

    struct WindSettings
    {
        bool  enabled; float directionDeg, speed, amplitude, wavelength, crossAmplitude, crossWavelength,
              crossAngleDeg, lean, variance, distanceFade, anchor;
    };
    struct PhysicsSettings
    {
        bool  enabled; float radius, forceCenter, forceEdge, coneHeight, minHeight;
    };
}
