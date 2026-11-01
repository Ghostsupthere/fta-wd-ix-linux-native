// Render-target and shader helpers behind the supplied wxl-unit-outline's gx facade. No engine dependency, so the
// dxvk-native probe (tools/wxl_unit_outline_gpu_probe.cpp) links the same code the client runs.
#include "events/Event.hpp"
#include "game/gx/Gx.hpp"
#include "outline_shaders.h"

#include <cstdint>

namespace wxl::game::gx {
namespace {
uint64_t Fnv1a(const char* text) {
    uint64_t value = 0xcbf29ce484222325ull;
    for (; *text; ++text) value = (value ^ static_cast<unsigned char>(*text)) * 0x100000001b3ull;
    return value;
}
}  // namespace

void* CompilePixelShader(Device9 device, const char* hlsl, const char* target) {
    (void)target;   // every outline shader is ps_2_0
    if (!device || !hlsl) return nullptr;
    const uint64_t hash = Fnv1a(hlsl);
    for (const auto& entry : wxl_outline_shaders::kEntries) {
        if (entry.sourceHash != hash) continue;
        IDirect3DPixelShader9* shader = nullptr;
        auto* d3d = static_cast<IDirect3DDevice9*>(device.raw());
        return SUCCEEDED(d3d->CreatePixelShader(reinterpret_cast<const DWORD*>(entry.bytes), &shader)) ? shader : nullptr;
    }
    return nullptr;
}

bool EnsureBackbufferTarget(Device9 device, RenderTarget& target, uint32_t d3dFormat) {
    if (!device) return false;
    if (target.surface) return true;
    auto* d3d = static_cast<IDirect3DDevice9*>(device.raw());
    IDirect3DSurface9* backBuffer = nullptr;
    if (FAILED(d3d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer)) || !backBuffer) return false;
    D3DSURFACE_DESC desc;
    backBuffer->GetDesc(&desc);
    backBuffer->Release();

    IDirect3DTexture9* texture = nullptr;
    if (FAILED(d3d->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, static_cast<D3DFORMAT>(d3dFormat),
                                  D3DPOOL_DEFAULT, &texture, nullptr)) || !texture)
        return false;
    IDirect3DSurface9* level0 = nullptr;
    texture->GetSurfaceLevel(0, &level0);
    if (!level0) { texture->Release(); return false; }

    target.texture = texture;
    target.width = static_cast<int>(desc.Width);
    target.height = static_cast<int>(desc.Height);
    target.surface = level0;
    target.resolveSurface = nullptr;
    // A multisampled swap chain also has a multisampled depth buffer; the mask shares it for player
    // silhouettes, so it needs the same sample count, and is resolved to the texture before sampling.
    if (desc.MultiSampleType != D3DMULTISAMPLE_NONE) {
        IDirect3DSurface9* multisampled = nullptr;
        if (SUCCEEDED(d3d->CreateRenderTarget(desc.Width, desc.Height, static_cast<D3DFORMAT>(d3dFormat), desc.MultiSampleType,
                                              desc.MultiSampleQuality, FALSE, &multisampled, nullptr)) && multisampled) {
            target.resolveSurface = level0;
            target.surface = multisampled;
        }
    }
    return true;
}

void ResolveTarget(Device9 device, RenderTarget& target) {
    if (!device || !target.resolveSurface || target.resolveSurface == target.surface) return;
    static_cast<IDirect3DDevice9*>(device.raw())->StretchRect(static_cast<IDirect3DSurface9*>(target.surface), nullptr,
        static_cast<IDirect3DSurface9*>(target.resolveSurface), nullptr, D3DTEXF_NONE);
}

void Release(RenderTarget& target) {
    Release(target.surface);
    if (target.resolveSurface && target.resolveSurface != target.surface) Release(target.resolveSurface);
    Release(target.texture);
    target = RenderTarget{};
}

void DrawFullscreenQuad(Device9 device) {
    if (!device) return;
    auto* d3d = static_cast<IDirect3DDevice9*>(device.raw());
    D3DVIEWPORT9 viewport;
    d3d->GetViewport(&viewport);
    const float w = static_cast<float>(viewport.Width), h = static_cast<float>(viewport.Height);
    // XYZRHW + one texcoord, with the half-pixel offset so texels map 1:1 to pixels.
    struct Vertex { float x, y, z, rhw, u, v; };
    const Vertex quad[4] = {
        {-0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f},
        {w - 0.5f, -0.5f, 0.0f, 1.0f, 1.0f, 0.0f},
        {-0.5f, h - 0.5f, 0.0f, 1.0f, 0.0f, 1.0f},
        {w - 0.5f, h - 0.5f, 0.0f, 1.0f, 1.0f, 1.0f},
    };
    d3d->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
    d3d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex));
}
}  // namespace wxl::game::gx
