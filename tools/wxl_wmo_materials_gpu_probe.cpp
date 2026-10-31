// Synthetic, hidden-window probe for the patched WMO layered-material shaders. It draws
// with the engine's own MapObjDiffuse permutation 0 and its generated patches on the
// real dxvk-native backend. It does not start the game or alter client data.
//
//   wxl-wmo-materials-gpu-probe <wmo_shaders.pack> <dir with the extracted originals>
#include <d3d9.h>
#include <SDL3/SDL.h>
#include <dlfcn.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include "Engine/Source/ModernAssets/WxlWmo/WmoShaderPack.h"
using namespace wxl::wmo;

static void Check(HRESULT r, const char* what) { if (FAILED(r)) { std::fprintf(stderr, "%s: %08x\n", what, unsigned(r)); std::exit(1); } }
static void Require(bool v, const char* what) { if (!v) { std::fprintf(stderr, "FAILED: %s\n", what); std::exit(1); } }
static std::vector<uint8_t> ReadAll(const std::string& path) {
    std::vector<uint8_t> out; FILE* f = std::fopen(path.c_str(), "rb"); Require(f != nullptr, path.c_str());
    uint8_t b[65536]; size_t n; while ((n = std::fread(b, 1, sizeof b, f)) > 0) out.insert(out.end(), b, b + n);
    std::fclose(f); return out;
}
struct Vertex0 { float x, y, z, nx, ny, nz; DWORD colour; };            // stream 0: 28 bytes
struct Vertex1 { float uv[4][2]; DWORD weights; };                       // stream 1: 36 bytes
static_assert(sizeof(Vertex0) == 28 && sizeof(Vertex1) == 36, "stream layouts");

static IDirect3DTexture9* Solid(IDirect3DDevice9* dev, DWORD argb) {
    IDirect3DTexture9* t = nullptr; Check(dev->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t, nullptr), "texture");
    D3DLOCKED_RECT r = {}; Check(t->LockRect(0, &r, nullptr, 0), "lock texture");
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) reinterpret_cast<DWORD*>(static_cast<uint8_t*>(r.pBits) + y * r.Pitch)[x] = argb;
    t->UnlockRect(0); return t;
}
static IDirect3DTexture9* Split(IDirect3DDevice9* dev, DWORD left, DWORD right) {   // 2x1: u<0.5 left, u>0.5 right
    IDirect3DTexture9* t = nullptr; Check(dev->CreateTexture(2, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t, nullptr), "split texture");
    D3DLOCKED_RECT r = {}; Check(t->LockRect(0, &r, nullptr, 0), "lock split texture");
    reinterpret_cast<DWORD*>(r.pBits)[0] = left; reinterpret_cast<DWORD*>(r.pBits)[1] = right;
    t->UnlockRect(0); return t;
}
static DWORD CentrePixel(IDirect3DDevice9* dev) {
    IDirect3DSurface9 *target = nullptr, *cpu = nullptr;
    Check(dev->GetRenderTarget(0, &target), "target");
    D3DSURFACE_DESC d = {}; target->GetDesc(&d);
    Check(dev->CreateOffscreenPlainSurface(d.Width, d.Height, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &cpu, nullptr), "cpu");
    Check(dev->GetRenderTargetData(target, cpu), "read");
    D3DLOCKED_RECT l = {}; Check(cpu->LockRect(&l, nullptr, D3DLOCK_READONLY), "lock");
    const DWORD p = reinterpret_cast<const DWORD*>(static_cast<uint8_t*>(l.pBits) + (d.Height / 2) * l.Pitch)[d.Width / 2] & 0xFFFFFF;
    cpu->UnlockRect(); cpu->Release(); target->Release(); return p;
}
static int Channel(DWORD p, int shift) { return int((p >> shift) & 255); }
static bool Near(DWORD p, int r, int g, int b, int tol = 6) {
    return std::abs(Channel(p, 16) - r) <= tol && std::abs(Channel(p, 8) - g) <= tol && std::abs(Channel(p, 0) - b) <= tol;
}

struct Rig {
    IDirect3DDevice9* dev = nullptr; IDirect3DVertexDeclaration9* decl = nullptr;
    IDirect3DVertexBuffer9 *vb0 = nullptr, *vb1 = nullptr; IDirect3DIndexBuffer9* ib = nullptr;
};

