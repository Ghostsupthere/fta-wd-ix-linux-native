// D3D9 draw for the quest billboard, corpse marker and screen-space diamond, ported from the supplied
// wxl-quest-marker (GPL-3.0-or-later, Copyright (C) 2026 WarcraftXL). Takes plain arguments (no engine
// globals) so the same code runs in the dxvk-native probe. Setup follows the loot-beam renderer: it
// captures and restores the whole device state and respects a reversed-depth projection.
#pragma once

#include <d3d9.h>

#include <cmath>

#include "Engine/Source/ModernAssets/WxlLootBeam/BeamRenderer.hpp"
#include "Engine/Source/ModernAssets/WxlQuestTracker/TrackerState.hpp"

namespace wxl::questtracker {

enum class DrawResult { Nothing, DepthMismatch, InvalidDepthRange, CaptureFailed, InvalidViewport, StateFailed, Drawn };

inline const char* DrawResultName(DrawResult result) {
    switch (result) {
        case DrawResult::DepthMismatch: return "depth-mismatch";
        case DrawResult::InvalidDepthRange: return "invalid-depth-range";
        case DrawResult::CaptureFailed: return "state-capture-failed";
        case DrawResult::InvalidViewport: return "invalid-viewport";
        case DrawResult::StateFailed: return "state-setup-failed";
        case DrawResult::Drawn: return "drawn";
        default: return "nothing-to-draw";
    }
}

// What the supplied OnWorldRenderEnd decides before touching the device.
inline bool MarkersVisible(const State& state) {
    const Settings& s = state.GetSettings();
    return !state.Markers().empty() && s.enabled && s.showMarker && !state.CorpseActive() && !state.Dead();
}
inline bool NeedsDraw(const State& state) {
    return state.CorpseActive() || state.DiamondActive() || MarkersVisible(state);
}

namespace render_detail {
struct Vertex {
    float x, y, z;
    DWORD color;
    float u, v;
};
constexpr DWORD kFormat = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;

inline void Quad(Vertex out[6], float x0, float x1, float y0, float y1, float z, DWORD color, const Uv& uv) {
    out[0] = {x0, y1, z, color, uv.u0, uv.v0};
    out[1] = {x0, y0, z, color, uv.u0, uv.v1};
    out[2] = {x1, y1, z, color, uv.u1, uv.v0};
    out[3] = out[2];
    out[4] = out[1];
    out[5] = {x1, y0, z, color, uv.u1, uv.v1};
}

// Shared state for every tracker draw: beam::Draw's reversed-depth-aware setup plus the textured
// fixed-function stage. Stages above 0 are disabled so a stale multitexture state cannot tint the icon.
inline bool SetupStates(IDirect3DDevice9* device, IDirect3DTexture9* texture, const float* view, const float* projection,
                        const D3DVIEWPORT9& viewport) {
    D3DMATRIX identity{};
    identity._11 = identity._22 = identity._33 = identity._44 = 1.0f;
    bool okay = true;
    auto set = [&](HRESULT result) { okay = okay && SUCCEEDED(result); };
    set(device->SetViewport(&viewport));
    set(device->SetVertexShader(nullptr));
    set(device->SetPixelShader(nullptr));
    set(device->SetStreamSourceFreq(0, 1));
    set(device->SetTransform(D3DTS_WORLD, &identity));
    set(device->SetTransform(D3DTS_VIEW, reinterpret_cast<const D3DMATRIX*>(view)));
    set(device->SetTransform(D3DTS_PROJECTION, reinterpret_cast<const D3DMATRIX*>(projection)));
    for (DWORD stage = 0; stage < 16; ++stage) set(device->SetTexture(stage, nullptr));
    for (DWORD stage = 0; stage < 4; ++stage) set(device->SetTexture(D3DVERTEXTEXTURESAMPLER0 + stage, nullptr));
    set(device->SetTexture(0, texture));
    set(device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE));
    set(device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE));
    set(device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE));
    set(device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE));
    set(device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE));
    set(device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE));
    for (DWORD stage = 1; stage < 8; ++stage) {
        set(device->SetTextureStageState(stage, D3DTSS_COLOROP, D3DTOP_DISABLE));
        set(device->SetTextureStageState(stage, D3DTSS_ALPHAOP, D3DTOP_DISABLE));
    }
    // The atlas has one mip level: no mip filtering.
    set(device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE));
    set(device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR));
    set(device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR));
    set(device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP));
    set(device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP));
    set(device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE));
    set(device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE));
    set(device->SetRenderState(D3DRS_ZFUNC, -projection[14] * projection[11] < 0.0f ? D3DCMP_GREATEREQUAL : D3DCMP_LESSEQUAL));
    set(device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE));
    set(device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE));
    set(device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD));
    set(device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA));
    set(device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA));
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
    set(device->SetFVF(kFormat));
    return okay;
}
}  // namespace render_detail

