// Hidden-window probe for the surface-light shader pack on the real dxvk-native backend. The pack is
// derived from the client's own terrain, M2 and WMO shaders (tools/build-light-shader-pack.py); this
// creates every patched shader on a real D3D9 device so the backend's shader compiler accepts them all
// (register limits, vertex texture fetch, loops and the 'rep'/'break' constructs). It does not start
// the game or alter client data.
//
//   wxl-surface-lights-gpu-probe <light_shaders.pack> [wmo_shaders.pack]
//
// With the WMO layered-material pack it also creates every one of its shaders, including the layered
// variants of the light-patched WMO vertex shaders.
#include <d3d9.h>
#include <SDL3/SDL.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "Engine/Source/ModernAssets/WxlSurfaceLights/LightShaderPack.hpp"
#include "Engine/Source/ModernAssets/WxlWmo/WmoShaderPack.h"

namespace sl = wxl::surfacelights;
static void Require(bool v, const char* what) { if (!v) { std::fprintf(stderr, "FAILED: %s\n", what); std::exit(1); } }

int main(int argc, char** argv) {
    Require(argc >= 2, "usage: probe <light_shaders.pack>");
    sl::LightShaderPack pack;
    std::string error;
    Require(pack.Load(argv[1], error), error.c_str());
    Require(pack.Count() > 0, "the pack holds shaders");
    Require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
    auto* window = SDL_CreateWindow("FTA light probe", 64, 64, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    Require(window != nullptr, SDL_GetError());
    auto* library = dlopen("libdxvk_d3d9.so", RTLD_NOW | RTLD_LOCAL); Require(library != nullptr, dlerror());
    auto create = reinterpret_cast<IDirect3D9* (*)(UINT)>(dlsym(library, "Direct3DCreate9")); Require(create != nullptr, "factory");
    auto* d3d = create(D3D_SDK_VERSION); Require(d3d != nullptr, "D3D9");
    D3DPRESENT_PARAMETERS pp = {}; pp.BackBufferWidth = pp.BackBufferHeight = 64; pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.BackBufferCount = 1; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.Windowed = TRUE; pp.hDeviceWindow = reinterpret_cast<HWND>(window);
    pp.EnableAutoDepthStencil = TRUE; pp.AutoDepthStencilFormat = D3DFMT_D24S8; pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* dev = nullptr;
    Require(SUCCEEDED(d3d->CreateDevice(0, D3DDEVTYPE_HAL, pp.hDeviceWindow, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev)), "device");

    // The light texture itself must be creatable as the runtime creates it.
    IDirect3DTexture9* texture = nullptr;
    Require(SUCCEEDED(dev->CreateTexture(1024, 64, 1, 0, D3DFMT_A32B32G32R32F, D3DPOOL_MANAGED, &texture, nullptr)), "A32B32G32R32F managed texture");
    texture->Release();

    unsigned created[4][2] = {}, failed[4][2] = {};
    for (const sl::LightPackEntry& e : pack.Entries()) {
        HRESULT hr;
        if (e.kind == 0) {
            IDirect3DVertexShader9* vs = nullptr;
            hr = dev->CreateVertexShader(reinterpret_cast<const DWORD*>(e.bytecode.data()), &vs);
            if (vs) vs->Release();
        } else {
            IDirect3DPixelShader9* ps = nullptr;
            hr = dev->CreatePixelShader(reinterpret_cast<const DWORD*>(e.bytecode.data()), &ps);
            if (ps) ps->Release();
        }
        if (SUCCEEDED(hr)) ++created[e.group][e.kind]; else { ++failed[e.group][e.kind]; std::fprintf(stderr, "reject: group %u kind %u hash %016llx hr %08x\n", e.group, e.kind, (unsigned long long)e.originalHash, unsigned(hr)); }
    }
    static const char* names[] = {"", "terrain", "M2", "WMO"};
    unsigned bad = 0;
    for (unsigned g = 1; g <= 3; ++g) {
        std::printf("%-8s vertex %4u created, %u rejected; pixel %4u created, %u rejected\n", names[g], created[g][0], failed[g][0], created[g][1], failed[g][1]);
        bad += failed[g][0] + failed[g][1];
    }
    unsigned wmoVertex = 0, wmoPixel = 0, wmoBad = 0;
    if (argc >= 3) {
        wxl::wmo::ShaderPack wmoPack;
        Require(wmoPack.Load(argv[2], error), error.c_str());
        for (const wxl::wmo::PackEntry& e : wmoPack.Entries()) {
            HRESULT hr;
            if (e.kind == 0) { IDirect3DVertexShader9* vs = nullptr; hr = dev->CreateVertexShader(reinterpret_cast<const DWORD*>(e.bytecode.data()), &vs); if (vs) vs->Release(); ++wmoVertex; }
            else { IDirect3DPixelShader9* ps = nullptr; hr = dev->CreatePixelShader(reinterpret_cast<const DWORD*>(e.bytecode.data()), &ps); if (ps) ps->Release(); ++wmoPixel; }
            if (FAILED(hr)) { ++wmoBad; std::fprintf(stderr, "reject: WMO layered entry kind %u hash %016llx hr %08x\n", e.kind, (unsigned long long)e.originalHash, unsigned(hr)); }
        }
        std::printf("WMO layered pack: %u vertex and %u pixel shaders created, %u rejected\n", wmoVertex - wmoBad, wmoPixel, wmoBad);
        // Each light-patched WMO vertex shader has a layered variant under the bytecode the engine will hold.
        unsigned covered = 0, total = 0;
        for (const sl::LightPackEntry& e : pack.Entries())
            if (e.group == sl::kGroupWmo && e.kind == 0) { ++total; covered += wmoPack.FindVertex(e.bytecode.data(), e.bytecode.size() * 4) != nullptr; }
        std::printf("light-patched WMO vertex shaders with a layered variant: %u of %u\n", covered, total);
        bad += wmoBad;
        Require(argc < 4 || covered == total, "every light-patched WMO shader has a layered variant");
        if (covered != total) std::printf("note: %u light-patched WMO shaders have no layered variant\n", total - covered);
    }
    dev->Release(); d3d->Release(); SDL_DestroyWindow(window);
    Require(bad == 0, "the backend rejected a patched shader");
    std::printf("PASS: all %zu patched shaders are accepted by the backend\n", pack.Count());
    return 0;
}