// uvU / diffuseRight (optional): each layer samples a 2x1 texture at u = uvU[layer], so a wrongly routed
// interpolant shows up as the wrong colour.
static DWORD Render(Rig& rig, const PackEntry& vs, const PackEntry& ps, const DWORD* diffuse, const DWORD* height, DWORD weights, const float tint[4],
                    const float* uvU = nullptr, const DWORD* diffuseRight = nullptr) {
    IDirect3DDevice9* dev = rig.dev;
    IDirect3DVertexShader9* v = nullptr; IDirect3DPixelShader9* p = nullptr;
    Check(dev->CreateVertexShader(vs.bytecode.data(), &v), "patched vertex shader");
    Check(dev->CreatePixelShader(ps.bytecode.data(), &p), "patched pixel shader");
    const Vertex0 s0[4] = {{-1, -1, .5f, 0, 0, 1, 0x80808080}, {-1, 1, .5f, 0, 0, 1, 0x80808080}, {1, -1, .5f, 0, 0, 1, 0x80808080}, {1, 1, .5f, 0, 0, 1, 0x80808080}};
    Vertex1 s1[4] = {}; for (auto& vertex : s1) { for (auto& uv : vertex.uv) uv[0] = uv[1] = .5f; vertex.weights = weights; if (uvU) for (int i = 0; i < 4; ++i) vertex.uv[i][0] = uvU[i]; }
    void* mem = nullptr;
    Check(rig.vb0->Lock(0, sizeof s0, &mem, 0), "lock vb0"); std::memcpy(mem, s0, sizeof s0); rig.vb0->Unlock();
    Check(rig.vb1->Lock(0, sizeof s1, &mem, 0), "lock vb1"); std::memcpy(mem, s1, sizeof s1); rig.vb1->Unlock();
    std::vector<IDirect3DTexture9*> textures;
    const unsigned stages = ps.layers == 4 ? 8 : 2;
    for (unsigned s = 0; s < stages; ++s) {
        const DWORD colour = ps.layers == 4 ? (s < 4 ? diffuse[s] : height[s - 4]) : diffuse[s];
        textures.push_back(uvU && ps.layers == 4 && s < 4 ? Split(dev, colour, diffuseRight[s]) : Solid(dev, colour));
        Check(dev->SetTexture(ps.samplerSlots[s], textures.back()), "bind texture");
        dev->SetSamplerState(ps.samplerSlots[s], D3DSAMP_MINFILTER, D3DTEXF_POINT);
        dev->SetSamplerState(ps.samplerSlots[s], D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        dev->SetSamplerState(ps.samplerSlots[s], D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
        dev->SetSamplerState(ps.samplerSlots[s], D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    }
    Check(dev->SetStreamSource(0, rig.vb0, 0, sizeof(Vertex0)), "stream 0");
    Check(dev->SetStreamSource(1, rig.vb1, 0, sizeof(Vertex1)), "stream 1");
    Check(dev->SetIndices(rig.ib), "indices");
    Check(dev->SetVertexDeclaration(rig.decl), "declaration");
    Check(dev->SetVertexShader(v), "vs"); Check(dev->SetPixelShader(p), "ps");
    const float identity[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    dev->SetVertexShaderConstantF(31, identity[0], 1); dev->SetVertexShaderConstantF(32, identity[1], 1); dev->SetVertexShaderConstantF(33, identity[2], 1);
    dev->SetVertexShaderConstantF(2, identity[0], 4);
    const float fog[4] = {0, 1, 1, 0}; dev->SetVertexShaderConstantF(30, fog, 1);   // fog factor stays 1
    const float fogColour[4] = {0, 0, 0, 1}; dev->SetPixelShaderConstantF(2, fogColour, 1);
    if (uvU) {   // the lit permutations: unit w, ambient and sun colour, sun direction
        const float c0[4] = {1, 0, 0, 0}, ambient[4] = {1, 1, 1, 0}, sun[4] = {0, 0, 0, 0}, direction[4] = {0, 0, -1, 0};
        dev->SetVertexShaderConstantF(0, c0, 1); dev->SetVertexShaderConstantF(10, ambient, 1);
        dev->SetVertexShaderConstantF(11, sun, 1); dev->SetVertexShaderConstantF(12, direction, 1);
    }
    if (ps.tintRegister >= 0) dev->SetPixelShaderConstantF(ps.tintRegister, tint, 1);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE); dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE); dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    Check(dev->BeginScene(), "begin");
    Check(dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000, 1, 0), "clear");
    Check(dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 4, 0, 2), "draw");
    Check(dev->EndScene(), "end");
    const DWORD pixel = CentrePixel(dev);
    static int renders = 0; ++renders;   // debugging aid: WMO_PROBE_CASES=n stops after n draws
    if (const char* limit = std::getenv("WMO_PROBE_CASES")) if (renders >= std::atoi(limit)) { SDL_Delay(2500); std::exit(0); }
    for (auto* t : textures) t->Release();
    for (unsigned s = 0; s < stages; ++s) dev->SetTexture(ps.samplerSlots[s], nullptr);
    v->Release(); p->Release();
    return pixel;
}

