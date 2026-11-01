// Native stand-in for the core's Gx facade used by the supplied wxl-unit-outline. Device9 wraps the
// live IDirect3DDevice9 (dxvk-native) with the same method names; the render-target and shader helpers
// are implemented in NativeFacade.cpp.
#pragma once

#include <cstdint>

#include "../../events/Event.hpp"

namespace wxl::game::gx {

namespace rs {
constexpr unsigned kZEnable = 7, kZWrite = 14, kSrcBlend = 19, kDestBlend = 20, kCullMode = 22, kZFunc = 23,
                   kAlphaBlend = 27, kStencilEnable = 52;
}
namespace blend { constexpr unsigned kSrcAlpha = 5, kInvSrcAlpha = 6; }
namespace cmp { constexpr unsigned kLessEqual = 4; }
namespace cull { constexpr unsigned kNone = 1; }
namespace clear { constexpr unsigned kTarget = 1, kZBuffer = 2, kStencil = 4; }
namespace prim { constexpr int kTriangleStrip = 5; }

// Releases a COM object obtained from a Get* call; null is ignored.
inline void Release(void* object) {
    if (object) static_cast<IUnknown*>(object)->Release();
}

class Device9 {
public:
    explicit Device9(void* device) : device_(static_cast<IDirect3DDevice9*>(device)) {}
    void* raw() const { return device_; }
    explicit operator bool() const { return device_ != nullptr; }

    long SetRenderState(unsigned state, unsigned value) { return device_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(state), value); }
    unsigned GetRenderState(unsigned state) {
        DWORD value = 0;
        device_->GetRenderState(static_cast<D3DRENDERSTATETYPE>(state), &value);
        return value;
    }
    long SetRenderTarget(unsigned index, void* surface) { return device_->SetRenderTarget(index, static_cast<IDirect3DSurface9*>(surface)); }
    long GetRenderTarget(unsigned index, void** out) { return device_->GetRenderTarget(index, reinterpret_cast<IDirect3DSurface9**>(out)); }
    long SetDepthStencil(void* surface) { return device_->SetDepthStencilSurface(static_cast<IDirect3DSurface9*>(surface)); }
    long GetDepthStencil(void** out) { return device_->GetDepthStencilSurface(reinterpret_cast<IDirect3DSurface9**>(out)); }
    long SetPixelShader(void* shader) { return device_->SetPixelShader(static_cast<IDirect3DPixelShader9*>(shader)); }
    long GetPixelShader(void** out) { return device_->GetPixelShader(reinterpret_cast<IDirect3DPixelShader9**>(out)); }
    long SetPixelShaderConstantF(unsigned start, const float* data, unsigned count) { return device_->SetPixelShaderConstantF(start, data, count); }
    long SetVertexShader(void* shader) { return device_->SetVertexShader(static_cast<IDirect3DVertexShader9*>(shader)); }
    long SetTexture(unsigned stage, void* texture) { return device_->SetTexture(stage, static_cast<IDirect3DBaseTexture9*>(texture)); }
    long Clear(unsigned rectCount, const void* rects, unsigned flags, unsigned color, float z, unsigned stencil) {
        return device_->Clear(rectCount, static_cast<const D3DRECT*>(rects), flags, color, z, stencil);
    }
    long DrawIndexedPrimitive(int primType, int baseVertex, unsigned minIndex, unsigned numVerts, unsigned startIndex, unsigned primCount) {
        return device_->DrawIndexedPrimitive(static_cast<D3DPRIMITIVETYPE>(primType), baseVertex, minIndex, numVerts, startIndex, primCount);
    }
    long GetViewport(void* viewport) { return device_->GetViewport(static_cast<D3DVIEWPORT9*>(viewport)); }
    long SetViewport(const void* viewport) { return device_->SetViewport(static_cast<const D3DVIEWPORT9*>(viewport)); }

private:
    IDirect3DDevice9* device_;
};

// The silhouette mask. `surface` is what is drawn into: a multisampled target when the back buffer is
// multisampled (so it can share the world depth buffer), otherwise level 0 of `texture`. `texture` is the
// plain texture the edge pass samples. ResolveTarget copies `surface` into `texture` when they differ.
struct RenderTarget {
    void* texture = nullptr;
    void* surface = nullptr;
    int width = 0;
    int height = 0;
    void* resolveSurface = nullptr;   // level 0 of `texture`; set only for the multisampled case
};

// Compiles nothing: looks up the offline-compiled bytecode for this exact HLSL text (see
// tools/build-outline-shaders.py) and creates the pixel shader. Null when the text is unknown.
void* CompilePixelShader(Device9 device, const char* hlsl, const char* target);
bool EnsureBackbufferTarget(Device9 device, RenderTarget& target, uint32_t d3dFormat);
void ResolveTarget(Device9 device, RenderTarget& target);
void Release(RenderTarget& target);
void DrawFullscreenQuad(Device9 device);

}  // namespace wxl::game::gx
