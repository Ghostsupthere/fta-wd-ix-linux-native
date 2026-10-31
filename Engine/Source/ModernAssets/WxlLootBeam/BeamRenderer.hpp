// Procedural loot shafts adapted from manatactical/wxl-loot-beam (fd75b76). GPL-3.0-or-later.
#pragma once
#include <d3d9.h>
#include <cstdint>
#include <cmath>

namespace wxl::scripts::lootbeam
{
    struct Vertex { float x, y, z; DWORD colour; };
    inline constexpr DWORD kVertexFormat = D3DFVF_XYZ | D3DFVF_DIFFUSE;

    // Capture must succeed before any mutation. DrawPrimitiveUP clears stream 0, so restore it explicitly.
    class DeviceState
    {
    public:
        explicit DeviceState(IDirect3DDevice9* device) : device_(device)
        {
            valid_ = device_ && SUCCEEDED(device_->CreateStateBlock(D3DSBT_ALL, &block_)) && block_ &&
                SUCCEEDED(block_->Capture()) && SUCCEEDED(device_->GetStreamSource(0, &stream_, &offset_, &stride_)) &&
                SUCCEEDED(device_->GetTransform(D3DTS_WORLD, &world_)) &&
                SUCCEEDED(device_->GetTransform(D3DTS_VIEW, &view_)) &&
                SUCCEEDED(device_->GetTransform(D3DTS_PROJECTION, &projection_)) &&
                SUCCEEDED(device_->GetViewport(&viewport_)) &&
                SUCCEEDED(device_->GetRenderState(D3DRS_ZWRITEENABLE, &depthWrite_));
        }
        ~DeviceState()
        {
            if (valid_)
            {
                block_->Apply();
                device_->SetStreamSource(0, stream_, offset_, stride_);
                device_->SetTransform(D3DTS_WORLD, &world_);
                device_->SetTransform(D3DTS_VIEW, &view_);
                device_->SetTransform(D3DTS_PROJECTION, &projection_);
                device_->SetViewport(&viewport_);
                // The atmosphere wrapper tracks requested depth-write state outside its backend state block.
                device_->SetRenderState(D3DRS_ZWRITEENABLE, depthWrite_);
            }
            if (stream_) stream_->Release();
            if (block_) block_->Release();
        }
        bool Valid() const { return valid_; }
        D3DVIEWPORT9 const& Viewport() const { return viewport_; }
        DeviceState(DeviceState const&) = delete;
        DeviceState& operator=(DeviceState const&) = delete;
    private:
        IDirect3DDevice9* device_;
        IDirect3DStateBlock9* block_ = nullptr;
        IDirect3DVertexBuffer9* stream_ = nullptr;
        UINT offset_ = 0, stride_ = 0;
        D3DMATRIX world_{}, view_{}, projection_{};
        D3DVIEWPORT9 viewport_{};
        DWORD depthWrite_ = FALSE;
        bool valid_ = false;
    };

    inline bool MatchingDepth(IDirect3DDevice9* device)
    {
        IDirect3DSurface9* colour = nullptr;
        IDirect3DSurface9* depth = nullptr;
        D3DSURFACE_DESC colourDesc{}, depthDesc{};
        bool const valid = SUCCEEDED(device->GetRenderTarget(0, &colour)) && colour &&
            SUCCEEDED(device->GetDepthStencilSurface(&depth)) && depth &&
            SUCCEEDED(colour->GetDesc(&colourDesc)) && SUCCEEDED(depth->GetDesc(&depthDesc)) &&
            colourDesc.Width == depthDesc.Width && colourDesc.Height == depthDesc.Height &&
            colourDesc.MultiSampleType == depthDesc.MultiSampleType &&
            colourDesc.MultiSampleQuality == depthDesc.MultiSampleQuality;
        if (colour) colour->Release();
        if (depth) depth->Release();
        return valid;
    }

