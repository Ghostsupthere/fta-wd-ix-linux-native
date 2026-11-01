// Hidden-window native DXVK probe for the quest tracker renderer: synthetic camera, the real navigation
// atlas file when a path is given (otherwise the generated fallback icon); no game, login or assets
// beyond that one file. Usage: wxl_quest_tracker_gpu_probe [path/to/IngameNavigationUI.blp]
#include <d3d9.h>
#include <SDL3/SDL.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include "Engine/Source/ModernAssets/WxlQuestTracker/BlpTexture.hpp"
#include "Engine/Source/ModernAssets/WxlQuestTracker/MarkerRenderer.hpp"
namespace qt = wxl::questtracker;
static void Check(HRESULT result, const char* operation) {
    if (FAILED(result)) { std::fprintf(stderr, "%s failed: 0x%x\n", operation, unsigned(result)); std::exit(1); }
}
constexpr unsigned kSize = 256;
int main(int argc, char** argv) {
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    auto* window = SDL_CreateWindow("FTA quest-tracker probe", kSize, kSize, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    if (!window) { std::fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
    void* library = dlopen("libdxvk_d3d9.so", RTLD_NOW | RTLD_LOCAL);
    if (!library) { std::fprintf(stderr, "DXVK: %s\n", dlerror()); return 1; }
    auto create = reinterpret_cast<IDirect3D9* (*)(UINT)>(dlsym(library, "Direct3DCreate9"));
    auto* d3d = create ? create(D3D_SDK_VERSION) : nullptr;
    if (!d3d) return 1;
    D3DPRESENT_PARAMETERS present{};
    present.BackBufferWidth = present.BackBufferHeight = kSize;
    present.BackBufferFormat = D3DFMT_A8R8G8B8;
    present.BackBufferCount = 1;
    present.SwapEffect = D3DSWAPEFFECT_DISCARD;
    present.Windowed = TRUE;
    present.hDeviceWindow = reinterpret_cast<HWND>(window);
    present.EnableAutoDepthStencil = TRUE;
    present.AutoDepthStencilFormat = D3DFMT_D24S8;
    present.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* device = nullptr;
    Check(d3d->CreateDevice(0, D3DDEVTYPE_HAL, present.hDeviceWindow, D3DCREATE_HARDWARE_VERTEXPROCESSING, &present, &device), "CreateDevice");

    IDirect3DTexture9* atlas = nullptr;
    bool real = false;
    if (argc > 1) {
        std::ifstream file(argv[1], std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
        atlas = qt::CreateBlpTexture(device, bytes.data(), bytes.size());
        real = atlas != nullptr;
        if (!atlas) { std::fprintf(stderr, "could not load %s\n", argv[1]); return 1; }
    } else {
        atlas = qt::CreateFallbackTexture(device);
    }
    if (!atlas) return 1;

    // Camera at the origin looking down +Y (row-vector D3D matrices), standard depth, near 0.1, far 100.
    const float view[16]{1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1};
    const float projection[16]{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 100.f / 99.9f, 1, 0, 0, -10.f / 99.9f, 0};
    const float player[3]{0, 0, 0};

    IDirect3DSurface9 *target = nullptr, *readback = nullptr;
    Check(device->GetRenderTarget(0, &target), "GetRenderTarget");
    Check(device->CreateOffscreenPlainSurface(kSize, kSize, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &readback, nullptr), "readback");

    struct Counts { unsigned lit = 0, minX = kSize, maxX = 0, minY = kSize, maxY = 0; };
    auto run = [&](const qt::State& state, float clearDepth, qt::DrawResult* result) {
        Check(device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF000000, clearDepth, 0), "Clear");
        Check(device->BeginScene(), "BeginScene");
        *result = qt::Draw(device, state, view, projection, 0.0f, 1.0f, player, atlas);
        Check(device->EndScene(), "EndScene");
        Check(device->GetRenderTargetData(target, readback), "GetRenderTargetData");
        D3DLOCKED_RECT lock{};
        Check(readback->LockRect(&lock, nullptr, D3DLOCK_READONLY), "LockRect");
        Counts counts;
        for (unsigned y = 0; y < kSize; ++y)
            for (unsigned x = 0; x < kSize; ++x) {
                const auto* row = reinterpret_cast<const uint32_t*>(static_cast<const unsigned char*>(lock.pBits) + y * lock.Pitch);
                if (row[x] & 0xFFFFFF) {
                    ++counts.lit;
                    counts.minX = std::min(counts.minX, x); counts.maxX = std::max(counts.maxX, x);
                    counts.minY = std::min(counts.minY, y); counts.maxY = std::max(counts.maxY, y);
                }
            }
        Check(readback->UnlockRect(), "UnlockRect");
        return counts;
    };

    int failures = 0;
    auto expect = [&](bool ok, const char* what) { std::printf("  %-58s %s\n", what, ok ? "ok" : "FAILED"); if (!ok) ++failures; };
    std::printf("atlas: %s\n", real ? argv[1] : "generated fallback");
    qt::DrawResult r;

    // 1. A quest marker straight ahead is drawn at the centre, about 32 pixels tall.
    qt::State marker;
    marker.SetMarker(1, 1, 0, 50, 0);
    marker.SetSettings(1, 1, 255);
    Counts a = run(marker, 1.0f, &r);
    std::printf("marker: result=%s lit=%u box=(%u..%u, %u..%u)\n", qt::DrawResultName(r), a.lit, a.minX, a.maxX, a.minY, a.maxY);
    expect(r == qt::DrawResult::Drawn && a.lit > 80, "marker drawn");
    expect(a.minX < kSize / 2 && a.maxX > kSize / 2 && a.minY < kSize / 2 && a.maxY > kSize / 2, "marker covers the centre");
    expect(a.maxY - a.minY <= 40 && a.maxX - a.minX <= 40, "marker keeps a constant screen size (<= 40 px)");

    // 2. The same marker twice as far keeps its size (billboard scaling).
    qt::State far;
    far.SetMarker(1, 1, 0, 90, 0);
    Counts b = run(far, 1.0f, &r);
    std::printf("far marker: lit=%u height=%u\n", b.lit, b.maxY - b.minY);
    expect(r == qt::DrawResult::Drawn && b.lit > 80, "far marker drawn");
    expect(std::abs(int(b.maxY - b.minY) - int(a.maxY - a.minY)) <= 3, "screen size does not change with distance");

    // 3. Opacity setting 0 draws nothing visible; behind the camera and out of range draw nothing.
    qt::State clear;
    clear.SetMarker(1, 1, 0, 50, 0);
    clear.SetSettings(1, 1, 0);
    expect(run(clear, 1.0f, &r).lit == 0, "alpha 0 leaves the target untouched");
    qt::State behind;
    behind.SetMarker(1, 1, 0, -50, 0);
    expect(run(behind, 1.0f, &r).lit == 0, "marker behind the camera not drawn");

    // 4. Depth test: with geometry in front of everything (depth 0) the marker is hidden.
    expect(run(marker, 0.0f, &r).lit == 0, "marker occluded by nearer geometry");

    // 5. Dead player or active corpse: the quest marker is suppressed.
    qt::State dead;
    dead.SetMarker(1, 1, 0, 50, 0);
    dead.SetDead(1);
    expect(run(dead, 1.0f, &r).lit == 0 && r == qt::DrawResult::Nothing, "dead player: no quest marker");

    // 6. Corpse marker is opaque and ignores the quest-marker opacity.
    qt::State corpse;
    corpse.SetSettings(1, 1, 0);
    corpse.SetCorpse(0, 50, 0);
    Counts c = run(corpse, 1.0f, &r);
    std::printf("corpse: lit=%u box=(%u..%u, %u..%u)\n", c.lit, c.minX, c.maxX, c.minY, c.maxY);
    expect(r == qt::DrawResult::Drawn && c.lit > 80, "corpse marker drawn at alpha 0");

    // 7. Diamond: screen space, ignores the depth buffer, placed where asked.
    qt::State diamond;
    diamond.SetDiamond(1, 0.5, 0.0, 0.1, 0.1);
    Counts d = run(diamond, 0.0f, &r);
    std::printf("diamond: lit=%u box=(%u..%u, %u..%u)\n", d.lit, d.minX, d.maxX, d.minY, d.maxY);
    expect(r == qt::DrawResult::Drawn && d.lit > 50, "diamond drawn through the depth buffer");
    expect(d.minX > kSize / 2 + 40 && d.maxX < kSize - 30, "diamond sits right of centre (NDC x 0.5)");
    expect(d.maxY - d.minY <= kSize / 2 * 0.2 + 2, "diamond height follows its NDC half height");

    // 8. The device state is restored: a second draw after everything still works and nothing leaks.
    expect(run(marker, 1.0f, &r).lit == a.lit, "repeat draw is identical (state restored)");

    readback->Release(); target->Release(); atlas->Release(); device->Release(); d3d->Release();
    SDL_DestroyWindow(window); SDL_Quit(); dlclose(library);
    std::printf("%s\n", failures ? "FAILED" : "all probe checks passed");
    return failures ? 2 : 0;
}
