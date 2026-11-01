// Hidden-window native DXVK probe for the unit-outline port. It links the shipped Outline.cpp and gx helpers
// (WoW/Source/WorldClient/Linux/WxlUnitOutline) and supplies a mock unit, camera and flat ground behind the
// same facade, so the real silhouette stamp, edge composite, chevron and danger-guide draws run on the real
// backend. Synthetic geometry only; no game, login or assets.
#include <d3d9.h>
#include <SDL3/SDL.h>
#include <dlfcn.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "WoW/Source/WorldClient/Linux/WxlUnitOutline/events/EventScript.hpp"
#include "WoW/Source/WorldClient/Linux/WxlUnitOutline/game/camera/Camera.hpp"
#include "WoW/Source/WorldClient/Linux/WxlUnitOutline/game/combat/Readability.hpp"
#include "WoW/Source/WorldClient/Linux/WxlUnitOutline/game/m2/M2.hpp"
#include "WoW/Source/WorldClient/Linux/WxlUnitOutline/game/unit/Unit.hpp"
#include "WoW/Source/WorldClient/Linux/WxlUnitOutline/game/world/Pick.hpp"
#include "WoW/Source/WorldClient/Linux/WxlUnitOutline/game/world/World.hpp"
#include "WoW/Source/WorldClient/Linux/WxlUnitOutline/gpu/Control.hpp"

namespace ev = wxl::events;
void WxlUnitOutlineInit();

// ------------------------------------------------------------------ mock engine behind the facade
namespace {
struct Mock {
    unsigned long long unitGuid = 0x0000F13000000001ull;   // creature GUID (high word non-zero)
    unsigned long long playerGuid = 0x0000000000000042ull;
    unsigned long long target = unitGuid;
    float unitPos[3] = {0, 20, -5};
    float playerPos[3] = {0, 0, -2};
    float facing = -1.5707963f;      // -Y, toward the camera, so the ground cues land in front of the unit
    bool targetIsPlayer = false;
    int rank = 0;
    float groundZ = -5.0f;
    char unitObject = 0, playerObject = 0, model = 0;
    wxl::gpu::control::Settings control;
    wxl::game::combat::ReadabilitySettings readability;
    float view[16] = {}, projection[16] = {};
} g_mock;
}

bool WxlProfileEnabled(const char*, bool) { return true; }
bool WxlGetControlSettings(void* out, size_t size) {
    if (size != sizeof(g_mock.control)) return false;
    std::memcpy(out, &g_mock.control, size);
    return true;
}
namespace wxl::game::combat { ReadabilitySettings Snapshot() { return g_mock.readability; } }
namespace wxl::game::world {
unsigned long long MouseoverGuid() { return 0; }
unsigned long long TargetGuid() { return g_mock.target; }
unsigned long long ActivePlayerGuid() { return g_mock.playerGuid; }
void* ResolveObject(unsigned long long guid, unsigned mask) {
    if (guid == g_mock.unitGuid && mask == (g_mock.targetIsPlayer ? kTypeMaskPlayer : kTypeMaskUnit)) return &g_mock.unitObject;
    if (guid == g_mock.playerGuid && mask == kTypeMaskPlayer) return &g_mock.playerObject;
    return nullptr;
}
void UnitPosition(void* unit, float out[3]) {
    const float* p = unit == &g_mock.playerObject ? g_mock.playerPos : g_mock.unitPos;
    std::memcpy(out, p, sizeof(float) * 3);
}
float UnitFacing(void*) { return g_mock.facing; }
int IntersectSegment(const Vec3& a, const Vec3& b, WorldHit& out) {
    out = WorldHit{};
    // A flat ground plane at groundZ.
    if ((a.z - g_mock.groundZ) * (b.z - g_mock.groundZ) > 0.0f || a.z == b.z) return 0;
    const float t = (g_mock.groundZ - a.z) / (b.z - a.z);
    out.type = 1;
    out.pos = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, g_mock.groundZ};
    out.t = t;
    return 1;
}
}
namespace wxl::game::unit {
void* Model(void* unit) { return unit == &g_mock.unitObject ? static_cast<void*>(&g_mock.model) : nullptr; }
void* ModelParent(void*) { return nullptr; }
int Reaction(void*, void*) { return 0; }   // hostile: red
int CreatureRank(void*) { return g_mock.rank; }
int CreatureType(void*) { return 0; }
}
namespace wxl::game::m2 {
bool VisualFacing(void*, float&) { return false; }
bool FootprintInputs(void*, float& x, float& y, float& scale) { x = y = 1.0f; scale = 1.0f; return true; }
}
namespace wxl::game::camera {
const float* View() { return g_mock.view; }
const float* Projection() { return g_mock.projection; }
void Store(const float* v, const float* p) { std::memcpy(g_mock.view, v, 64); std::memcpy(g_mock.projection, p, 64); }
}