    enum class DrawResult { InvalidInput, DepthMismatch, CaptureFailed, InvalidDepthRange, StateFailed, DrawFailed, Drawn };
    inline const char* DrawResultName(DrawResult result) {
        switch (result) {
            case DrawResult::DepthMismatch:return "depth-mismatch";
            case DrawResult::CaptureFailed:return "state-capture-failed";
            case DrawResult::InvalidDepthRange:return "invalid-depth-range";
            case DrawResult::StateFailed:return "state-setup-failed";
            case DrawResult::DrawFailed:return "draw-failed";
            case DrawResult::Drawn:return "drawn";
            default:return "invalid-input";
        }
    }
    inline bool Draw(IDirect3DDevice9* device, Vertex const* vertices, unsigned count,
        float const* view, float const* projection, float const* depthRange = nullptr, DrawResult* report = nullptr)
    {
        auto result = [&](DrawResult value) { if (report) *report=value; return value==DrawResult::Drawn; };
        if (!device || !vertices || !view || !projection || !count || count % 3)
            return result(DrawResult::InvalidInput);
        if (!MatchingDepth(device)) return result(DrawResult::DepthMismatch);
        DeviceState saved(device);
        if (!saved.Valid()) return result(DrawResult::CaptureFailed);
        D3DVIEWPORT9 viewport = saved.Viewport();
        if (depthRange)
        {
            if (!std::isfinite(depthRange[0]) || !std::isfinite(depthRange[1]) ||
                depthRange[0] < 0 || depthRange[1] > 1 || depthRange[1] - depthRange[0] <= 0.01f)
                return result(DrawResult::InvalidDepthRange);
            viewport.MinZ = depthRange[0];
            viewport.MaxZ = depthRange[1];
        }
        D3DMATRIX identity{};
        identity._11 = identity._22 = identity._33 = identity._44 = 1.0f;
        bool okay = true;
        auto set = [&](HRESULT result) { okay = okay && SUCCEEDED(result); };
        set(device->SetViewport(&viewport));
        set(device->SetVertexShader(nullptr));
        set(device->SetPixelShader(nullptr));
        set(device->SetFVF(kVertexFormat));
        set(device->SetStreamSourceFreq(0, 1));
        set(device->SetTransform(D3DTS_WORLD, &identity));
        set(device->SetTransform(D3DTS_VIEW, reinterpret_cast<D3DMATRIX const*>(view)));
        set(device->SetTransform(D3DTS_PROJECTION, reinterpret_cast<D3DMATRIX const*>(projection)));
        // Unbind all samplers, including INTZ left by another pass, before using the matching world depth.
        for (DWORD stage = 0; stage < 16; ++stage)
            set(device->SetTexture(stage, nullptr));
        for (DWORD stage = 0; stage < 4; ++stage)
            set(device->SetTexture(D3DVERTEXTEXTURESAMPLER0 + stage, nullptr));
        set(device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1));
        set(device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE));
        set(device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1));
        set(device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE));
        set(device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE));
        set(device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE));
        set(device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE));
        set(device->SetRenderState(D3DRS_ZFUNC, -projection[14] * projection[11] < 0.0f ?
            D3DCMP_GREATEREQUAL : D3DCMP_LESSEQUAL));
        set(device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE));
        set(device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE));
        set(device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD));
        set(device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA));
        set(device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE));
        set(device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));
        set(device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID));
        set(device->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD));
        set(device->SetRenderState(D3DRS_LIGHTING, FALSE));
        set(device->SetRenderState(D3DRS_VERTEXBLEND, D3DVBF_DISABLE));
        set(device->SetRenderState(D3DRS_INDEXEDVERTEXBLENDENABLE, FALSE));
        set(device->SetRenderState(D3DRS_SPECULARENABLE, FALSE));
        set(device->SetRenderState(D3DRS_FOGENABLE, FALSE));
        set(device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE));
        set(device->SetRenderState(D3DRS_STENCILENABLE, FALSE));
        set(device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE));
        set(device->SetRenderState(D3DRS_CLIPPLANEENABLE, 0));
        set(device->SetRenderState(D3DRS_DEPTHBIAS, 0));
        set(device->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, 0));
        set(device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF));
        if (!okay) return result(DrawResult::StateFailed);
        return result(SUCCEEDED(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, count / 3, vertices, sizeof(Vertex))) ? DrawResult::Drawn : DrawResult::DrawFailed);
    }
}