#define EXPECT(pixel, r, g, b, label) do { const DWORD value = (pixel); \
    std::printf("%-44s -> %02x%02x%02x\n", label, Channel(value, 16), Channel(value, 8), Channel(value, 0)); \
    Require(Near(value, r, g, b, 12), label); } while (0)

int main(int argc, char** argv) {
    Require(argc >= 3, "usage: probe <pack> <originals dir>");
    Require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
    auto* window = SDL_CreateWindow("FTA WMO materials probe", 64, 64, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    Require(window != nullptr, SDL_GetError());
    auto* library = dlopen("libdxvk_d3d9.so", RTLD_NOW | RTLD_LOCAL); Require(library != nullptr, dlerror());
    auto create = reinterpret_cast<IDirect3D9* (*)(UINT)>(dlsym(library, "Direct3DCreate9")); Require(create != nullptr, "factory");
    auto* d3d = create(D3D_SDK_VERSION); Require(d3d != nullptr, "D3D9");
    D3DPRESENT_PARAMETERS pp = {}; pp.BackBufferWidth = pp.BackBufferHeight = 64; pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.BackBufferCount = 1; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.Windowed = TRUE; pp.hDeviceWindow = reinterpret_cast<HWND>(window);
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* dev = nullptr; Check(d3d->CreateDevice(0, D3DDEVTYPE_HAL, pp.hDeviceWindow, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev), "device");

    ShaderPack pack; std::string error;
    Require(pack.Load(argv[1], error), error.c_str());
    // Every generated shader must be accepted by the backend.
    unsigned accepted = 0, index = 0;
    // Debugging aid: WMO_PROBE_RANGE=first:last restricts the sweep; WMO_PROBE_SWEEP_ONLY=1 stops after it.
    unsigned first = 0, last = ~0u;
    if (const char* range = std::getenv("WMO_PROBE_RANGE")) std::sscanf(range, "%u:%u", &first, &last);
    for (const PackEntry& e : pack.Entries()) {
        if (index < first || index++ >= last) continue;
        if (e.kind == 0) { IDirect3DVertexShader9* v = nullptr; Check(dev->CreateVertexShader(e.bytecode.data(), &v), "pack vertex shader"); v->Release(); }
        else { IDirect3DPixelShader9* p = nullptr; Check(dev->CreatePixelShader(e.bytecode.data(), &p), "pack pixel shader"); p->Release(); }
        ++accepted;
    }
    std::printf("backend accepted %u of %zu patched shaders\n", accepted, pack.Count());
    if (std::getenv("WMO_PROBE_SWEEP_ONLY")) { SDL_Delay(2500); return 0; }
    const std::string dir = argv[2];
    const auto vsBytes = ReadAll(dir + "/MapObjDiffuse_T1_000.vs"), psBytes = ReadAll(dir + "/MapObjDiffuse_000.ps");
    const PackEntry* vs = pack.FindVertex(vsBytes.data(), vsBytes.size());
    Require(vs != nullptr, "patched vertex shader for MapObjDiffuse_T1 permutation 0");
    const PackEntry* two = pack.FindPixel(psBytes.data(), psBytes.size(), 2, *vs);
    const PackEntry* four = pack.FindPixel(psBytes.data(), psBytes.size(), 4, *vs);
    Require(two && four, "patched pixel shaders for MapObjDiffuse permutation 0");
    std::printf("mapping uv=%u weights=%u; two-layer slots %u,%u; four-layer slots %u..%u, tint c%d\n", vs->uvUsage, vs->weightUsage,
                two->samplerSlots[0], two->samplerSlots[1], four->samplerSlots[0], four->samplerSlots[7], four->tintRegister);

    const D3DVERTEXELEMENT9 elements[] = {
        {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0}, {0, 12, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0},
        {0, 24, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0}, {1, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
        {1, 8, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1}, {1, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 2},
        {1, 24, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 3}, {1, 32, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 1}, D3DDECL_END()};
    Rig rig; rig.dev = dev;
    Check(dev->CreateVertexDeclaration(elements, &rig.decl), "declaration");
    Check(dev->CreateVertexBuffer(4 * sizeof(Vertex0), D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &rig.vb0, nullptr), "vb0");
    Check(dev->CreateVertexBuffer(4 * sizeof(Vertex1), D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &rig.vb1, nullptr), "vb1");
    Check(dev->CreateIndexBuffer(6 * sizeof(WORD), D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &rig.ib, nullptr), "ib");
    { void* m = nullptr; const WORD idx[6] = {0, 1, 2, 2, 1, 3}; Check(rig.ib->Lock(0, sizeof idx, &m, 0), "lock ib"); std::memcpy(m, idx, sizeof idx); rig.ib->Unlock(); }

    const DWORD none[4] = {}; const float noTint[4] = {};
    // Two layers: the overlay's alpha selects between the layers.
    { const DWORD fullOverlay[2] = {0xFFFF0000, 0xFF0000FF}, noOverlay[2] = {0xFFFF0000, 0x000000FF}, halfOverlay[2] = {0xFFFF0000, 0x800000FF};
      EXPECT(Render(rig, *vs, *two, noOverlay, none, 0, noTint), 255, 0, 0, "two layers, transparent overlay -> layer A");
      EXPECT(Render(rig, *vs, *two, fullOverlay, none, 0, noTint), 0, 0, 255, "two layers, opaque overlay -> layer B");
      EXPECT(Render(rig, *vs, *two, halfOverlay, none, 0, noTint), 127, 0, 128, "two layers, half overlay -> mix"); }
    // Four layers: diffuse A..D, height in the height maps' alpha, weights in COLOR1.
    const DWORD diffuse[4] = {0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFFFF};
    const DWORD equal[4] = {0x80000000, 0x80000000, 0x80000000, 0x80000000};
    const auto weights = [](unsigned a, unsigned b, unsigned c, unsigned alpha) { return DWORD((alpha << 24) | (a << 16) | (b << 8) | c); };
    EXPECT(Render(rig, *vs, *four, diffuse, equal, weights(255, 0, 0, 0), noTint), 255, 0, 0, "four layers, weight A -> layer A");
    EXPECT(Render(rig, *vs, *four, diffuse, equal, weights(0, 255, 0, 0), noTint), 0, 255, 0, "four layers, weight B -> layer B");
    EXPECT(Render(rig, *vs, *four, diffuse, equal, weights(0, 0, 255, 0), noTint), 0, 0, 255, "four layers, weight C -> layer C");
    EXPECT(Render(rig, *vs, *four, diffuse, equal, weights(0, 0, 0, 0), noTint), 255, 255, 255, "four layers, remainder -> layer D");
    EXPECT(Render(rig, *vs, *four, diffuse, equal, weights(128, 128, 0, 0), noTint), 127, 127, 0, "four layers, equal A/B -> average");
    const DWORD tallA[4] = {0xFF000000, 0x1E000000, 0x80000000, 0x80000000};
    { const DWORD pixel = Render(rig, *vs, *four, diffuse, tallA, weights(128, 128, 0, 0), noTint);
      std::printf("%-44s -> %02x%02x%02x\n", "four layers, tall A height wins the blend", Channel(pixel, 16), Channel(pixel, 8), Channel(pixel, 0));
      Require(Channel(pixel, 16) > Channel(pixel, 8) + 100, "height-based blending favours the taller layer"); }
    const float blueTint[4] = {0, 0, 1, 1};
    EXPECT(Render(rig, *vs, *four, diffuse, equal, weights(255, 0, 0, 255), blueTint), 0, 0, 255, "four layers, enabled tint at full strength");
    EXPECT(Render(rig, *vs, *four, diffuse, equal, weights(255, 0, 0, 255), noTint), 255, 0, 0, "four layers, disabled tint leaves the blend");
    // Compact vertex layouts (the high shadow tiers whose outputs leave no room for the supplied
    // layout). Each layer samples a 2x1 texture at its own u, so a misrouted UV or weight shows as
    // the wrong colour. Lighting differs per permutation, so only the hue pattern is compared:
    // A red, B magenta, C yellow, D white.
    {
        const DWORD left[4] = {0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFFFF}, right[4] = {0xFF00FFFF, 0xFFFF00FF, 0xFFFFFF00, 0xFF000000};
        const float u[4] = {.25f, .75f, .75f, .25f};
        const int expected[4] = {0b100, 0b101, 0b110, 0b111};
        const auto pattern = [](DWORD p) { const int r = Channel(p, 16), g = Channel(p, 8), b = Channel(p, 0), m = std::max({r, g, b});
                                           return m < 24 ? -1 : int((r * 2 > m ? 4 : 0) | (g * 2 > m ? 2 : 0) | (b * 2 > m ? 1 : 0)); };
        const char* const vsNames[] = {"MapObjSpecular_T1_060", "MapObjSpecular_T1_061", "MapObjSpecular_T1_069", "MapObjDiffuse_T1_Refl_060",
                                       "MapObjDiffuse_T1_Refl_061", "MapObjDiffuse_T1_Refl_069", "MapObjUDiffuse_T1_Refl_061", "MapObjUSpecular_T1_061"};
        std::vector<std::string> pixelFiles;
        for (const auto& entry : std::filesystem::directory_iterator(dir))
            if (entry.path().extension() == ".ps") pixelFiles.push_back(entry.path().string());
        std::sort(pixelFiles.begin(), pixelFiles.end());
        unsigned compactVertex = 0;
        for (const char* name : vsNames) {
            const auto code = ReadAll(dir + "/" + name + ".vs");
            const PackEntry* compactVs = pack.FindVertex(code.data(), code.size());
            Require(compactVs != nullptr, name);
            Require(compactVs->uvUsage >= 0x80, "the former overflow permutation now has a compact layout");
            ++compactVertex;
            unsigned paired = 0, correct = 0; std::map<std::string, unsigned> families;
            for (const std::string& file : pixelFiles) {
                const auto psCode = ReadAll(file);
                const PackEntry* compactPs = pack.FindPixel(psCode.data(), psCode.size(), 4, *compactVs);
                if (!compactPs) continue;
                ++paired;
                bool ok = true;
                const DWORD wts[4] = {DWORD(255 << 16), DWORD(255 << 8), DWORD(255), 0};
                for (int layer = 0; layer < 4 && ok; ++layer)
                    ok = pattern(Render(rig, *compactVs, *compactPs, left, equal, wts[layer], noTint, u, right)) == expected[layer];
                if (ok) { ++correct; const std::string base = std::filesystem::path(file).filename().string(); ++families[base.substr(0, base.rfind('_'))]; }
                else if (std::getenv("WMO_PROBE_VERBOSE") && paired <= 6) {
                    std::printf("  %s:", std::filesystem::path(file).filename().c_str());
                    for (int layer = 0; layer < 4; ++layer) { const DWORD q = Render(rig, *compactVs, *compactPs, left, equal, wts[layer], noTint, u, right);
                        std::printf(" %06x", q); }
                    std::printf("\n");
                }
            }
            std::printf("%-30s layout %02x: %u pixel shaders have a compact variant, %u route all four layers correctly (", name, compactVs->uvUsage, paired, correct);
            for (const auto& [family, count] : families) std::printf(" %s x%u", family.c_str(), count);
            std::printf(" )\n");
            Require(correct > 0, "compact layout routes UV C/D and the weights to a pixel shader");
        }
        std::printf("compact vertex layouts checked: %u\n", compactVertex);
    }
    // Let DXVK's background shader compiles finish, then release everything in order.
    dev->SetStreamSource(0, nullptr, 0, 0); dev->SetStreamSource(1, nullptr, 0, 0); dev->SetIndices(nullptr);
    dev->SetVertexShader(nullptr); dev->SetPixelShader(nullptr);
    rig.ib->Release(); rig.vb1->Release(); rig.vb0->Release(); rig.decl->Release();
    SDL_Delay(1500);
    dev->Release(); d3d->Release(); SDL_DestroyWindow(window); SDL_Delay(500);
    std::puts("PASS: patched WMO shaders load and blend on the native backend");
    return 0;
}