// ------------------------------------------------------------------ the probe
static void Check(HRESULT result, const char* operation) {
    if (FAILED(result)) { std::fprintf(stderr, "%s failed: 0x%x\n", operation, unsigned(result)); std::exit(1); }
}
constexpr unsigned kSize = 256;

struct Rig {
    SDL_Window* window = nullptr;
    void* library = nullptr;
    IDirect3D9* d3d = nullptr;
    IDirect3DDevice9* device = nullptr;
    IDirect3DVertexBuffer9* vb = nullptr;
    IDirect3DIndexBuffer9* ib = nullptr;
    IDirect3DSurface9 *target = nullptr, *resolved = nullptr, *readback = nullptr;
    bool multisampled = false;
};

struct Pixels { std::vector<uint32_t> data = std::vector<uint32_t>(kSize * kSize); };

static bool Open(Rig& rig, bool msaa) {
    if (!SDL_Init(SDL_INIT_VIDEO)) return false;
    rig.window = SDL_CreateWindow("FTA unit-outline probe", kSize, kSize, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    rig.library = dlopen("libdxvk_d3d9.so", RTLD_NOW | RTLD_LOCAL);
    if (!rig.window || !rig.library) return false;
    auto create = reinterpret_cast<IDirect3D9* (*)(UINT)>(dlsym(rig.library, "Direct3DCreate9"));
    rig.d3d = create ? create(D3D_SDK_VERSION) : nullptr;
    if (!rig.d3d) return false;
    D3DPRESENT_PARAMETERS present{};
    present.BackBufferWidth = present.BackBufferHeight = kSize;
    present.BackBufferFormat = D3DFMT_A8R8G8B8;
    present.BackBufferCount = 1;
    present.SwapEffect = D3DSWAPEFFECT_DISCARD;
    present.Windowed = TRUE;
    present.hDeviceWindow = reinterpret_cast<HWND>(rig.window);
    present.EnableAutoDepthStencil = TRUE;
    present.AutoDepthStencilFormat = D3DFMT_D24S8;
    present.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    if (msaa) {
        DWORD levels = 0;
        if (FAILED(rig.d3d->CheckDeviceMultiSampleType(0, D3DDEVTYPE_HAL, D3DFMT_A8R8G8B8, TRUE, D3DMULTISAMPLE_4_SAMPLES, &levels))) return false;
        present.MultiSampleType = D3DMULTISAMPLE_4_SAMPLES;
        rig.multisampled = true;
    }
    Check(rig.d3d->CreateDevice(0, D3DDEVTYPE_HAL, present.hDeviceWindow, D3DCREATE_HARDWARE_VERTEXPROCESSING, &present, &rig.device), "CreateDevice");

    // The "unit": an upright quad, feet on the ground, 6 wide and 8 tall, 20 ahead of the camera.
    struct Vertex { float x, y, z; DWORD colour; };
    const float x = 3, z0 = -5, z1 = 3, y = 20;
    const Vertex vertices[4] = {{-x, y, z0, 0xFFFFFFFF}, {x, y, z0, 0xFFFFFFFF}, {-x, y, z1, 0xFFFFFFFF}, {x, y, z1, 0xFFFFFFFF}};
    const uint16_t indices[6] = {0, 1, 2, 2, 1, 3};
    Check(rig.device->CreateVertexBuffer(sizeof(vertices), 0, D3DFVF_XYZ | D3DFVF_DIFFUSE, D3DPOOL_MANAGED, &rig.vb, nullptr), "vb");
    void* bits = nullptr;
    Check(rig.vb->Lock(0, 0, &bits, 0), "lock vb"); std::memcpy(bits, vertices, sizeof(vertices)); rig.vb->Unlock();
    Check(rig.device->CreateIndexBuffer(sizeof(indices), 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &rig.ib, nullptr), "ib");
    Check(rig.ib->Lock(0, 0, &bits, 0), "lock ib"); std::memcpy(bits, indices, sizeof(indices)); rig.ib->Unlock();

    Check(rig.device->GetRenderTarget(0, &rig.target), "GetRenderTarget");
    Check(rig.device->CreateRenderTarget(kSize, kSize, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &rig.resolved, nullptr), "resolve target");
    Check(rig.device->CreateOffscreenPlainSurface(kSize, kSize, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &rig.readback, nullptr), "readback");

    // Camera at the origin looking down +Y (row-vector D3D matrices), standard depth, near 0.1, far 100.
    // The eye is 3 above the origin (view translation -3 in y), so the ground ahead is seen at a slant.
    const float view[16]{1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, -3, 0, 1};
    const float projection[16]{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 100.f / 99.9f, 1, 0, 0, -10.f / 99.9f, 0};
    wxl::game::camera::Store(view, projection);
    return true;
}

static void DrawUnit(Rig& rig) {
    auto* d = rig.device;
    D3DMATRIX identity{};
    identity._11 = identity._22 = identity._33 = identity._44 = 1.0f;
    d->SetTransform(D3DTS_WORLD, &identity);
    d->SetTransform(D3DTS_VIEW, reinterpret_cast<const D3DMATRIX*>(g_mock.view));
    d->SetTransform(D3DTS_PROJECTION, reinterpret_cast<const D3DMATRIX*>(g_mock.projection));
    d->SetRenderState(D3DRS_LIGHTING, FALSE);
    d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    d->SetRenderState(D3DRS_ZENABLE, TRUE);
    d->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    d->SetTexture(0, nullptr);
    d->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    d->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    d->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    d->SetStreamSource(0, rig.vb, 0, 16);
    d->SetIndices(rig.ib);
    d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 4, 0, 2);
    ev::M2BatchDrawArgs args{};
    args.device = d; args.model = &g_mock.model; args.primType = D3DPT_TRIANGLELIST;
    args.baseVertex = 0; args.minIndex = 0; args.numVerts = 4; args.startIndex = 0; args.primCount = 2; args.phase = 1;
    ev::Dispatch(ev::Event::OnM2BatchDraw, args);
}

