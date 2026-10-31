// Synthetic, hidden-window probe for the patched grass-motion vertex shaders. It draws a
// blade with the engine's own live grass vertex shader and its generated patch on the real
// dxvk-native backend. It does not start the game or alter client data.
//
//   wxl-grass-motion-gpu-probe <grass_shaders.pack> <dir with the extracted DetailDoodad_*.vs> [grass_instanced.pack]
//
// With the instanced pack it also draws the same blades through the hardware-instanced path
// (model vertices + a 64-byte plant record) and compares them with the baked path.
#include <d3d9.h>
#include <SDL3/SDL.h>
#include <dlfcn.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "Engine/Source/ModernAssets/WxlWmo/WmoShaderPack.h"
#include "Engine/Source/ModernAssets/WxlGrass/GrassInstancing.hpp"
using namespace wxl::wmo;

static void Check(HRESULT r, const char* what) { if (FAILED(r)) { std::fprintf(stderr, "%s: %08x\n", what, unsigned(r)); std::exit(1); } }
static void Require(bool v, const char* what) { if (!v) { std::fprintf(stderr, "FAILED: %s\n", what); std::exit(1); } }
static std::vector<uint8_t> ReadAll(const std::string& path) {
    std::vector<uint8_t> out; FILE* f = std::fopen(path.c_str(), "rb"); Require(f != nullptr, path.c_str());
    uint8_t b[8192]; size_t n; while ((n = std::fread(b, 1, sizeof b, f)) > 0) out.insert(out.end(), b, b + n);
    std::fclose(f); return out;
}
struct Vertex { float x, y, z, nx, ny, nz; DWORD colour; float u, v; };   // CGxVertexPNCT, 36 bytes
static_assert(sizeof(Vertex) == 36, "grass vertex");

constexpr unsigned kSize = 64;

static std::vector<DWORD> Pixels(IDirect3DDevice9* dev) {
    IDirect3DSurface9 *target = nullptr, *cpu = nullptr;
    Check(dev->GetRenderTarget(0, &target), "target");
    Check(dev->CreateOffscreenPlainSurface(kSize, kSize, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &cpu, nullptr), "cpu");
    Check(dev->GetRenderTargetData(target, cpu), "read");
    D3DLOCKED_RECT l = {}; Check(cpu->LockRect(&l, nullptr, D3DLOCK_READONLY), "lock");
    std::vector<DWORD> p(kSize * kSize);
    for (unsigned y = 0; y < kSize; ++y) for (unsigned x = 0; x < kSize; ++x) p[y * kSize + x] = reinterpret_cast<const DWORD*>(static_cast<uint8_t*>(l.pBits) + y * l.Pitch)[x] & 0xFFFFFF;
    cpu->UnlockRect(); cpu->Release(); target->Release(); return p;
}
// Mean x (in pixels) of lit pixels in rows [y0, y1); -1 when none.
static float Centroid(const std::vector<DWORD>& p, unsigned y0, unsigned y1) {
    double sum = 0; unsigned n = 0;
    for (unsigned y = y0; y < y1; ++y) for (unsigned x = 0; x < kSize; ++x) if (p[y * kSize + x]) { sum += x; ++n; }
    return n ? float(sum / n) : -1.0f;
}

struct Params {
    float windAmp = 0, physicsCenter = 0, bladeX = 0, motion = 1; uint8_t alpha = 255;
    float phase = 0.0f;
};

struct Rig { IDirect3DDevice9* dev; IDirect3DVertexDeclaration9* decl; IDirect3DPixelShader9* ps; IDirect3DTexture9* white; IDirect3DVertexDeclaration9* instancedDecl = nullptr; };
namespace gi = wxl::grass::instancing;
struct Placement { float rotation = 0, scale = 1, x = 0; };
constexpr int kBladeRows = 13;

