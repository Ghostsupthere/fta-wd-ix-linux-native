// CPU checks for the instanced grass transform and the vertex-shader derivation. The GPU
// equivalence against the baked path runs in tools/wxl_grass_motion_gpu_probe.cpp.
#include "Engine/Source/ModernAssets/WxlGrass/GrassInstancing.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace wxl::grass::instancing;
static void Require(bool ok, const char* what) { if (!ok) { std::fprintf(stderr, "FAILED: %s\n", what); std::exit(1); } }
static float Near(float a, float b) { return std::fabs(a - b); }

// The engine's two bake branches (FillVertices), written out per vertex.
static void BakeYaw(const float p[3], float rotation, float scale, const float pos[3], float out[3]) {
    const float c = std::cos(rotation), s = std::sin(rotation);
    out[0] = (p[0] * c - p[1] * s) * scale + pos[0];
    out[1] = (p[1] * c + p[0] * s) * scale + pos[1];
    out[2] = p[2] * scale + pos[2];
}
// C3Vector * C33Matrix, row-vector convention (Engine/Source/Tempest/C33Matrix.cpp).
struct M3 { float a0, a1, a2, b0, b1, b2, c0, c1, c2; };
static void Mul(const float v[3], const M3& m, float out[3]) {
    out[0] = m.b0 * v[1] + m.c0 * v[2] + v[0] * m.a0;
    out[1] = m.b1 * v[1] + m.a1 * v[0] + m.c1 * v[2];
    out[2] = m.b2 * v[1] + m.a2 * v[0] + m.c2 * v[2];
}

int main() {
    srand(7);
    const auto random = [] { return float(rand()) / RAND_MAX * 2.0f - 1.0f; };
    for (int i = 0; i < 2000; ++i) {
        const float p[3] = {random() * 3, random() * 3, random() * 3}, pos[3] = {random() * 5000, random() * 5000, random() * 500};
        const float rotation = random() * 6.3f, scale = 0.5f + std::fabs(random()) * 2;
        float expected[3], got[3];
        GpuInstance plant = {};
        SetPlantTransform(YawImages(rotation), scale, pos, plant);
        BakeYaw(p, rotation, scale, pos, expected);
        Transform(plant, p, got);
        for (int k = 0; k < 3; ++k) Require(Near(expected[k], got[k]) < 0.01f, "non-tilting plant equals the baked vertex");

        // Tilting plant: any matrix, images taken from the unit vectors as the runtime does.
        const M3 m = {random(), random(), random(), random(), random(), random(), random(), random(), random()};
        AxisImages images;
        const float unit[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        for (int axis = 0; axis < 3; ++axis) Mul(unit[axis], m, images.image[axis]);
        SetPlantTransform(images, scale, pos, plant);
        float rotated[3]; Mul(p, m, rotated);
        for (int k = 0; k < 3; ++k) expected[k] = rotated[k] * scale + pos[k];
        Transform(plant, p, got);
        for (int k = 0; k < 3; ++k) Require(Near(expected[k], got[k]) < 0.01f, "tilting plant equals the baked vertex");
    }

    // Shader derivation on a stock-shaped grass vertex shader.
    const std::string stock =
        "vs_3_0\n    def c13, 0, 1, 0, 0\n    dcl_position v0\n    dcl_normal v1\n    dcl_color v2\n    dcl_texcoord v3\n"
        "    dcl_position o0\n    dcl_color o1\n    dcl_texcoord o3.xy\n"
        "    mul r0, c1, v0.y\n    mad r0, v0.x, c0, r0\n    mad r0, v0.z, c2, r0\n    add r0, r0, c3\n"
        "    mov o0, r0\n    mov o1, v2\n    mov o3.xy, v3\n    mov r9.x, v0.w\n    mov r9.y, v10\n";
    std::string derived, error;
    Require(MakeInstancedGrassText(stock, derived, error), error.c_str());
    Require(derived.find("dcl_texcoord1 v4") != std::string::npos && derived.find("dcl_texcoord3 v6") != std::string::npos, "instance rows declared");
    Require(derived.find("dp4 r31.z, r30, v6") != std::string::npos, "position computed from the rows");
    Require(derived.find("mul r0, c1, r31.y") != std::string::npos && derived.find("mad r0, r31.x, c0, r0") != std::string::npos, "v0 reads become r31");
    Require(derived.find("mov r9.x, r31.w") != std::string::npos, "v0.w reads become r31.w");
    Require(derived.find("mov r9.y, v10") != std::string::npos, "v10 is not mistaken for v0");
    Require(derived.find("mov o1, v2") != std::string::npos && derived.find("mov o3.xy, v3") != std::string::npos, "normal, colour and uv inputs untouched");
    Require(derived.find("dcl_position v0") != std::string::npos, "v0 stays declared (the model position)");
    // The derivation inserts after the last declaration, before any instruction.
    Require(derived.find("mov r30, v0") < derived.find("mul r0"), "derivation runs first");

    Require(!MakeInstancedGrassText("vs_3_0\n    dcl_position v0\n    dcl_normal v1\n    dcl_color v2\n    dcl_texcoord v3\n    mov r31, v0\n", derived, error), "r31 users refused");
    Require(!MakeInstancedGrassText("vs_3_0\n    dcl_position v0\n    dcl_texcoord v3\n    mov r0, v0\n", derived, error), "other layouts refused");
    Require(!MakeInstancedGrassText("vs_3_0\n    dcl_position v0\n    dcl_normal v1\n    dcl_color v2\n    dcl_texcoord v3\n    dcl_texcoord1 v4\n", derived, error), "extra inputs refused");
    std::puts("PASS: instanced grass transform and shader derivation");
    return 0;
}