// One presented frame; `unitDrawn` false builds the target snapshot only (the pipeline is one frame deep).
static void Frame(Rig& rig, bool unitDrawn, Pixels& out) {
    Check(rig.device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF000000, 1.0f, 0), "Clear");
    Check(rig.device->BeginScene(), "BeginScene");
    if (unitDrawn) DrawUnit(rig);
    ev::Dispatch(ev::Event::OnEndScene, ev::EndSceneArgs{rig.device});
    Check(rig.device->EndScene(), "EndScene");
    IDirect3DSurface9* source = rig.target;
    if (rig.multisampled) { Check(rig.device->StretchRect(rig.target, nullptr, rig.resolved, nullptr, D3DTEXF_NONE), "resolve"); source = rig.resolved; }
    Check(rig.device->GetRenderTargetData(source, rig.readback), "GetRenderTargetData");
    D3DLOCKED_RECT lock{};
    Check(rig.readback->LockRect(&lock, nullptr, D3DLOCK_READONLY), "LockRect");
    for (unsigned y = 0; y < kSize; ++y)
        std::memcpy(&out.data[y * kSize], static_cast<const unsigned char*>(lock.pBits) + y * lock.Pitch, kSize * 4);
    rig.readback->UnlockRect();
}

struct Stats { unsigned lit = 0, red = 0, white = 0, other = 0, minX = kSize, maxX = 0, minY = kSize, maxY = 0; };
static Stats Count(const Pixels& p) {
    Stats s;
    for (unsigned y = 0; y < kSize; ++y)
        for (unsigned x = 0; x < kSize; ++x) {
            const uint32_t c = p.data[y * kSize + x];
            const unsigned r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
            if (!(c & 0xFFFFFF)) continue;
            ++s.lit;
            if (r > 200 && g > 200 && b > 200) ++s.white;
            else if (r > 120 && g < 90 && b < 90) {
                ++s.red;
                s.minX = std::min(s.minX, x); s.maxX = std::max(s.maxX, x);
                s.minY = std::min(s.minY, y); s.maxY = std::max(s.maxY, y);
            } else ++s.other;
        }
    return s;
}

static int g_failures = 0;
static void Expect(bool ok, const char* what) { std::printf("  %-66s %s\n", what, ok ? "ok" : "FAILED"); if (!ok) ++g_failures; }

static void Reset() {
    g_mock.control = wxl::gpu::control::Settings{};
    g_mock.control.facingMode = wxl::gpu::control::FacingOff;
    g_mock.readability = wxl::game::combat::ReadabilitySettings{};
    g_mock.target = g_mock.unitGuid;
    g_mock.rank = 0;
}