static std::vector<DWORD> Render(Rig& rig, const std::vector<uint32_t>& bytecode, const Params& q, bool instanced = false, const Placement* place = nullptr) {
    IDirect3DDevice9* dev = rig.dev;
    IDirect3DVertexShader9* vs = nullptr; Check(dev->CreateVertexShader(bytecode.data(), &vs), "vertex shader");
    const DWORD white = D3DCOLOR_ARGB(q.alpha, 255, 255, 255);
    // Blade along model z (screen up): base z=0 (v=1) to tip z=1.2 (v=0), 0.12 wide, at x = bladeX.
    // Segmented so the shader's per-vertex anchor band is not blurred by interpolation.
    constexpr int kRows = kBladeRows;
    // The model: a blade at the origin. The baked path places it on the CPU, as the engine's bake does.
    gi::GpuInstance plant = {};
    if (place) {
        const float position[3] = {place->x, 0, 0};
        gi::SetPlantTransform(gi::YawImages(place->rotation), place->scale, position, plant);
    } else {
        const float position[3] = {q.bladeX, 0, 0};
        gi::SetPlantTransform(gi::YawImages(0), 1, position, plant);
    }
    plant.normal[0] = plant.normal[1] = 0; plant.normal[2] = 1; plant.color = white;
    Vertex blade[kRows * 2];
    gi::GpuVertex model[kRows * 2];
    for (int row = 0; row < kRows; ++row) {
        const float z = 0.1f * row, v = 1.0f - z / 1.2f;
        const float left[3] = {-.06f, 0, z}, right[3] = {.06f, 0, z};
        model[row * 2] = {{left[0], left[1], left[2]}, {0, v}};
        model[row * 2 + 1] = {{right[0], right[1], right[2]}, {1, v}};
        float a[3], b[3]; gi::Transform(plant, left, a); gi::Transform(plant, right, b);
        blade[row * 2] = {a[0], a[1], a[2], 0, 0, 1, white, 0, v};
        blade[row * 2 + 1] = {b[0], b[1], b[2], 0, 0, 1, white, 1, v};
    }
    Check(dev->SetVertexDeclaration(instanced ? rig.instancedDecl : rig.decl), "declaration");
    Check(dev->SetVertexShader(vs), "vs"); Check(dev->SetPixelShader(rig.ps), "ps");
    Check(dev->SetTexture(0, rig.white), "texture");
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT); dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    const float fogColour[4] = {0, 0, 0, 1}; dev->SetPixelShaderConstantF(2, fogColour, 1);
    auto set = [&](unsigned reg, float a, float b, float c, float d) { const float v[4] = {a, b, c, d}; dev->SetVertexShaderConstantF(reg, v, 1); };
    // View rotation maps model (x, y, z) to view (x, z, y): screen up is world up; c3 drops the base to y=-0.8.
    set(0, 1, 0, 0, 0); set(1, 0, 0, 1, 0); set(2, 0, 1, 0, 0); set(3, 0, -0.8f, 0.5f, 1);   // w must be 1: the shader multiplies c7 by r0.w
    set(4, 1, 0, 0, 0); set(5, 0, 1, 0, 0); set(6, 0, 0, 0.1f, 0); set(7, 0, 0, 0.5f, 1);   // orthographic projection
    set(8, 0, 1, 1, 0); set(9, 0, 1, 0, 0); set(10, 0, 0, 1, 0); set(11, 1, 1, 1, 0); set(12, 0, 0, 0, 0);
    // Motion constants c14..c22 (same layout as WxlGrassMotion.cpp).
    const float kTwoPi = 6.2831853f, wavelength = 3.0f, k = kTwoPi / wavelength;
    set(14, 0, 0, 0, 0);                                    // camera at the origin, phase2 0
    set(15, 1.0f / 0.5f, 1.0f / 2.0f, 0.3f, 1e-3f);        // 1/cone, 1/radius, minHeight, eps
    set(16, 0, 0, -0.8f, 0);                                // player at x=y=0, ground z = base height
    set(17, -q.physicsCenter, q.physicsCenter, 2.0f, q.phase);   // edge-centre, centre, radius, phase1
    set(18, q.windAmp, 0, k, 0);                            // wind along +x
    set(19, 0, 0, k, 0);
    set(20, 0.737f, 1.311f, 1.7f, 0.0f);
    set(21, 1.0f, 0.0f, -0.3f, 1.0f / 0.7f);                // variance 0, anchor 0.3
    set(22, q.motion, 0, 0, 0);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE); dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE); dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    Check(dev->BeginScene(), "begin");
    Check(dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF000000, 1, 0), "clear");
    if (!instanced) Check(dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, kRows * 2 - 2, blade, sizeof(Vertex)), "draw");
    else {
        IDirect3DVertexBuffer9 *geometry = nullptr, *plants = nullptr; IDirect3DIndexBuffer9* indices = nullptr;
        std::vector<uint16_t> list;
        for (int tri = 0; tri < kRows * 2 - 2; ++tri) { list.push_back(uint16_t(tri)); list.push_back(uint16_t(tri + 1 + (tri & 1))); list.push_back(uint16_t(tri + 2 - (tri & 1))); }
        Check(dev->CreateVertexBuffer(sizeof model, D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &geometry, nullptr), "geometry vb");
        Check(dev->CreateVertexBuffer(sizeof plant, D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &plants, nullptr), "instance vb");
        Check(dev->CreateIndexBuffer(UINT(list.size() * 2), D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &indices, nullptr), "index buffer");
        void* m = nullptr;
        Check(geometry->Lock(0, sizeof model, &m, 0), "lock"); std::memcpy(m, model, sizeof model); geometry->Unlock();
        Check(plants->Lock(0, sizeof plant, &m, 0), "lock"); std::memcpy(m, &plant, sizeof plant); plants->Unlock();
        Check(indices->Lock(0, UINT(list.size() * 2), &m, 0), "lock"); std::memcpy(m, list.data(), list.size() * 2); indices->Unlock();
        Check(dev->SetStreamSource(0, geometry, 0, sizeof(gi::GpuVertex)), "stream 0");
        Check(dev->SetStreamSource(1, plants, 0, sizeof(gi::GpuInstance)), "stream 1");
        Check(dev->SetStreamSourceFreq(0, D3DSTREAMSOURCE_INDEXEDDATA | 1u), "frequency 0");
        Check(dev->SetStreamSourceFreq(1, D3DSTREAMSOURCE_INSTANCEDATA | 1u), "frequency 1");
        Check(dev->SetIndices(indices), "indices");
        Check(dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, kRows * 2, 0, kRows * 2 - 2), "instanced draw");
        Check(dev->SetStreamSourceFreq(0, 1), "restore frequency 0"); Check(dev->SetStreamSourceFreq(1, 1), "restore frequency 1");
        dev->SetStreamSource(0, nullptr, 0, 0); dev->SetStreamSource(1, nullptr, 0, 0); dev->SetIndices(nullptr);
        geometry->Release(); plants->Release(); indices->Release();
    }
    Check(dev->EndScene(), "end");
    auto pixels = Pixels(dev);
    vs->Release();
    return pixels;
}