// view: world-space view matrix (eye translation included). projection: the engine's matrix. minZ/maxZ:
// the depth range of the world pass's viewport. player: position for the range check.
inline DrawResult Draw(IDirect3DDevice9* device, const State& state, const float* view, const float* projection,
                       float minZ, float maxZ, const float player[3], IDirect3DTexture9* atlas) {
    if (!device || !atlas || !NeedsDraw(state)) return DrawResult::Nothing;
    if (!scripts::lootbeam::MatchingDepth(device)) return DrawResult::DepthMismatch;
    if (!std::isfinite(minZ) || !std::isfinite(maxZ) || minZ < 0 || maxZ > 1 || maxZ - minZ <= 0.01f)
        return DrawResult::InvalidDepthRange;

    scripts::lootbeam::DeviceState saved(device);
    if (!saved.Valid()) return DrawResult::CaptureFailed;
    D3DVIEWPORT9 viewport = saved.Viewport();
    if (viewport.Height == 0) return DrawResult::InvalidViewport;
    viewport.MinZ = minZ;
    viewport.MaxZ = maxZ;

    // Standard depth gets the far plane pushed out so distant markers are not clipped; a reversed-depth
    // matrix is used as the engine built it.
    float extended[16];
    const float* drawProjection = projection;
    if (-projection[14] * projection[11] > 0.0f && ExtendFarPlane(projection, extended)) drawProjection = extended;

    if (!render_detail::SetupStates(device, atlas, view, drawProjection, viewport)) return DrawResult::StateFailed;
    const float height = static_cast<float>(viewport.Height);
    float world[16];

    // Corpse marker: always fully opaque. The icon's art has translucent edge texels that LINEAR filtering
    // turns into a dark shimmer, so cut them out with an alpha test (as the supplied draw does).
    if (state.CorpseActive()) {
        const float* c = state.Corpse();
        Projected p;
        if (Project(view, projection, c[0], c[1], c[2], p) && p.inFront &&
            BillboardWorld(view, projection, height, c[0], c[1], c[2], p.viewZ, world)) {
            device->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
            device->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
            device->SetRenderState(D3DRS_ALPHAREF, 160);
            render_detail::Vertex quad[6];
            render_detail::Quad(quad, -kCorpseIconAspect, kCorpseIconAspect, -1.0f, 1.0f, 0.0f, 0xFFFFFFFFu, kCorpseIconUv);
            device->SetTransform(D3DTS_WORLD, reinterpret_cast<const D3DMATRIX*>(world));
            device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, quad, sizeof(render_detail::Vertex));
            device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        }
    }

    // Objective-tracker diamond: a screen-space quad with no depth test, from the same atlas.
    if (state.DiamondActive()) {
        D3DMATRIX identity{};
        identity._11 = identity._22 = identity._33 = identity._44 = 1.0f;
        device->SetTransform(D3DTS_WORLD, &identity);
        device->SetTransform(D3DTS_VIEW, &identity);
        device->SetTransform(D3DTS_PROJECTION, &identity);
        device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
        const float* d = state.Diamond();
        render_detail::Vertex quad[6];
        render_detail::Quad(quad, d[0] - d[2], d[0] + d[2], d[1] - d[3], d[1] + d[3], 0.5f, 0xFFFFFFFFu, kQuestIconUv);
        device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, quad, sizeof(render_detail::Vertex));
        device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
        device->SetTransform(D3DTS_VIEW, reinterpret_cast<const D3DMATRIX*>(view));
        device->SetTransform(D3DTS_PROJECTION, reinterpret_cast<const D3DMATRIX*>(drawProjection));
    }

    // Quest markers: one billboard per active marker within range and in front of the camera.
    if (MarkersVisible(state)) {
        const DWORD color = (static_cast<DWORD>(state.GetSettings().alpha) << 24) | 0xFFFFFFu;
        render_detail::Vertex quad[6];
        render_detail::Quad(quad, -kQuestIconAspect, kQuestIconAspect, -1.0f, 1.0f, 0.0f, color, kQuestIconUv);
        for (const Marker& m : state.Markers()) {
            if (!m.active) continue;
            const float dx = m.x - player[0], dy = m.y - player[1], dz = m.z - player[2];
            if (dx * dx + dy * dy + dz * dz > kMaxMarkerDistance * kMaxMarkerDistance) continue;
            Projected p;
            if (!Project(view, projection, m.x, m.y, m.z, p) || !p.inFront ||
                !BillboardWorld(view, projection, height, m.x, m.y, m.z, p.viewZ, world))
                continue;
            device->SetTransform(D3DTS_WORLD, reinterpret_cast<const D3DMATRIX*>(world));
            device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, quad, sizeof(render_detail::Vertex));
        }
    }
    return DrawResult::Drawn;
}

}  // namespace wxl::questtracker