static void Suite(Rig& rig, const char* name) {
    std::printf("== %s\n", name);
    Pixels pixels;
    auto run = [&]() { Frame(rig, false, pixels); Frame(rig, true, pixels); return Count(pixels); };

    // Silhouette outline only: a red line hugging the unit, nothing painted over its interior.
    Reset();
    Stats a = run();
    std::printf("outline: red=%u white=%u box=(%u..%u, %u..%u)\n", a.red, a.white, a.minX, a.maxX, a.minY, a.maxY);
    Expect(a.red > 60, "hostile target gets a red silhouette outline");
    Expect(a.white > 500, "the unit body itself is not overpainted");
    Expect(a.maxY - a.minY > 20 && a.maxX - a.minX > 15, "outline spans the unit's silhouette");

    // The outline switch off: the unit draws unadorned.
    Reset();
    g_mock.control.outlineEnabled = 0;
    Stats b = run();
    Expect(b.red == 0 && b.white > 500, "outline off leaves only the unit");

    // No target, no mouseover: nothing to outline.
    Reset();
    g_mock.target = 0;
    Stats c = run();
    Expect(c.red == 0, "no target or mouseover: no outline");

    // Nothing but the unit when every cue is off (the baseline the ground cues are measured against).
    Reset();
    g_mock.control.outlineEnabled = 0;
    Stats base = run();
    std::printf("baseline: lit=%u white=%u\n", base.lit, base.white);
    const unsigned baseExtra = base.lit - base.white;   // anti-aliased edge pixels of the unit itself
    Expect(baseExtra < 250 && base.red == 0, "all cues off: only the unit's own pixels");

    // Ground facing chevron (terrain-conforming decal), outline off to isolate it.
    Reset();
    g_mock.control.outlineEnabled = 0;
    g_mock.control.facingMode = wxl::gpu::control::FacingGround;
    g_mock.control.normalScale = 5.0f;   // a default-size decal is only a few pixels in this small scene
    Stats d = run();
    std::printf("ground chevron: lit-nonwhite=%u red=%u\n", d.lit - d.white, d.red);
    Expect(d.lit - d.white > baseExtra + 40, "ground chevron decal drawn");

    // Overlay chevron drawn at EndScene without depth.
    Reset();
    g_mock.control.outlineEnabled = 0;
    g_mock.control.facingMode = wxl::gpu::control::FacingOverlay;
    g_mock.control.normalScale = 5.0f;
    Stats e = run();
    std::printf("overlay chevron: lit-nonwhite=%u\n", e.lit - e.white);
    Expect(e.lit - e.white > baseExtra + 40, "overlay chevron drawn");

    // Danger guide: the frontal cone on the ground for the targeted elite.
    Reset();
    g_mock.control.outlineEnabled = 0;
    g_mock.rank = 1;
    g_mock.readability.enabled = 1;
    g_mock.readability.dangerEnabled = 1;
    g_mock.readability.coneEnabled = 1;
    g_mock.readability.dangerCategory = wxl::game::combat::DangerAllHostile;
    g_mock.readability.coneRange = 12.0f;
    g_mock.readability.coneOpacity = 0.6f;
    Stats f = run();
    std::printf("danger cone: lit-nonwhite=%u\n", f.lit - f.white);
    Expect(f.lit - f.white > baseExtra + 80, "frontal danger cone drawn");
    g_mock.readability.coneEnabled = 0;
    g_mock.readability.ringEnabled = 1;
    g_mock.readability.ringRadius = 6.0f;
    g_mock.readability.ringOpacity = 0.6f;
    Stats g = run();
    std::printf("danger ring: lit-nonwhite=%u\n", g.lit - g.white);
    Expect(g.lit - g.white > baseExtra + 80, "danger ring drawn");

    // A player target stamps through the shared depth buffer (the multisample-matching path): the outline must
    // still appear, and be hidden where nearer geometry already sits in front of the unit.
    Reset();
    g_mock.targetIsPlayer = true;
    g_mock.unitGuid = 0x0000000000000077ull;
    g_mock.target = g_mock.unitGuid;
    Stats h = run();
    std::printf("player outline: red=%u white=%u\n", h.red, h.white);
    Expect(h.red > 60, "player target gets a red outline through the shared depth buffer");
    g_mock.targetIsPlayer = false;
    g_mock.unitGuid = 0x0000F13000000001ull;
    Reset();
}

int main(int argc, char** argv) {
    const bool only = argc > 1;
    const bool wantPlain = !only || !std::strcmp(argv[1], "plain");
    const bool wantMsaa = !only || !std::strcmp(argv[1], "msaa");
    if (wantPlain) {
        Rig rig;
        if (!Open(rig, false)) return 1;
        WxlUnitOutlineInit();
        Suite(rig, "no multisampling");
    }
    if (wantMsaa) {
        // Multisampled swap chain: the mask is multisampled to share the depth buffer, then resolved.
        Rig msaa;
        if (Open(msaa, true)) { WxlUnitOutlineInit(); Suite(msaa, "4x multisampled"); }
        else std::printf("== 4x multisampled: skipped (not supported by this backend)\n");
    }
    std::printf("%s\n", g_failures ? "FAILED" : "all probe checks passed");
    std::fflush(stdout);
    std::_Exit(g_failures ? 2 : 0);   // skip dxvk teardown, which can abort on exit in this sandbox
}