static void Report(const char* label, const std::vector<DWORD>& p) {
    std::printf("%-52s tip x=%6.2f   base x=%6.2f\n", label, Centroid(p, 20, 28), Centroid(p, 48, 54));
}

int main(int argc, char** argv) {
    Require(argc >= 3, "usage: probe <grass pack> <dir>");
    Require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
    auto* window = SDL_CreateWindow("FTA grass probe", kSize, kSize, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    Require(window != nullptr, SDL_GetError());
    auto* library = dlopen("libdxvk_d3d9.so", RTLD_NOW | RTLD_LOCAL); Require(library != nullptr, dlerror());
    auto create = reinterpret_cast<IDirect3D9* (*)(UINT)>(dlsym(library, "Direct3DCreate9")); Require(create != nullptr, "factory");
    auto* d3d = create(D3D_SDK_VERSION); Require(d3d != nullptr, "D3D9");
    D3DPRESENT_PARAMETERS pp = {}; pp.BackBufferWidth = pp.BackBufferHeight = kSize; pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.BackBufferCount = 1; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.Windowed = TRUE; pp.hDeviceWindow = reinterpret_cast<HWND>(window);
    pp.EnableAutoDepthStencil = TRUE; pp.AutoDepthStencilFormat = D3DFMT_D24S8;   // as the game and the other probes
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* dev = nullptr; Check(d3d->CreateDevice(0, D3DDEVTYPE_HAL, pp.hDeviceWindow, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev), "device");

    ShaderPack pack; std::string error; Require(pack.Load(argv[1], error), error.c_str());
    Require(pack.Count() == 3, "three live grass permutations");
    const D3DVERTEXELEMENT9 elements[] = {
        {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0}, {0, 12, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0},
        {0, 24, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0}, {0, 28, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0}, D3DDECL_END()};
    Rig rig{dev, nullptr, nullptr, nullptr};
    Check(dev->CreateVertexDeclaration(elements, &rig.decl), "declaration");
    const D3DVERTEXELEMENT9 instancedElements[] = {   // the runtime's declaration (WxlGrassInstancing.cpp)
        {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0}, {0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
        {1, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1}, {1, 16, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 2},
        {1, 32, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 3}, {1, 48, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0},
        {1, 60, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0}, D3DDECL_END()};
    Check(dev->CreateVertexDeclaration(instancedElements, &rig.instancedDecl), "instanced declaration");
    ShaderPack instancedPack; const bool haveInstanced = argc >= 4;
    if (haveInstanced) { std::string why; Require(instancedPack.Load(argv[3], why), why.c_str()); Require(instancedPack.Count() == 6, "stock and motion instanced shaders for three permutations"); }
    {   // the engine's own grass pixel shader (permutation 0) and a white texture
        const auto psBytes = ReadAll(std::string(argv[2]) + "/DetailDoodad_0.ps");
        Check(dev->CreatePixelShader(reinterpret_cast<const DWORD*>(psBytes.data()), &rig.ps), "grass pixel shader");
        Check(dev->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &rig.white, nullptr), "texture");
        D3DLOCKED_RECT r = {}; Check(rig.white->LockRect(0, &r, nullptr, 0), "lock");
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) reinterpret_cast<DWORD*>(static_cast<uint8_t*>(r.pBits) + y * r.Pitch)[x] = 0xFFFFFFFF;
        rig.white->UnlockRect(0);
    }

    for (int permutation = 0; permutation < 3; ++permutation) {
        const auto original = ReadAll(std::string(argv[2]) + "/DetailDoodad_" + std::to_string(permutation) + ".vs");
        const PackEntry* patched = pack.FindVertex(original.data(), original.size());
        Require(patched != nullptr, "patched shader for permutation");
        std::vector<uint32_t> stock(original.size() / 4); std::memcpy(stock.data(), original.data(), original.size());
        std::printf("-- permutation %d\n", permutation);
        Params off; off.motion = 0;
        const auto reference = Render(rig, stock, off);
        Require(Centroid(reference, 20, 28) > 0, "stock blade is visible");
        Report("stock shader", reference);
        // Motion switch off: the patch must be pixel-identical to stock.
        Require(Render(rig, patched->bytecode, off) == reference, "patched shader with motion off equals stock");
        // Wind: the tip moves, the anchored base does not.
        Params wind; wind.windAmp = 0.35f; wind.phase = 1.0f;
        const auto swayed = Render(rig, patched->bytecode, wind);
        Report("wind (amplitude 0.35)", swayed);
        Require(std::fabs(Centroid(swayed, 20, 28) - Centroid(reference, 20, 28)) > 1.5f, "wind moves the tip");
        Require(std::fabs(Centroid(swayed, 48, 54) - Centroid(reference, 48, 54)) < 1.5f, "wind keeps the base anchored");
        // Player repulsion: blades part away from the player at x=0.
        Params right; right.physicsCenter = 0.5f; right.bladeX = 0.3f;
        Params left = right; left.bladeX = -0.3f;
        Params rightOff = right; rightOff.motion = 0; Params leftOff = left; leftOff.motion = 0;
        const float rightShift = Centroid(Render(rig, patched->bytecode, right), 20, 28) - Centroid(Render(rig, stock, rightOff), 20, 28);
        const float leftShift = Centroid(Render(rig, patched->bytecode, left), 20, 28) - Centroid(Render(rig, stock, leftOff), 20, 28);
        std::printf("%-52s right blade %+6.2f px, left blade %+6.2f px\n", "repulsion from a player at the origin", rightShift, leftShift);
        Require(rightShift > 3.0f && leftShift < -3.0f, "blades part away from the player");
        // Rigid props carry an interior alpha and must not move; endpoint alphas still do.
        Params rigid = wind; rigid.alpha = 254; Params rigidLow = wind; rigidLow.alpha = 1;
        const auto rigidImage = Render(rig, patched->bytecode, rigid);
        Require(std::fabs(Centroid(rigidImage, 20, 28) - Centroid(reference, 20, 28)) < 1.0f, "rigid prop (alpha 254) does not sway");
        Require(std::fabs(Centroid(Render(rig, patched->bytecode, rigidLow), 20, 28) - Centroid(reference, 20, 28)) < 1.0f, "rigid prop (alpha 1) does not sway");
        Params flexible = wind; flexible.alpha = 0;
        Require(std::fabs(Centroid(Render(rig, patched->bytecode, flexible), 20, 28) - Centroid(reference, 20, 28)) > 1.5f, "endpoint alpha 0 still sways");
        if (haveInstanced) {
            const PackEntry* instStock = instancedPack.FindVertex(original.data(), original.size());
            const PackEntry* instMotion = instancedPack.FindVertex(patched->bytecode.data(), patched->bytecode.size() * 4);
            Require(instStock && instMotion, "instanced shaders for the stock and the motion-patched bytecode");
            const auto same = [](const std::vector<DWORD>& a, const std::vector<DWORD>& b) { size_t d = 0; for (size_t i = 0; i < a.size(); ++i) d += a[i] != b[i]; return d; };
            // Same placement: exact, with and without motion, repulsion and the rigid alpha.
            Require(same(Render(rig, instStock->bytecode, off, true), reference) == 0, "instanced stock blade equals the baked blade");
            const auto bakedWind = Render(rig, patched->bytecode, wind);
            const size_t windDiff = same(Render(rig, instMotion->bytecode, wind, true), bakedWind);
            std::printf("%-52s %zu differing pixels\n", "instanced wind vs baked wind", windDiff);
            Require(windDiff == 0, "instanced motion shader sways exactly like the baked one");
            Require(same(Render(rig, instMotion->bytecode, right, true), Render(rig, patched->bytecode, right)) == 0, "instanced repulsion equals baked");
            Require(same(Render(rig, instMotion->bytecode, rigid, true), Render(rig, patched->bytecode, rigid)) == 0, "instanced rigid prop equals baked");
            // A rotated, scaled, moved plant: positions come out of different float orders on the CPU and
            // the GPU, so allow a sliver of edge pixels.
            Placement placed; placed.rotation = 0.7f; placed.scale = 1.3f; placed.x = 0.15f;
            const auto bakedPlaced = Render(rig, stock, off, false, &placed);
            const size_t placedDiff = same(Render(rig, instStock->bytecode, off, true, &placed), bakedPlaced);
            size_t lit = 0; for (DWORD px : bakedPlaced) lit += px != 0;
            std::printf("%-52s %zu of %zu lit pixels differ\n", "rotated and scaled plant, instanced vs baked", placedDiff, lit);
            Require(lit > 0 && placedDiff * 50 <= lit, "instanced placement matches the baked placement");
        }
    }
    dev->SetTexture(0, nullptr); dev->SetPixelShader(nullptr); dev->SetVertexShader(nullptr);
    rig.white->Release(); rig.ps->Release(); rig.decl->Release(); rig.instancedDecl->Release();
    SDL_Delay(1500);
    dev->Release(); d3d->Release(); SDL_DestroyWindow(window); SDL_Delay(500);
    std::puts("PASS: patched grass shaders match stock when off, sway the tips, part around the player and spare rigid props");
    return 0;
}
