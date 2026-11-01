// Immediate native-batch silhouette stamping; legacy visibility policy. GPL-3.0-or-later.
#pragma once

#include "events/Event.hpp"

#include <d3d9.h>

namespace wxl::scripts::outline
{
    class ImmediateSilhouette final
    {
    public:
        static bool Stamp(IDirect3DDevice9* device, IDirect3DSurface9* mask,
            IDirect3DStateBlock9* state, events::M2BatchDrawArgs const& batch,
            IDirect3DPixelShader9* color, IDirect3DPixelShader9* cutout, float const rgba[4],
            bool player, bool& maskCleared)
        {
            if (!device || !mask || !state || !color || !cutout || !rgba)
                return false;
            RestoreState restore(device, state);
            if (!restore.Ready() || (player && !restore.Depth()))
                return false;

            DWORD alphaRef = 0;
            DWORD alphaTest = 0;
            device->GetRenderState(D3DRS_ALPHAREF, &alphaRef);
            device->GetRenderState(D3DRS_ALPHATESTENABLE, &alphaTest);
            bool const alphaCutout = alphaTest != 0 && alphaRef >= 8;

            if (FAILED(device->SetRenderTarget(0, mask)) ||
                FAILED(device->SetDepthStencilSurface(player ? restore.Depth() : nullptr)))
                return false;

            // Preserve pre-occlusion behavior: NPCs are visible through the world. Players use
            // only depth already present at their native batch, not later world geometry.
            device->SetRenderState(D3DRS_ZENABLE, player ? D3DZB_TRUE : D3DZB_FALSE);
            if (player)
            {
                device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
                device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
            }
            if (!maskCleared)
            {
                if (FAILED(device->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0)))
                    return false;
                maskCleared = true;
            }
            device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
            device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
            device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xFu);
            device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
            if (FAILED(device->SetPixelShader(alphaCutout ? cutout : color)) ||
                FAILED(device->SetPixelShaderConstantF(0, rgba, 1)))
                return false;

            // Consume even transient CPU-skinned geometry immediately, before the native client
            // reuses its buffers. No deferred state-block list or per-target batch limit remains.
            return SUCCEEDED(device->DrawIndexedPrimitive(static_cast<D3DPRIMITIVETYPE>(batch.primType),
                batch.baseVertex, batch.minIndex, batch.numVerts, batch.startIndex, batch.primCount));
        }

    private:
        class RestoreState final
        {
        public:
            RestoreState(IDirect3DDevice9* device, IDirect3DStateBlock9* state) : device_(device), state_(state)
            {
                captured_ = SUCCEEDED(state_->Capture());
                if (!captured_)
                    return;
                ready_ = SUCCEEDED(device_->GetRenderTarget(0, &color_)) && color_ &&
                    SUCCEEDED(device_->GetViewport(&viewport_));
                if (ready_)
                    device_->GetDepthStencilSurface(&depth_);
            }

            ~RestoreState()
            {
                // D3DSBT_ALL does not capture color/depth bindings. Restore those explicitly,
                // then the viewport, shaders, constants, streams and all other captured state.
                if (ready_)
                {
                    device_->SetRenderTarget(0, color_);
                    device_->SetDepthStencilSurface(depth_);
                    device_->SetViewport(&viewport_);
                }
                if (captured_)
                    state_->Apply();
                if (depth_)
                    depth_->Release();
                if (color_)
                    color_->Release();
            }

            bool Ready() const { return ready_; }
            IDirect3DSurface9* Depth() const { return depth_; }

        private:
            IDirect3DDevice9* device_;
            IDirect3DStateBlock9* state_;
            IDirect3DSurface9* color_ = nullptr;
            IDirect3DSurface9* depth_ = nullptr;
            D3DVIEWPORT9 viewport_{};
            bool captured_ = false;
            bool ready_ = false;
        };
    };
}
