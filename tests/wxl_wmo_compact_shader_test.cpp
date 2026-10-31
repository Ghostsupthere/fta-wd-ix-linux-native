// Text-level checks for the compact WMO vertex/pixel layouts (WmoShaderCompact.hpp). The real
// assembler and GPU checks run in tools/wxl_wmo_materials_gpu_probe.cpp.
#include "Engine/Source/ModernAssets/WxlWmo/WmoShaderCompact.hpp"
#include <cstdio>
#include <cstdlib>

using namespace wxl::modern::assets::wmo::shadertext;
static void Require(bool ok, const char* what) { if (!ok) { std::fprintf(stderr, "FAILED: %s\n", what); std::exit(1); } }
static int Count(const std::string& text, const std::string& needle) {
    int n = 0; for (size_t at = 0; (at = text.find(needle, at)) != std::string::npos; at += needle.size()) ++n; return n;
}

// Outputs o0..o9 as in the shadow-tier-2 specular shader: base uv .xy, texcoord1/2 .xyz, a constant COLOR1.
static const char* kVs =
    "vs_3_0\n"
    "    def c0, 1, 0, 0, 0\n"
    "    dcl_position v0\n    dcl_normal v1\n    dcl_color v2\n    dcl_texcoord v3\n"
    "    dcl_position o0\n    dcl_color o1\n    dcl_color1 o2\n    dcl_texcoord o3.xy\n"
    "    dcl_texcoord1 o4.xyz\n    dcl_texcoord2 o5.xyz\n    dcl_texcoord3 o6\n    dcl_texcoord4 o7\n"
    "    dcl_texcoord5 o8\n    dcl_fog o9.x\n"
    "    mov o0, v0\n    mov o1, v2\n    mov o2, c0.y\n    mov o3.xy, v3\n    mov o4.xyz, v1\n"
    "    mov o5.xyz, v1\n    mov o6, v0\n    mov o7, v0\n    mov o8, v0\n    mov o9.x, c0.x\n";

static const char* kPs =
    "ps_3_0\n"
    "    dcl_color v0\n    dcl_texcoord v1.xy\n    dcl_texcoord1 v2.xyz\n    dcl_texcoord2 v3.xyz\n"
    "    dcl_texcoord3 v4\n    dcl_fog v5.x\n    dcl_2d s0\n"
    "    texld r0, v1, s0\n    mul r0, r0, v0\n    mov oC0, r0\n";

int main() {
    // The supplied layout does not fit (it needs two more output registers than remain).
    VsMapping map{};
    const std::string supplied = BuildLayeredVsText(kVs, map);
    Require(supplied.empty() || MaxRegister(supplied, 'o') > 10, "supplied layout overflows the 11 output registers or is refused");

    CompactLayout layout;
    const std::string vs = BuildCompactVsText(kVs, layout);
    Require(!vs.empty(), "compact vertex layout built");
    Require(MaxRegister(vs, 'o') <= 10, "compact layout stays within o0..o10");
    Require(vs.find("dcl_color1 o") == std::string::npos, "COLOR1 output taken over");
    for (const auto& slot : layout.slots) Require(slot.usage > 0 && slot.comp >= 0 && slot.comp < 4, "every float has a slot");
    // Eight distinct (usage, component) destinations.
    for (size_t a = 0; a < 8; ++a)
        for (size_t b = a + 1; b < 8; ++b)
            Require(layout.slots[a].usage != layout.slots[b].usage || layout.slots[a].comp != layout.slots[b].comp, "slots are distinct");
    Require(Count(vs, "mov o") == 10 + 1 + 8, "base zw plus eight extra moves appended");
    Require(vs.find("dcl_texcoord o3.xyzw") != std::string::npos, "base UV widened for UV B");
    Require(vs.find("dcl_texcoord1 o4\n") != std::string::npos && vs.find("dcl_texcoord2 o5\n") != std::string::npos,
            "spare w components of the .xyz outputs are used");

    int tint = -1;
    std::array<unsigned, 8> slots{};
    const std::string ps = BuildCompactLayeredPsText(kPs, layout, tint, &slots);
    Require(!ps.empty(), "compact pixel patch built for a pixel shader that does not read COLOR1");
    Require(tint >= 0, "tint register assigned");
    Require(Count(ps, "dcl_texcoord14") == 0 && Count(ps, "dcl_texcoord15") == 0, "no stray placeholder usages");
    Require(Count(ps, "mov r") >= 8, "temporaries assembled from the interpolants");

    // A pixel shader that reads COLOR1 cannot pair with the taken-over register.
    const std::string readsColour1 = "ps_3_0\n    dcl_color v0\n    dcl_color1 v1\n    dcl_texcoord v2.xy\n    dcl_2d s0\n    texld r0, v2, s0\n    mov oC0, r0\n";
    Require(BuildCompactLayeredPsText(readsColour1, layout, tint).empty(), "pixel shader reading COLOR1 is refused");

    // Shaders that cannot fit are refused, not mangled.
    Require(BuildCompactVsText("vs_3_0\n    dcl_position v0\n    dcl_texcoord v3\n    dcl_position o0\n    dcl_texcoord o1.xy\n    mov o0, v0\n    mov o1.xy, v3\n", layout).empty(),
            "a shader with too little room (more than one new register needed) is refused");
    Require(BuildCompactVsText("ps_3_0\n", layout).empty(), "wrong profile refused");
    std::puts("PASS: compact WMO vertex/pixel layouts");
    return 0;
}
