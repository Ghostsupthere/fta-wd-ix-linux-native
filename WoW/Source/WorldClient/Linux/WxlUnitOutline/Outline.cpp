// wxl-unit-outline: reaction-colored silhouette outline on the mouseover and target units.
// Copyright (C) 2026 WarcraftXL
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

#include "Outline.hpp"
#include "Hlsl.hpp"
#include "ImmediateSilhouette.hpp"

#include "common/Config.hpp"
#include "core/Logger.hpp"
#include "game/unit/Unit.hpp"
#include "game/world/Pick.hpp"
#include "game/world/World.hpp"
#include "game/camera/Camera.hpp"
#include "game/m2/M2.hpp"
#include "gpu/ControlImports.hpp"

#include <d3d9.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

#ifdef GetObject
#undef GetObject
#endif

namespace wxl::scripts::outline
{
    namespace gx    = wxl::game::gx;
    namespace ev    = wxl::events;
    namespace cam   = wxl::game::camera;
    namespace m2    = wxl::game::m2;
    namespace world = wxl::game::world;
    namespace unit  = wxl::game::unit;
    namespace combat = wxl::game::combat;
    constexpr uint32_t kFmtA8R8G8B8 = 21;
    int g_outlineResourceState = -1;
    bool g_loggedOutlineTarget = false;
    bool g_loggedOutlineStamp = false;
    bool g_loggedOutlineComposite = false;
    bool g_loggedFrontalGuide = false;

    bool SampleGroundZ(float x, float y, float referenceZ, float& outZ) noexcept
    {
        __try
        {
            // Start close enough to the unit's feet to avoid roofs and high doodads, while still
            // allowing ordinary uphill terrain. The 3.3.5a wrapper returns 1 for terrain, 2 for
            // M2/doodad geometry and 3 for WMO geometry; only ground-bearing surfaces are accepted.
            const world::Vec3 start{ x, y, referenceZ + 4.0f };
            const world::Vec3 end{ x, y, referenceZ - 32.0f };
            world::WorldHit hit{};
            const int hitType = world::IntersectSegment(start, end, hit);
            if ((hitType != 1 && hitType != 3) ||
                !std::isfinite(hit.pos.z) || hit.pos.z > referenceZ + 4.05f ||
                hit.pos.z < referenceZ - 32.05f)
                return false;
            outZ = hit.pos.z + 0.045f;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    class ScopedDeviceState final
    {
    public:
        explicit ScopedDeviceState(void* cachedState)
        {
            state_ = static_cast<IDirect3DStateBlock9*>(cachedState);
            captured_ = state_ && SUCCEEDED(state_->Capture());
        }

        ~ScopedDeviceState() { Restore(); }

        explicit operator bool() const { return captured_; }

        void Restore()
        {
            if (!captured_) return;
            state_->Apply();
            captured_ = false;
        }

    private:
        IDirect3DStateBlock9* state_ = nullptr;
        bool captured_ = false;
    };

    Outline::Outline()
    {
        if (!wxl::config::Flag("WXL_UNIT_OVERLAYS", nullptr))
            return;

        // Post-processing runs at the default priority. Composite the outline afterward so FXAA does not
        // blur or destabilize the silhouette edge.
        on<&Outline::OnEndScene>(ev::Event::OnEndScene, 100);
        on<&Outline::OnWorldLeave>(ev::Event::OnWorldLeave);
        on<&Outline::OnM2Batch>(ev::Event::OnM2BatchDraw);
        on<&Outline::OnDeviceLost>(ev::Event::OnDeviceLost);
        on<&Outline::OnDeviceReset>(ev::Event::OnDeviceReset);
        RefreshSettings();
    }

    Outline::~Outline()
    {
        ReleaseShaders();
        ReleaseMask();
        ReleaseStateBlock();
        ReleaseChevronTextures();
    }

    void Outline::ReleaseShaders()
    {
        for (void** shader : { &colorPS_, &cutoutPS_, &edgePS_, &chevronPS_, &overlayChevronPS_ })
        {
            if (*shader)
                static_cast<IDirect3DPixelShader9*>(*shader)->Release();
            *shader = nullptr;
        }
        shaderDevice_ = nullptr;
    }

    void Outline::ReleaseMask()
    {
        gx::Release(mask_);
        maskDevice_ = nullptr;
        maskCleared_ = false;
    }

    void Outline::ReleaseStateBlock()
    {
        if (stateBlock_)
            static_cast<IDirect3DStateBlock9*>(stateBlock_)->Release();
        stateBlock_ = nullptr;
        stateDevice_ = nullptr;
    }

    void Outline::ReleaseChevronTextures()
    {
        for (void*& texture : chevronTextures_)
        {
            if (texture)
                static_cast<IDirect3DTexture9*>(texture)->Release();
            texture = nullptr;
        }
        chevronDevice_ = nullptr;
    }

    void Outline::RefreshSettings()
    {
        gpu::control::Settings next{};
        if (WxlGetControlSettings(&next, sizeof(next)) &&
            next.abiSize == sizeof(next) && next.apiVersion == gpu::control::kApiVersion)
            settings_ = next;

        readability_ = combat::Snapshot();
        // Chevron visibility and frontal-guide visibility are separate feature
        // switches, but both ground cues must obey one depth/overlay mode. When
        // chevrons are Off, keep an independently enabled guide depth-tested.
        readability_.dangerVisibility = settings_.facingMode == gpu::control::FacingOverlay
            ? combat::DangerOverlay : combat::DangerGround;
    }

    bool Outline::EnsureStateBlock(gx::Device9 dev)
    {
        auto* d = static_cast<IDirect3DDevice9*>(dev.raw());
        if (!d) return false;
        if (stateDevice_ != d)
            ReleaseStateBlock();
        if (!stateBlock_)
        {
            IDirect3DStateBlock9* block = nullptr;
            if (FAILED(d->CreateStateBlock(D3DSBT_ALL, &block)) || !block)
                return false;
            stateBlock_ = block;
            stateDevice_ = d;
        }
        return true;
    }

    void Outline::OnDeviceLost(const ev::DeviceResetArgs& a)
    {
        if (!a.device || shaderDevice_ == a.device)
            ReleaseShaders();
        if (!a.device || maskDevice_ == a.device)
            ReleaseMask();
        if (!a.device || stateDevice_ == a.device)
            ReleaseStateBlock();
        if (!a.device || chevronDevice_ == a.device)
            ReleaseChevronTextures();
        maskCleared_ = false;
        stateCapturedThisFrame_ = false;
    }

    void Outline::OnDeviceReset(const ev::DeviceResetArgs& a)
    {
        // Reset normally follows OnDeviceLost, but release defensively in case a caller emits only the
        // successful-reset event. Recreate lazily once the next render call has a fully usable device.
        if (!a.device || shaderDevice_ == a.device)
            ReleaseShaders();
        if (!a.device || maskDevice_ == a.device)
            ReleaseMask();
        if (!a.device || stateDevice_ == a.device)
            ReleaseStateBlock();
        if (!a.device || chevronDevice_ == a.device)
            ReleaseChevronTextures();
        maskCleared_ = false;
        stateCapturedThisFrame_ = false;
    }

    void Outline::ColorForReaction(int reaction, float* c)
    {
        if (reaction < 2)      { c[0] = 1.0f; c[1] = 0.0f; c[2] = 0.0f; } // hostile
        else if (reaction < 4) { c[0] = 1.0f; c[1] = 1.0f; c[2] = 0.0f; } // neutral
        else                   { c[0] = 0.0f; c[1] = 1.0f; c[2] = 0.0f; } // friendly
        c[3] = 1.0f;
    }

    bool Outline::EnsureOutlineResources(gx::Device9 dev)
    {
        if (!settings_.outlineEnabled) return false;
        if (maskDevice_ && maskDevice_ != dev.raw())
            ReleaseMask();
        if (shaderDevice_ && shaderDevice_ != dev.raw())
            ReleaseShaders();
        if (!shaderDevice_) shaderDevice_ = dev.raw();
        if (!colorPS_)  colorPS_ = gx::CompilePixelShader(dev, kColorHLSL, "ps_2_0");
        if (!cutoutPS_) cutoutPS_ = gx::CompilePixelShader(dev, kCutoutColorHLSL, "ps_2_0");
        if (!edgePS_)   edgePS_ = gx::CompilePixelShader(dev, kEdgeHLSL, "ps_2_0");
        if (!mask_.surface && gx::EnsureBackbufferTarget(dev, mask_, kFmtA8R8G8B8))
            maskDevice_ = dev.raw();
        const bool ready = colorPS_ && cutoutPS_ && edgePS_ && mask_.surface;
        const int state = ready ? 1 : 0;
        if (state != g_outlineResourceState)
        {
            g_outlineResourceState = state;
            WLOG_INFO("unit-outline: resources %s (color=%p cutout=%p edge=%p mask=%p %ux%u)",
                      ready ? "ready" : "unavailable", colorPS_, cutoutPS_, edgePS_,
                      mask_.surface, mask_.width, mask_.height);
        }
        return ready;
    }

    bool Outline::EnsureMarkerResources(gx::Device9 dev)
    {
        if (shaderDevice_ && shaderDevice_ != dev.raw())
            ReleaseShaders();
        if (!shaderDevice_) shaderDevice_ = dev.raw();
        if (settings_.facingMode == gpu::control::FacingGround)
        {
            if (!chevronPS_) chevronPS_ = gx::CompilePixelShader(dev, kChevronHLSL, "ps_2_0");
            return chevronPS_ != nullptr;
        }
        if (settings_.facingMode == gpu::control::FacingOverlay)
        {
            if (!overlayChevronPS_)
                overlayChevronPS_ = gx::CompilePixelShader(dev, kOverlayChevronHLSL, "ps_2_0");
            return overlayChevronPS_ != nullptr;
        }
        return false;
    }

    int Outline::FindTarget(void* model) const
    {
        for (int hop = 0; model && hop < 8; ++hop, model = unit::ModelParent(model))
            for (int i = 0; i < count_; ++i)
                if (targets_[i].model == model) return i;
        return -1;
    }

    bool Outline::ValidateTarget(int targetIndex) const
    {
        if (targetIndex < 0 || targetIndex >= count_) return false;

        const Target& target = targets_[targetIndex];
        if (!target.guid || !target.unit || !target.model) return false;

        // The target list survives from one EndScene to the next. A despawn, display swap or world teardown
        // can invalidate both cached pointers during that interval, so resolve the GUID again before any draw
        // path dereferences them. A recreated object/model is picked up by the next RebuildTargets instead of
        // being mixed into the prior frame's snapshot.
        void* const liveUnit = world::ResolveObject(
            target.guid, target.isPlayer ? world::kTypeMaskPlayer : world::kTypeMaskUnit);
        return liveUnit == target.unit && liveUnit && unit::Model(liveUnit) == target.model;
    }

    void Outline::AddTarget(unsigned long long guid, void* player, bool drawOutline, bool drawMarker,
                            bool currentTarget, const float* colorOverride)
    {
        const bool drawDanger = currentTarget && readability_.enabled && readability_.dangerEnabled;
        if (!guid || (!drawOutline && !drawMarker && !drawDanger)) return;
        const bool isPlayer = (guid >> 32) == 0;

        void* obj = world::ResolveObject(guid, isPlayer ? world::kTypeMaskPlayer : world::kTypeMaskUnit);
        if (!obj) return;

        void* model = unit::Model(obj);
        if (!model) return;

        for (int i = 0; i < count_; ++i)
            if (targets_[i].model == model)
            {
                // Target and mouseover can be the same GUID but request different features.
                targets_[i].drawOutline = targets_[i].drawOutline || drawOutline;
                targets_[i].drawMarker = targets_[i].drawMarker || drawMarker;
                targets_[i].currentTarget = targets_[i].currentTarget || currentTarget;
                return;
            }
        if (count_ >= kMaxTargets) return;

        const int reaction = player ? unit::Reaction(obj, player) : 5;
        targets_[count_].guid     = guid;
        targets_[count_].model    = model;
        targets_[count_].unit     = obj;
        targets_[count_].isPlayer = isPlayer;
        targets_[count_].drawOutline = drawOutline;
        targets_[count_].drawMarker = drawMarker;
        targets_[count_].markerDrawn = false;
        targets_[count_].dangerAttempted = false;
        targets_[count_].currentTarget = currentTarget;
        targets_[count_].creatureRank = isPlayer ? 0 : unit::CreatureRank(obj);
        if (colorOverride)
            std::memcpy(targets_[count_].color, colorOverride, sizeof(targets_[count_].color));
        else
            ColorForReaction(reaction, targets_[count_].color);

        if (readability_.enabled && currentTarget)
        {
            float accent[3]{};
            const int rank = targets_[count_].creatureRank;
            if (readability_.castContrast && readability_.castState != 0)
            {
                combat::PaletteColor(readability_.castState == 2 ? readability_.protectedPalette :
                    readability_.castPalette, accent);
                targets_[count_].color[0] = accent[0];
                targets_[count_].color[1] = accent[1];
                targets_[count_].color[2] = accent[2];
            }
            else if (readability_.bossContrast && rank == 3)
            {
                combat::PaletteColor(readability_.bossPalette, accent);
                targets_[count_].color[0] = accent[0];
                targets_[count_].color[1] = accent[1];
                targets_[count_].color[2] = accent[2];
            }
        }
        ++count_;
    }

    void Outline::RebuildTargets()
    {
        void* player = world::ResolveObject(world::ActivePlayerGuid(), world::kTypeMaskPlayer);
        count_ = 0;
        const bool drawOutline = settings_.outlineEnabled != 0;
        const bool markersActive = settings_.facingMode != gpu::control::FacingOff;

        // Unit outlines continue to follow target and mouseover independently of the facing-marker
        // visibility switches. The shared list is only a two-entry storage optimization now.
        // The retained frontal guide turns on the readability settings container, but its
        // retired combat-outline flags must not suppress the independent base outline pass.
        const bool mouseoverOutline = drawOutline;
        const bool targetOutline = drawOutline;
        // Add the selected target first. When target and mouseover resolve to the same model, this also
        // guarantees that cast/boss contrast wins over the ordinary mouseover reaction color.
        AddTarget(world::TargetGuid(), player, targetOutline,
                  markersActive && settings_.showTarget != 0, true);
        AddTarget(world::MouseoverGuid(), player, mouseoverOutline,
                  markersActive && settings_.showMouseover != 0, false);
        if (!g_loggedOutlineTarget && drawOutline && count_ > 0)
        {
            g_loggedOutlineTarget = true;
            WLOG_INFO("unit-outline: target snapshot active (count=%d readability=%d)",
                      count_, readability_.enabled);
        }
    }

    bool Outline::IsOpaqueBatch(gx::Device9 dev) const
    {
        if (!dev) return false;

        // Attached particles, glows and billboard effects render alpha-blended quads through the same
        // model context. Effects that decline to write depth are likewise not solid silhouette geometry.
        // Letting either class into the mask makes the edge pass outline a broad card instead of the unit.
        return dev.GetRenderState(gx::rs::kAlphaBlend) == 0 &&
               dev.GetRenderState(gx::rs::kZWrite) != 0;
    }

    // Draw the model again into the mask render target, using one reusable state block rather than
    // allocating and destroying a D3DSBT_ALL object for every material batch.
    void Outline::StampSilhouette(gx::Device9 dev, const ev::M2BatchDrawArgs& a, int idx)
    {
        if (!EnsureStateBlock(dev)) return;
        stateCapturedThisFrame_ = true;
        const bool stamped = ImmediateSilhouette::Stamp(static_cast<IDirect3DDevice9*>(dev.raw()),
            static_cast<IDirect3DSurface9*>(mask_.surface), static_cast<IDirect3DStateBlock9*>(stateBlock_), a,
            static_cast<IDirect3DPixelShader9*>(colorPS_), static_cast<IDirect3DPixelShader9*>(cutoutPS_),
            targets_[idx].color, targets_[idx].isPlayer, maskCleared_);
        if (stamped && !g_loggedOutlineStamp)
        {
            g_loggedOutlineStamp = true;
            WLOG_INFO("unit-outline: first immediate silhouette batch stamped (legacy visibility)");
        }
    }

    void Outline::EdgePass(gx::Device9 dev)
    {
        if (!EnsureStateBlock(dev)) return;
        ScopedDeviceState state(stateBlock_);
        if (!state) return;
        stateCapturedThisFrame_ = true;

        dev.SetRenderState(gx::rs::kZEnable, 0);
        dev.SetRenderState(gx::rs::kCullMode, gx::cull::kNone);
        dev.SetRenderState(gx::rs::kAlphaBlend, 1);
        dev.SetRenderState(gx::rs::kSrcBlend, gx::blend::kSrcAlpha);
        dev.SetRenderState(gx::rs::kDestBlend, gx::blend::kInvSrcAlpha);
        dev.SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        dev.SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        dev.SetRenderState(D3DRS_STENCILENABLE, FALSE);
        dev.SetRenderState(D3DRS_COLORWRITEENABLE,
                           D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN |
                           D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);
        dev.SetVertexShader(nullptr);
        gx::ResolveTarget(dev, mask_); // NATIVE: a multisampled mask is resolved to its sampled texture first
        dev.SetTexture(0, mask_.texture);
        dev.SetPixelShader(edgePS_);
        auto* d = static_cast<IDirect3DDevice9*>(dev.raw());
        d->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        d->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        d->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        d->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        d->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);

        float thickness = readability_.enabled ? readability_.outlineThickness : thickness_;
        float opacity = readability_.enabled ? readability_.outlineOpacity : 1.0f;
        bool accented = false;
        if (readability_.enabled)
        {
            for (int i = 0; i < count_; ++i)
                if (targets_[i].currentTarget && targets_[i].drawOutline &&
                    ((readability_.castContrast && readability_.castState != 0) ||
                     (readability_.bossContrast && !targets_[i].isPlayer &&
                      targets_[i].creatureRank == 3)))
                {
                    accented = true;
                    break;
                }
        }
        if (accented && readability_.pulseEnabled && readability_.pulseAmount > 0.0f)
        {
            const float seconds = float(GetTickCount() % 600000u) * 0.001f;
            const float wave = 0.5f + 0.5f * std::sin(seconds * readability_.pulseSpeed * 6.2831853f);
            const float pulse = wave * readability_.pulseAmount;
            thickness *= 1.0f + pulse;
            opacity = std::min(1.0f, opacity * (1.0f + pulse * 0.45f));
        }

        const float c0[4] = { 1.0f / mask_.width, 1.0f / mask_.height, thickness, 0.0f };
        const float c1[4] = { opacity, 0.0f, 0.0f, 0.0f };
        dev.SetPixelShaderConstantF(0, c0, 1);
        dev.SetPixelShaderConstantF(1, c1, 1);
        gx::DrawFullscreenQuad(dev);
        if (!g_loggedOutlineComposite)
        {
            g_loggedOutlineComposite = true;
            WLOG_INFO("unit-outline: first edge composite issued");
        }

        dev.SetPixelShader(nullptr);
        // Drop the private mask before applying the captured state. Restore then puts the caller's
        // original stage-0 texture back instead of leaking our render-target texture or clearing theirs.
        dev.SetTexture(0, nullptr);
        state.Restore();
    }

    namespace
    {
        struct ChevronVertex { float x, y, z, u, v; };
        struct OverlayChevronVertex { float x, y, z; D3DCOLOR color; };
        struct MarkerStyle
        {
            float tipDistance;
            float spacing;
            float scale;
            float width;
            float length;
            float opacity;
            int shape;
            int count;
            bool boss;
            bool enabled;
        };

        struct ShapeProfile
        {
            float widthFactor;
            float lengthFactor;
            float notchRatio;
            float outerTipY, outerBaseX, outerBaseY, outerNotchY;
            float bodyTipY, bodyBaseX, bodyBaseY, bodyNotchY;
            float ridgeTipY, ridgeBaseX, ridgeBaseY, ridgeNotchY;
            bool arrowhead;
        };

        constexpr int kFacingMarkerGround  = 1;
        constexpr int kFacingMarkerOverlay = 2;

        const ShapeProfile& ProfileForShape(int shape)
        {
            static constexpr ShapeProfile profiles[gpu::control::ShapeCount] = {
                // Current polished V, preserved exactly as the default.
                { 1.00f, 1.00f, 0.615f, .96f,.03f,.04f,.43f, .91f,.11f,.10f,.45f, .82f,.24f,.17f,.45f, false },
                // Shorter, substantially wider arms for frontal-cone readability.
                { 1.25f, 0.82f, 0.520f, .96f,.01f,.08f,.36f, .91f,.07f,.14f,.39f, .82f,.18f,.21f,.41f, false },
                // Long, narrow V with a deeper notch.
                { 0.82f, 1.20f, 0.720f, .98f,.14f,.02f,.54f, .93f,.21f,.08f,.55f, .84f,.31f,.15f,.54f, false },
                // Filled triangular arrowhead.
                { 1.05f, 0.95f, 0.000f, .96f,.02f,.06f,.00f, .91f,.10f,.13f,.00f, .82f,.24f,.22f,.00f, true  },
            };
            return profiles[std::clamp(shape, 0, int(gpu::control::ShapeCount) - 1)];
        }

        // Horizontal authored footprint multiplied by the live instance transform's XY scale. The old
        // bounds-only calculation missed server/display scaling, which is why very large bosses could still
        // receive a nearly ordinary marker. The user-configured cap keeps extreme displays bounded.
        float ModelFootprintFactor(void* model, float maxFactor)
        {
            constexpr float kReferenceRadius = 1.00f;
            // NATIVE: the Windows code read the M2 header and instance matrix by raw offset; the native
            // accessor returns the same three inputs (authored horizontal extents and live XY scale).
            float extentX = 0.0f, extentY = 0.0f, instanceScale = 1.0f;
            if (!model || !m2::FootprintInputs(model, extentX, extentY, instanceScale)) return 1.0f;

            float radius = std::sqrt(extentX * extentX + extentY * extentY);
            if (std::isfinite(instanceScale) && instanceScale > 0.01f)
                radius *= instanceScale;
            if (!std::isfinite(radius)) return 1.0f;

            return std::min(maxFactor, std::max(1.0f, radius / kReferenceRadius));
        }

        MarkerStyle MarkerStyleForUnit(void* unit, void* model, const gpu::control::Settings& settings)
        {
            constexpr int kCreatureRankElite = 1;
            constexpr int kCreatureRankRareElite = 2;
            constexpr int kCreatureRankWorldBoss = 3;
            constexpr int kCreatureRankRare = 4;
            const int rank = unit::CreatureRank(unit);
            const bool boss = rank == kCreatureRankWorldBoss;
            const bool elite = rank == kCreatureRankElite || rank == kCreatureRankRareElite ||
                               rank == kCreatureRankRare;
            const float maxFactor = boss ? settings.bossMaxModelFactor :
                                    elite ? settings.eliteMaxModelFactor : settings.normalMaxModelFactor;
            const float distanceWeight = boss ? settings.bossDistanceModelWeight :
                                         elite ? settings.eliteDistanceModelWeight : settings.normalDistanceModelWeight;
            const float sizeWeight = boss ? settings.bossSizeModelWeight :
                                     elite ? settings.eliteSizeModelWeight : settings.normalSizeModelWeight;
            const float footprint = ModelFootprintFactor(model, maxFactor);
            const float sizeScale = 1.0f + (footprint - 1.0f) * sizeWeight;
            const float distanceScale = 1.0f + (footprint - 1.0f) * distanceWeight;

            if (boss)
                return { settings.bossTipDistance * distanceScale, settings.bossSpacing,
                         settings.bossScale * sizeScale, settings.bossWidth, settings.bossLength,
                         settings.bossOpacity, settings.bossShape, settings.bossCount, true,
                         settings.bossEnabled != 0 };
            if (elite)
                return { settings.eliteTipDistance * distanceScale, settings.eliteSpacing,
                         settings.eliteScale * sizeScale, settings.eliteWidth, settings.eliteLength,
                         settings.eliteOpacity, settings.eliteShape, settings.eliteCount, false,
                         settings.eliteEnabled != 0 };
            return { settings.normalTipDistance * distanceScale, settings.normalSpacing,
                     settings.normalScale * sizeScale, settings.normalWidth, settings.normalLength,
                     settings.normalOpacity, settings.normalShape, settings.normalCount, false,
                     settings.normalEnabled != 0 };
        }
    }

    bool Outline::EnsureChevronTexture(gx::Device9 dev, int shape)
    {
        auto* d = static_cast<IDirect3DDevice9*>(dev.raw());
        if (!d) return false;

        shape = std::clamp(shape, 0, int(gpu::control::ShapeCount) - 1);
        if (chevronDevice_ != d)
        {
            ReleaseChevronTextures();
            chevronDevice_ = d;
        }
        if (chevronTextures_[shape]) return true;
        const ShapeProfile& shapeProfile = ProfileForShape(shape);

        constexpr unsigned kWidth = 128;
        constexpr unsigned kHeight = 64;
        constexpr unsigned kSamples = 4;

        IDirect3DTexture9* texture = nullptr;
        if (FAILED(d->CreateTexture(kWidth, kHeight, 1, 0, D3DFMT_A8R8G8B8,
                                    D3DPOOL_MANAGED, &texture, nullptr)) || !texture)
            return false;

        D3DLOCKED_RECT locked{};
        if (FAILED(texture->LockRect(0, &locked, nullptr, 0)))
        {
            texture->Release();
            return false;
        }

        struct Point { float x, y; };
        const auto edge = [](Point p, Point a, Point b)
        {
            return (p.x - b.x) * (a.y - b.y) - (a.x - b.x) * (p.y - b.y);
        };
        const auto insideTriangle = [&edge](Point p, Point a, Point b, Point c)
        {
            const bool n0 = edge(p, a, b) < 0.0f;
            const bool n1 = edge(p, b, c) < 0.0f;
            const bool n2 = edge(p, c, a) < 0.0f;
            return n0 == n1 && n1 == n2;
        };
        const auto insideV = [&insideTriangle](Point p, float tipY, float baseX, float baseY, float notchY)
        {
            const Point tip{ 0.50f, tipY };
            const Point left{ baseX, baseY };
            const Point notch{ 0.50f, notchY };
            const Point right{ 1.0f - baseX, baseY };
            return insideTriangle(p, tip, left, notch) || insideTriangle(p, tip, notch, right);
        };
        const auto insideShape = [&insideTriangle, &insideV, &shapeProfile]
            (Point p, float tipY, float baseX, float baseY, float notchY)
        {
            if (!shapeProfile.arrowhead)
                return insideV(p, tipY, baseX, baseY, notchY);
            return insideTriangle(p, Point{ 0.50f, tipY }, Point{ baseX, baseY },
                                  Point{ 1.0f - baseX, baseY });
        };

        for (unsigned y = 0; y < kHeight; ++y)
        {
            auto* row = reinterpret_cast<D3DCOLOR*>(static_cast<unsigned char*>(locked.pBits) + y * locked.Pitch);
            for (unsigned x = 0; x < kWidth; ++x)
            {
                float covered = 0.0f;
                float luminance = 0.0f;

                for (unsigned sy = 0; sy < kSamples; ++sy)
                {
                    for (unsigned sx = 0; sx < kSamples; ++sx)
                    {
                        const Point p{
                            (float(x) + (float(sx) + 0.5f) / float(kSamples)) / float(kWidth),
                            (float(y) + (float(sy) + 0.5f) / float(kSamples)) / float(kHeight)
                        };

                        if (!insideShape(p, shapeProfile.outerTipY, shapeProfile.outerBaseX,
                                         shapeProfile.outerBaseY, shapeProfile.outerNotchY))
                            continue;

                        float shade = 0.08f; // dark outer rim
                        if (insideShape(p, shapeProfile.bodyTipY, shapeProfile.bodyBaseX,
                                        shapeProfile.bodyBaseY, shapeProfile.bodyNotchY))
                            shade = 0.74f; // reaction-colored body
                        if (insideShape(p, shapeProfile.ridgeTipY, shapeProfile.ridgeBaseX,
                                        shapeProfile.ridgeBaseY, shapeProfile.ridgeNotchY))
                            shade = 1.00f; // pale inset ridge

                        covered += 1.0f;
                        luminance += shade;
                    }
                }

                if (covered == 0.0f)
                {
                    row[x] = 0;
                    continue;
                }

                const float coverage = covered / float(kSamples * kSamples);
                const unsigned alpha = unsigned(std::lround(coverage * 255.0f));
                const unsigned value = unsigned(std::lround((luminance / covered) * 255.0f));
                row[x] = D3DCOLOR_ARGB(alpha, value, value, value);
            }
        }

        texture->UnlockRect(0);
        chevronTextures_[shape] = texture;
        return true;
    }

    void Outline::PrepareMarkerGroundCache(int targetIndex)
    {
        if (settings_.facingMode != kFacingMarkerGround ||
            targetIndex < 0 || targetIndex >= count_)
            return;

        const Target& target = targets_[targetIndex];
        if (target.isPlayer || !target.drawMarker || !target.unit || !target.model)
            return;

        const MarkerStyle marker = MarkerStyleForUnit(target.unit, target.model, settings_);
        const bool drawTail = settings_.tailEnabled && unit::CreatureType(target.unit) == 2;
        if (!marker.enabled && !drawTail)
            return;
        const ShapeProfile& shape = ProfileForShape(marker.shape);

        float pos[3]{};
        world::UnitPosition(target.unit, pos);
        float facing = world::UnitFacing(target.unit);
        m2::VisualFacing(target.model, facing);
        const float forward[2] = { std::cos(facing), std::sin(facing) };

        MarkerGroundCache& cache = markerGround_[targetIndex];
        if (cache.guid != target.guid)
        {
            cache = MarkerGroundCache{};
            cache.guid = target.guid;
        }

        int quadIndex = 0;
        const auto prepareChevron = [&](float direction, float tipDistance, float markerScale,
                                        float width, float length)
        {
            if (quadIndex >= kMaxGroundMarkerQuads)
                return;

            const float lineForward[2] = { forward[0] * direction, forward[1] * direction };
            const float right[2] = { -lineForward[1], lineForward[0] };
            const float baseDepth = 0.52f * markerScale * length * shape.lengthFactor;
            const float baseDistance = tipDistance - baseDepth;
            const float halfWidth = 0.70f * markerScale * width * shape.widthFactor;
            const float fallbackZ = pos[2] + 0.055f;
            const float xy[4][2] = {
                { pos[0] + lineForward[0] * baseDistance + right[0] * halfWidth,
                  pos[1] + lineForward[1] * baseDistance + right[1] * halfWidth },
                { pos[0] + lineForward[0] * tipDistance + right[0] * halfWidth,
                  pos[1] + lineForward[1] * tipDistance + right[1] * halfWidth },
                { pos[0] + lineForward[0] * baseDistance - right[0] * halfWidth,
                  pos[1] + lineForward[1] * baseDistance - right[1] * halfWidth },
                { pos[0] + lineForward[0] * tipDistance - right[0] * halfWidth,
                  pos[1] + lineForward[1] * tipDistance - right[1] * halfWidth },
            };

            MarkerGroundQuad& quad = cache.quads[quadIndex++];
            const uint32_t now = GetTickCount();
            const uint32_t elapsed = now - quad.sampledAt;
            bool moved = !quad.valid;
            for (int i = 0; i < 4 && !moved; ++i)
            {
                const float dx = xy[i][0] - quad.xy[i][0];
                const float dy = xy[i][1] - quad.xy[i][1];
                moved = dx * dx + dy * dy > 0.0225f;
            }
            // Collision queries are comparatively expensive.  Between refreshes the draw path projects
            // the marker's current vertices onto this sampled ground plane, so a moving unit stays smooth
            // instead of visually stepping from one batch of collision samples to the next.
            if (!quad.valid || (moved && elapsed >= 250u) || elapsed >= 1500u)
            {
                for (int i = 0; i < 4; ++i)
                {
                    quad.xy[i][0] = xy[i][0];
                    quad.xy[i][1] = xy[i][1];
                    quad.z[i] = fallbackZ;
                    float sampledZ = fallbackZ;
                    if (SampleGroundZ(xy[i][0], xy[i][1], pos[2], sampledZ))
                        quad.z[i] = sampledZ + 0.010f;
                }
                quad.sampledAt = now;
                quad.valid = true;
            }
        };

        if (marker.enabled)
        {
            for (int index = marker.count - 1; index >= 1; --index)
            {
                const float accentScale = marker.scale * (marker.boss ? 0.55f : 0.72f);
                const float accentTip = std::max(0.15f, marker.tipDistance - marker.spacing * index);
                prepareChevron(1.0f, accentTip, accentScale, marker.width, marker.length);
            }
        }
        if (drawTail)
        {
            prepareChevron(-1.0f, marker.tipDistance * settings_.tailDistanceMultiplier,
                           marker.scale * settings_.tailScaleMultiplier,
                           marker.width * settings_.tailWidth,
                           marker.length * settings_.tailLength);
        }
        if (marker.enabled)
            prepareChevron(1.0f, marker.tipDistance, marker.scale, marker.width, marker.length);

        for (int i = quadIndex; i < kMaxGroundMarkerQuads; ++i)
            cache.quads[i].valid = false;
    }

    void Outline::PrepareDangerGroundCache(int targetIndex)
    {
        if (!readability_.enabled || !readability_.dangerEnabled ||
            readability_.dangerVisibility != combat::DangerGround ||
            targetIndex < 0 || targetIndex >= count_)
        {
            dangerGround_ = {};
            return;
        }

        const Target& target = targets_[targetIndex];
        if (!target.currentTarget || target.isPlayer || !target.unit || !target.model ||
            (!readability_.ringEnabled && !readability_.coneEnabled))
        {
            dangerGround_ = {};
            return;
        }

        void* player = world::ResolveObject(world::ActivePlayerGuid(), world::kTypeMaskPlayer);
        if (!player || unit::Reaction(target.unit, player) >= 2)
        {
            dangerGround_ = {};
            return;
        }

        const int rank = target.creatureRank;
        const bool boss = rank == 3;
        const bool elite = rank == 1 || rank == 2 || rank == 4;
        if ((readability_.dangerCategory == combat::DangerBossOnly && !boss) ||
            (readability_.dangerCategory == combat::DangerEliteAndBoss && !elite && !boss))
        {
            dangerGround_ = {};
            return;
        }

        float targetPos[3]{};
        float playerPos[3]{};
        world::UnitPosition(target.unit, targetPos);
        world::UnitPosition(player, playerPos);
        const float dx = targetPos[0] - playerPos[0];
        const float dy = targetPos[1] - playerPos[1];
        const float dz = targetPos[2] - playerPos[2];
        if (dx * dx + dy * dy + dz * dz >
            readability_.maxDistance * readability_.maxDistance)
        {
            dangerGround_ = {};
            return;
        }

        constexpr float kTau = 6.28318530718f;
        constexpr float kDegreesToRadians = 0.01745329252f;
        constexpr int kGroundAngularStride = 4;
        // A guide sample must remain connected to the prior radial band by a plausible
        // walkable slope. The fixed allowance keeps ordinary stair steps, while the cap
        // prevents a long, coarsely sampled cone from treating the top of a wall or cliff
        // as a continuous ground surface.
        constexpr float kMaxGuideSlope = 0.75f;
        constexpr float kGuideStepAllowance = 0.35f;
        constexpr float kMaxGuideHeightStep = 2.0f;
        const auto maxContinuousHeightDelta = [&](float horizontalDistance)
        {
            return std::min(kMaxGuideHeightStep,
                kGuideStepAllowance + std::max(0.0f, horizontalDistance) * kMaxGuideSlope);
        };
        const float scale = boss ? readability_.bossScale : 1.0f;
        const float ringRadius = readability_.ringRadius * scale;
        const float ringHalfWidth = readability_.ringWidth * 0.5f;
        const float ringInner = std::max(0.05f, ringRadius - ringHalfWidth);
        const float ringOuter = ringRadius + ringHalfWidth;
        const float coneHalfAngle =
            readability_.coneAngle * 0.5f * kDegreesToRadians;
        const float coneRange = readability_.coneRange * scale;
        float facing = world::UnitFacing(target.unit);
        if (readability_.coneEnabled)
            m2::VisualFacing(target.model, facing);

        if (!readability_.ringEnabled)
            dangerGround_.ringValid = false;
        if (!readability_.coneEnabled)
            dangerGround_.coneValid = false;

        const uint32_t now = GetTickCount();
        const uint32_t elapsed = now - dangerGround_.sampledAt;
        const float moveX = targetPos[0] - dangerGround_.center[0];
        const float moveY = targetPos[1] - dangerGround_.center[1];
        const float moveZ = targetPos[2] - dangerGround_.center[2];
        const bool moved = moveX * moveX + moveY * moveY + moveZ * moveZ > 0.0225f;
        const float facingDelta = std::abs(std::remainder(facing - dangerGround_.facing, kTau));
        const bool turned = readability_.coneEnabled && facingDelta > 0.05236f;
        const bool targetChanged = dangerGround_.guid != target.guid;
        const bool ringChanged = readability_.ringEnabled &&
            (!dangerGround_.ringValid ||
             std::abs(dangerGround_.ringRadius - ringRadius) > 0.001f ||
             std::abs(dangerGround_.ringWidth - readability_.ringWidth) > 0.001f);
        const bool coneChanged = readability_.coneEnabled &&
            (!dangerGround_.coneValid ||
             std::abs(dangerGround_.coneRange - coneRange) > 0.001f ||
             std::abs(dangerGround_.coneAngle - readability_.coneAngle) > 0.001f);
        const bool refresh = targetChanged || ringChanged || coneChanged ||
            ((moved || turned) && elapsed >= 150u) || elapsed >= 1250u;
        if (!refresh)
            return;

        DangerGroundCache sampled{};
        sampled.guid = target.guid;
        sampled.sampledAt = now;
        sampled.center[0] = targetPos[0];
        sampled.center[1] = targetPos[1];
        sampled.center[2] = targetPos[2];
        sampled.facing = facing;
        sampled.ringRadius = ringRadius;
        sampled.ringWidth = readability_.ringWidth;
        sampled.coneRange = coneRange;
        sampled.coneAngle = readability_.coneAngle;
        const float fallbackZ = targetPos[2] + 0.045f;

        if (readability_.ringEnabled)
        {
            const float radii[2] = { ringInner, ringOuter };
            for (int edge = 0; edge < 2; ++edge)
            {
                for (int i = 0; i < kDangerRingSegments; i += kGroundAngularStride)
                {
                    const float angle = kTau * float(i) / float(kDangerRingSegments);
                    const float x = targetPos[0] + std::cos(angle) * radii[edge];
                    const float y = targetPos[1] + std::sin(angle) * radii[edge];
                    if (!SampleGroundZ(x, y, targetPos[2], sampled.ringZ[edge][i]))
                        sampled.ringZ[edge][i] = fallbackZ;
                }
                sampled.ringZ[edge][kDangerRingSegments] = sampled.ringZ[edge][0];
                for (int start = 0; start < kDangerRingSegments;
                     start += kGroundAngularStride)
                {
                    const float z0 = sampled.ringZ[edge][start];
                    const float z1 = sampled.ringZ[edge][start + kGroundAngularStride];
                    for (int offset = 1; offset < kGroundAngularStride; ++offset)
                    {
                        const float t = float(offset) / float(kGroundAngularStride);
                        sampled.ringZ[edge][start + offset] = z0 + (z1 - z0) * t;
                    }
                }
            }
            sampled.ringValid = true;
        }

        if (readability_.coneEnabled)
        {
            const float invalidGroundZ = std::numeric_limits<float>::quiet_NaN();
            const float radialSpacing = coneRange / float(kDangerConeBands);
            const float maxRadialHeightDelta = maxContinuousHeightDelta(radialSpacing);
            float centerZ = fallbackZ;
            SampleGroundZ(targetPos[0], targetPos[1], targetPos[2], centerZ);
            for (int i = 0; i <= kDangerConeSegments; ++i)
                sampled.coneZ[0][i] = centerZ;
            for (int band = 1; band <= kDangerConeBands; ++band)
            {
                const float radius = coneRange * float(band) / float(kDangerConeBands);
                for (int i = 0; i <= kDangerConeSegments; i += kGroundAngularStride)
                {
                    const float angle = facing - coneHalfAngle +
                        coneHalfAngle * 2.0f * float(i) / float(kDangerConeSegments);
                    const float x = targetPos[0] + std::cos(angle) * radius;
                    const float y = targetPos[1] + std::sin(angle) * radius;
                    const float innerZ = sampled.coneZ[band - 1][i];
                    if (!std::isfinite(innerZ))
                    {
                        sampled.coneZ[band][i] = invalidGroundZ;
                        continue;
                    }

                    float candidateZ = innerZ;
                    // A miss (including an M2/doodad hit, which SampleGroundZ deliberately
                    // declines) retains the prior plane and lets scene depth hide it. A real
                    // terrain/WMO hit that jumps too far is a hard boundary: descendants stay
                    // invalid so no triangle can bridge from the floor onto the obstruction.
                    if (!SampleGroundZ(x, y, targetPos[2], candidateZ))
                        sampled.coneZ[band][i] = innerZ;
                    else if (std::abs(candidateZ - innerZ) <= maxRadialHeightDelta)
                        sampled.coneZ[band][i] = candidateZ;
                    else
                        sampled.coneZ[band][i] = invalidGroundZ;
                }

                const float angularStep = coneHalfAngle * 2.0f *
                    float(kGroundAngularStride) / float(kDangerConeSegments);
                const float angularChord = 2.0f * radius * std::sin(angularStep * 0.5f);
                const float maxAngularHeightDelta = maxContinuousHeightDelta(angularChord);
                for (int start = 0; start < kDangerConeSegments;
                     start += kGroundAngularStride)
                {
                    const float z0 = sampled.coneZ[band][start];
                    const float z1 = sampled.coneZ[band][start + kGroundAngularStride];
                    const bool continuous = std::isfinite(z0) && std::isfinite(z1) &&
                        std::abs(z1 - z0) <= maxAngularHeightDelta;
                    for (int offset = 1; offset < kGroundAngularStride; ++offset)
                    {
                        if (continuous)
                        {
                            const float t = float(offset) / float(kGroundAngularStride);
                            sampled.coneZ[band][start + offset] = z0 + (z1 - z0) * t;
                        }
                        else
                        {
                            sampled.coneZ[band][start + offset] = invalidGroundZ;
                        }
                    }
                }
            }
            sampled.coneValid = true;
        }
        dangerGround_ = sampled;
    }

    void Outline::PrepareGroundCaches()
    {
        if (settings_.facingMode == kFacingMarkerGround)
        {
            for (int i = 0; i < count_; ++i)
                PrepareMarkerGroundCache(i);
        }
        else
        {
            for (MarkerGroundCache& cache : markerGround_)
                cache = {};
        }

        bool preparedDanger = false;
        if (readability_.dangerVisibility == combat::DangerGround)
        {
            for (int i = 0; i < count_; ++i)
            {
                if (!targets_[i].currentTarget)
                    continue;
                PrepareDangerGroundCache(i);
                preparedDanger = true;
                break;
            }
        }
        if (!preparedDanger)
            dangerGround_ = {};
    }

    // Draw the polished texture immediately after the target's first opaque batch. Corner heights are
    // sampled at EndScene, outside DrawIndexedPrimitive, so the decal follows terrain without re-entering
    // world collision from the renderer's hot batch callback.
    bool Outline::DrawFacingChevron(gx::Device9 dev, int targetIndex)
    {
        auto* d = static_cast<IDirect3DDevice9*>(dev.raw());
        if (!d || settings_.facingMode != kFacingMarkerGround || !chevronPS_ ||
            targetIndex < 0 || targetIndex >= count_)
            return false;

        const Target& target = targets_[targetIndex];
        if (target.isPlayer || !target.drawMarker || !target.unit || !target.model)
            return false;

        const MarkerStyle marker = MarkerStyleForUnit(target.unit, target.model, settings_);
        const bool drawTail = settings_.tailEnabled && unit::CreatureType(target.unit) == 2;
        if ((!marker.enabled && !drawTail) || !EnsureChevronTexture(dev, marker.shape))
            return false;
        const ShapeProfile& shape = ProfileForShape(marker.shape);

        IDirect3DSurface9* depth = nullptr;
        if (FAILED(d->GetDepthStencilSurface(&depth)) || !depth)
            return false;
        depth->Release();

        const float* view = cam::View();
        const float* proj = cam::Projection();
        if (!view || !proj) return false;

        if (!EnsureStateBlock(dev)) return false;
        ScopedDeviceState state(stateBlock_);
        if (!state) return false;
        stateCapturedThisFrame_ = true;
        d->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
        d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        d->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
        d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        d->SetRenderState(D3DRS_LIGHTING, FALSE);
        d->SetRenderState(D3DRS_FOGENABLE, FALSE);

        // Preserve the previously proven chevron depth behavior. The target's first opaque batch is already
        // in depth when this draws, and the pre-sampled terrain lift prevents floor z-fighting.
        const float depthBias = -0.00010f;
        DWORD depthBiasBits = 0;
        std::memcpy(&depthBiasBits, &depthBias, sizeof(depthBiasBits));
        d->SetRenderState(D3DRS_DEPTHBIAS, depthBiasBits);
        d->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, 0);

        d->SetVertexShader(nullptr);
        d->SetPixelShader(static_cast<IDirect3DPixelShader9*>(chevronPS_));
        d->SetTexture(0, static_cast<IDirect3DTexture9*>(chevronTextures_[marker.shape]));
        d->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        d->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        d->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        d->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        d->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);

        const D3DMATRIX world = { 1, 0, 0, 0,
                                   0, 1, 0, 0,
                                   0, 0, 1, 0,
                                   0, 0, 0, 1 };
        D3DMATRIX viewMatrix{}, projectionMatrix{};
        std::memcpy(&viewMatrix, view, sizeof(viewMatrix));
        std::memcpy(&projectionMatrix, proj, sizeof(projectionMatrix));
        d->SetTransform(D3DTS_WORLD, &world);
        d->SetTransform(D3DTS_VIEW, &viewMatrix);
        d->SetTransform(D3DTS_PROJECTION, &projectionMatrix);
        d->SetFVF(D3DFVF_XYZ | D3DFVF_TEX1);

        float pos[3];
        world::UnitPosition(target.unit, pos);
        float facing = world::UnitFacing(target.unit);
        m2::VisualFacing(target.model, facing);
        const float forward[2] = { std::cos(facing), std::sin(facing) };
        bool drew = false;
        int groundQuadIndex = 0;
        MarkerGroundCache& groundCache = markerGround_[targetIndex];
        if (groundCache.guid != target.guid)
            return false;

        const auto drawChevron = [&](float direction, float tipDistance, float markerScale,
                                     float width, float length, const float color[4])
        {
            const float lineForward[2] = { forward[0] * direction, forward[1] * direction };
            const float right[2] = { -lineForward[1], lineForward[0] };
            const float baseDepth = 0.52f * markerScale * length * shape.lengthFactor;
            const float baseDistance = tipDistance - baseDepth;
            const float halfWidth = 0.70f * markerScale * width * shape.widthFactor;
            const float z = pos[2] + 0.055f;

            const auto point = [&](float along, float across, float u, float v)
            {
                return ChevronVertex{
                    pos[0] + lineForward[0] * along + right[0] * across,
                    pos[1] + lineForward[1] * along + right[1] * across,
                    z, u, v
                };
            };

            ChevronVertex vertices[4] = {
                point(baseDistance,  halfWidth, 0.0f, 0.0f),
                point(tipDistance,   halfWidth, 0.0f, 1.0f),
                point(baseDistance, -halfWidth, 1.0f, 0.0f),
                point(tipDistance,  -halfWidth, 1.0f, 1.0f),
            };

            if (groundQuadIndex >= kMaxGroundMarkerQuads)
                return;
            const MarkerGroundQuad& cached = groundCache.quads[groundQuadIndex++];
            if (!cached.valid)
                return;

            // The four cached samples form a local terrain patch.  Re-evaluate its plane at the current
            // moving vertices until the next bounded collision refresh.  This retains terrain following
            // without the 150 ms height snaps that made ground chevrons feel laggy while moving.
            const float dx1 = cached.xy[1][0] - cached.xy[0][0];
            const float dy1 = cached.xy[1][1] - cached.xy[0][1];
            const float dx2 = cached.xy[2][0] - cached.xy[0][0];
            const float dy2 = cached.xy[2][1] - cached.xy[0][1];
            const float determinant = dx1 * dy2 - dx2 * dy1;
            const float dz1 = cached.z[1] - cached.z[0];
            const float dz2 = cached.z[2] - cached.z[0];
            const bool havePlane = std::abs(determinant) > 0.0001f;
            const float slopeX = havePlane ? (dz1 * dy2 - dz2 * dy1) / determinant : 0.0f;
            const float slopeY = havePlane ? (dx1 * dz2 - dx2 * dz1) / determinant : 0.0f;
            for (int i = 0; i < 4; ++i)
            {
                const float projected = cached.z[0] +
                    slopeX * (vertices[i].x - cached.xy[0][0]) +
                    slopeY * (vertices[i].y - cached.xy[0][1]);
                // Do not extrapolate a stale terrain patch across a sharp discontinuity.  The next
                // normal refresh will recover the exact collision height there.
                vertices[i].z = havePlane && std::isfinite(projected) &&
                    std::abs(projected - cached.z[i]) <= 4.0f ? projected : cached.z[i];
            }

            d->SetPixelShaderConstantF(0, color, 1);
            if (SUCCEEDED(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(ChevronVertex))))
                drew = true;
        };

        if (marker.enabled)
        {
            for (int index = marker.count - 1; index >= 1; --index)
            {
                const float accentScale = marker.scale * (marker.boss ? 0.55f : 0.72f);
                const float accentTip = std::max(0.15f, marker.tipDistance - marker.spacing * index);
                const float accent[4] = {
                    marker.boss ? 1.0f : target.color[0] + (1.0f - target.color[0]) * 0.25f,
                    marker.boss ? 0.58f : target.color[1] + (1.0f - target.color[1]) * 0.25f,
                    marker.boss ? 0.08f : target.color[2] + (1.0f - target.color[2]) * 0.25f,
                    marker.boss ? std::min(1.0f, marker.opacity + 0.03f) : marker.opacity * 0.85f
                };
                drawChevron(1.0f, accentTip, accentScale, marker.width, marker.length, accent);
            }
        }
        if (drawTail)
        {
            const float tailWarning[4] = { 1.0f, 0.12f, 0.02f, settings_.tailOpacity };
            drawChevron(-1.0f, marker.tipDistance * settings_.tailDistanceMultiplier,
                        marker.scale * settings_.tailScaleMultiplier,
                        marker.width * settings_.tailWidth, marker.length * settings_.tailLength,
                        tailWarning);
        }
        if (marker.enabled)
        {
            const float reactionColor[4] = { target.color[0], target.color[1], target.color[2], marker.opacity };
            drawChevron(1.0f, marker.tipDistance, marker.scale, marker.width, marker.length, reactionColor);
        }

        return drew;
    }

    // Generic target-centric danger guide. It intentionally does not claim to be a spell-radius detector:
    // the 3.3.5 client surface available to WarcraftXL has no authoritative missile/DynamicObject metadata.
    bool Outline::DrawDangerGuide(gx::Device9 dev, int targetIndex)
    {
        auto* d = static_cast<IDirect3DDevice9*>(dev.raw());
        if (!d || !readability_.enabled || !readability_.dangerEnabled ||
            targetIndex < 0 || targetIndex >= count_)
            return false;

        const Target& target = targets_[targetIndex];
        if (!target.currentTarget || target.isPlayer || !target.unit || !target.model ||
            (!readability_.ringEnabled && !readability_.coneEnabled))
            return false;

        void* player = world::ResolveObject(world::ActivePlayerGuid(), world::kTypeMaskPlayer);
        if (!player || unit::Reaction(target.unit, player) >= 2)
            return false;

        const int rank = target.creatureRank;
        const bool boss = rank == 3;
        const bool elite = rank == 1 || rank == 2 || rank == 4;
        if (readability_.dangerCategory == combat::DangerBossOnly && !boss)
            return false;
        if (readability_.dangerCategory == combat::DangerEliteAndBoss && !elite && !boss)
            return false;

        float targetPos[3]{};
        float playerPos[3]{};
        world::UnitPosition(target.unit, targetPos);
        world::UnitPosition(player, playerPos);
        const float dx = targetPos[0] - playerPos[0];
        const float dy = targetPos[1] - playerPos[1];
        const float dz = targetPos[2] - playerPos[2];
        if (dx * dx + dy * dy + dz * dz > readability_.maxDistance * readability_.maxDistance)
            return false;

        const float* view = cam::View();
        const float* proj = cam::Projection();
        if (!view || !proj)
            return false;

        const bool overlay = readability_.dangerVisibility == combat::DangerOverlay;
        constexpr float kTau = 6.28318530718f;
        constexpr float kDegreesToRadians = 0.01745329252f;
        const float scale = boss ? readability_.bossScale : 1.0f;
        const float ringRadius = readability_.ringRadius * scale;
        const float ringHalfWidth = readability_.ringWidth * 0.5f;
        const float ringInner = std::max(0.05f, ringRadius - ringHalfWidth);
        const float ringOuter = ringRadius + ringHalfWidth;
        const float coneHalfAngle = readability_.coneAngle * 0.5f * kDegreesToRadians;
        const float coneRange = readability_.coneRange * scale;
        float facing = world::UnitFacing(target.unit);
        if (readability_.coneEnabled)
            m2::VisualFacing(target.model, facing);

        // Ground collision is deliberately sampled at EndScene, never from the
        // DrawIndexedPrimitive callback. Do not draw stale geometry for a prior target.
        if (!overlay &&
            (dangerGround_.guid != target.guid ||
             (readability_.ringEnabled && !dangerGround_.ringValid) ||
             (readability_.coneEnabled && !dangerGround_.coneValid)))
            return false;

        if (!EnsureStateBlock(dev))
            return false;

        // Require the world depth surface already bound for the target batch; never
        // rebind or substitute a copied depth target here.
        IDirect3DSurface9* sceneDepth = nullptr;
        if (!overlay && (FAILED(d->GetDepthStencilSurface(&sceneDepth)) || !sceneDepth))
            return false;
        if (sceneDepth) sceneDepth->Release();

        ScopedDeviceState state(stateBlock_);
        if (!state)
            return false;
        stateCapturedThisFrame_ = true;

        d->SetRenderState(D3DRS_ZENABLE, overlay ? FALSE : D3DZB_TRUE);
        d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        d->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
        d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        d->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
        d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
        d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        d->SetRenderState(D3DRS_LIGHTING, FALSE);
        d->SetRenderState(D3DRS_FOGENABLE, FALSE);
        const float depthBias = overlay ? 0.0f : -0.000015f;
        DWORD depthBiasBits = 0;
        std::memcpy(&depthBiasBits, &depthBias, sizeof(depthBiasBits));
        d->SetRenderState(D3DRS_DEPTHBIAS, depthBiasBits);
        d->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, 0);
        d->SetVertexShader(nullptr);
        d->SetPixelShader(nullptr);
        d->SetTexture(0, nullptr);
        d->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        d->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        d->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        d->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
        d->SetTexture(1, nullptr);
        d->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        d->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

        const D3DMATRIX identity = { 1, 0, 0, 0,
                                     0, 1, 0, 0,
                                     0, 0, 1, 0,
                                     0, 0, 0, 1 };
        D3DMATRIX viewMatrix{}, projectionMatrix{};
        std::memcpy(&viewMatrix, view, sizeof(viewMatrix));
        std::memcpy(&projectionMatrix, proj, sizeof(projectionMatrix));
        d->SetTransform(D3DTS_WORLD, &identity);
        d->SetTransform(D3DTS_VIEW, &viewMatrix);
        d->SetTransform(D3DTS_PROJECTION, &projectionMatrix);
        d->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);

        struct DangerVertex { float x, y, z; D3DCOLOR color; };
        float rgb[3]{};
        combat::PaletteColor(readability_.dangerPalette, rgb);
        const auto color = [&](float opacity)
        {
            return D3DCOLOR_ARGB(
                static_cast<unsigned>(std::lround(std::clamp(opacity, 0.0f, 1.0f) * 255.0f)),
                static_cast<unsigned>(std::lround(rgb[0] * 255.0f)),
                static_cast<unsigned>(std::lround(rgb[1] * 255.0f)),
                static_cast<unsigned>(std::lround(rgb[2] * 255.0f)));
        };

        const float z = targetPos[2] + 0.065f;
        bool drew = false;

        if (readability_.ringEnabled)
        {
            const D3DCOLOR tint = color(readability_.ringOpacity);
            // Treat a cone that reaches fully across the ring as a union, not a second translucent layer.
            // The frontal arc is omitted because the cone already owns that area.  Keeping this conditional
            // on the outer radius avoids punching a visible gap when a deliberately short cone only clips
            // the ring's inner edge.
            const bool mergeWithCone = readability_.coneEnabled && coneRange >= ringOuter - 0.001f;
            const auto segmentCoveredByCone = [&](int segment)
            {
                if (!mergeWithCone)
                    return false;
                const float midAngle = kTau * (float(segment) + 0.5f) / float(kDangerRingSegments);
                return std::abs(std::remainder(midAngle - facing, kTau)) <= coneHalfAngle;
            };
            const auto ringVertex = [&](int unwrappedIndex, bool outer)
            {
                const int index = unwrappedIndex % kDangerRingSegments;
                const float angle = kTau * float(unwrappedIndex) / float(kDangerRingSegments);
                const float radius = outer ? ringOuter : ringInner;
                const float vertexZ = !overlay && dangerGround_.ringValid
                    ? dangerGround_.ringZ[outer ? 1 : 0][index] : z;
                return DangerVertex{
                    targetPos[0] + std::cos(angle) * radius,
                    targetPos[1] + std::sin(angle) * radius,
                    vertexZ, tint
                };
            };
            const auto drawRingRun = [&](int firstSegment, int lastSegment)
            {
                if (lastSegment < firstSegment)
                    return;
                std::array<DangerVertex, (kDangerRingSegments + 1) * 2> ring{};
                size_t vertex = 0;
                ring[vertex++] = ringVertex(firstSegment, false);
                ring[vertex++] = ringVertex(firstSegment, true);
                for (int segment = firstSegment; segment <= lastSegment; ++segment)
                {
                    ring[vertex++] = ringVertex(segment + 1, false);
                    ring[vertex++] = ringVertex(segment + 1, true);
                }
                if (SUCCEEDED(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,
                        static_cast<UINT>(vertex - 2), ring.data(), sizeof(DangerVertex))))
                    drew = true;
            };

            int firstCovered = -1;
            for (int segment = 0; segment < kDangerRingSegments; ++segment)
            {
                if (segmentCoveredByCone(segment))
                {
                    firstCovered = segment;
                    break;
                }
            }
            if (firstCovered < 0)
            {
                drawRingRun(0, kDangerRingSegments - 1);
            }
            else
            {
                bool drawingRun = false;
                int runStart = 0;
                // Start immediately after a covered segment so an unbroken rear/side arc can cross 0 degrees.
                for (int offset = 1; offset <= kDangerRingSegments; ++offset)
                {
                    const int segment = firstCovered + offset;
                    if (!segmentCoveredByCone(segment % kDangerRingSegments))
                    {
                        if (!drawingRun)
                        {
                            runStart = segment;
                            drawingRun = true;
                        }
                    }
                    else if (drawingRun)
                    {
                        drawRingRun(runStart, segment - 1);
                        drawingRun = false;
                    }
                }
            }
        }

        if (readability_.coneEnabled)
        {
            const D3DCOLOR tint = color(readability_.coneOpacity);
            if (overlay || !dangerGround_.coneValid)
            {
                std::array<DangerVertex, kDangerConeSegments + 2> cone{};
                size_t vertex = 0;
                cone[vertex++] = { targetPos[0], targetPos[1], z, tint };
                for (int i = 0; i <= kDangerConeSegments; ++i)
                {
                    const float angle = facing - coneHalfAngle +
                        (coneHalfAngle * 2.0f * float(i) / float(kDangerConeSegments));
                    cone[vertex++] = { targetPos[0] + std::cos(angle) * coneRange,
                                       targetPos[1] + std::sin(angle) * coneRange, z, tint };
                }
                if (SUCCEEDED(d->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, kDangerConeSegments,
                        cone.data(), sizeof(DangerVertex))))
                    drew = true;
            }
            else
            {
                // Rejected ground samples are NaN sentinels. Build one bounded triangle list
                // from only fully continuous cells so the guide ends at a wall/cliff rather
                // than stretching a strip between the floor and the rejected surface.
                std::array<DangerVertex,
                    kDangerConeBands * kDangerConeSegments * 6> cone{};
                size_t vertex = 0;
                for (int band = 0; band < kDangerConeBands; ++band)
                {
                    const float innerRadius = coneRange * float(band) / float(kDangerConeBands);
                    const float outerRadius = coneRange * float(band + 1) / float(kDangerConeBands);
                    for (int i = 0; i < kDangerConeSegments; ++i)
                    {
                        const float innerZ0 = dangerGround_.coneZ[band][i];
                        const float innerZ1 = dangerGround_.coneZ[band][i + 1];
                        const float outerZ0 = dangerGround_.coneZ[band + 1][i];
                        const float outerZ1 = dangerGround_.coneZ[band + 1][i + 1];
                        if (!std::isfinite(innerZ0) || !std::isfinite(innerZ1) ||
                            !std::isfinite(outerZ0) || !std::isfinite(outerZ1))
                            continue;

                        const float angle0 = facing - coneHalfAngle +
                            coneHalfAngle * 2.0f * float(i) / float(kDangerConeSegments);
                        const float angle1 = facing - coneHalfAngle +
                            coneHalfAngle * 2.0f * float(i + 1) / float(kDangerConeSegments);
                        const float cs0 = std::cos(angle0), sn0 = std::sin(angle0);
                        const float cs1 = std::cos(angle1), sn1 = std::sin(angle1);
                        const DangerVertex inner0{
                            targetPos[0] + cs0 * innerRadius,
                            targetPos[1] + sn0 * innerRadius, innerZ0, tint };
                        const DangerVertex inner1{
                            targetPos[0] + cs1 * innerRadius,
                            targetPos[1] + sn1 * innerRadius, innerZ1, tint };
                        const DangerVertex outer0{
                            targetPos[0] + cs0 * outerRadius,
                            targetPos[1] + sn0 * outerRadius, outerZ0, tint };
                        const DangerVertex outer1{
                            targetPos[0] + cs1 * outerRadius,
                            targetPos[1] + sn1 * outerRadius, outerZ1, tint };
                        cone[vertex++] = inner0;
                        cone[vertex++] = outer0;
                        cone[vertex++] = inner1;
                        cone[vertex++] = inner1;
                        cone[vertex++] = outer0;
                        cone[vertex++] = outer1;
                    }
                }
                if (vertex != 0 && SUCCEEDED(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,
                        static_cast<UINT>(vertex / 3), cone.data(), sizeof(DangerVertex))))
                    drew = true;
            }
        }

        if (drew && !g_loggedFrontalGuide)
        {
            g_loggedFrontalGuide = true;
            WLOG_INFO("frontal-guide: first cone issued (rank=%d visibility=%d)",
                      target.creatureRank, readability_.dangerVisibility);
        }

        return drew;
    }

    // Exact polished implementation used immediately before the ground-decal experiment. It deliberately
    // disables scene depth so the cue remains visible through the selected unit and other geometry.
    void Outline::DrawFacingChevronsOverlay(gx::Device9 dev)
    {
        auto* d = static_cast<IDirect3DDevice9*>(dev.raw());
        if (!d || settings_.facingMode != kFacingMarkerOverlay || !overlayChevronPS_) return;

        bool hasEligibleTarget = false;
        for (int i = 0; i < count_; ++i)
            if (!targets_[i].isPlayer && targets_[i].drawMarker && targets_[i].unit)
            {
                hasEligibleTarget = true;
                break;
            }
        if (!hasEligibleTarget) return;

        const float* view = cam::View();
        const float* proj = cam::Projection();
        if (!view || !proj) return;

        if (!EnsureStateBlock(dev)) return;
        ScopedDeviceState state(stateBlock_);
        if (!state) return;
        stateCapturedThisFrame_ = true;
        d->SetRenderState(D3DRS_ZENABLE, FALSE);
        d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        d->SetVertexShader(nullptr);
        d->SetPixelShader(static_cast<IDirect3DPixelShader9*>(overlayChevronPS_));
        d->SetTexture(0, nullptr);
        // The client normally leaves stage 0 in textured-modulate mode. With no texture bound that turns
        // colored vertices black, so explicitly select the chevron's diffuse vertex color.
        d->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        d->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        d->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        d->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
        const D3DMATRIX worldMatrix = { 1, 0, 0, 0,
                                        0, 1, 0, 0,
                                        0, 0, 1, 0,
                                        0, 0, 0, 1 };
        D3DMATRIX viewMatrix{}, projectionMatrix{};
        std::memcpy(&viewMatrix, view, sizeof(viewMatrix));
        std::memcpy(&projectionMatrix, proj, sizeof(projectionMatrix));
        d->SetTransform(D3DTS_WORLD, &worldMatrix);
        d->SetTransform(D3DTS_VIEW, &viewMatrix);
        d->SetTransform(D3DTS_PROJECTION, &projectionMatrix);
        d->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);

        for (int i = 0; i < count_; ++i)
        {
            const Target& target = targets_[i];
            if (target.isPlayer || !target.drawMarker || !ValidateTarget(i)) continue;

            float pos[3];
            world::UnitPosition(target.unit, pos);
            float facing = world::UnitFacing(target.unit);
            m2::VisualFacing(target.model, facing);
            const float forward[2] = { std::cos(facing), std::sin(facing) };
            const MarkerStyle marker = MarkerStyleForUnit(target.unit, target.model, settings_);
            const bool drawTail = settings_.tailEnabled && unit::CreatureType(target.unit) == 2;
            if (!marker.enabled && !drawTail) continue;
            const ShapeProfile& shape = ProfileForShape(marker.shape);

            const auto drawChevron = [&](float direction, float tipDistance, float markerScale,
                                         float width, float length, const float color[4])
            {
                const float lineForward[2] = { forward[0] * direction, forward[1] * direction };
                const float right[2] = { -lineForward[1], lineForward[0] };

                const auto drawLayer = [&](float layerTipDistance, float baseDepth, float notchDepth,
                                           float halfWidth, float zOffset, const float layerColor[4])
                {
                    const float shapeLength = length * shape.lengthFactor;
                    const float baseDistance = layerTipDistance - baseDepth * markerScale * shapeLength;
                    const float notchDistance = layerTipDistance - notchDepth * markerScale * shapeLength *
                                                (shape.notchRatio / 0.615f);
                    const float layerWidth = halfWidth * markerScale * width * shape.widthFactor;
                    const float tip[3] = {
                        pos[0] + lineForward[0] * layerTipDistance,
                        pos[1] + lineForward[1] * layerTipDistance,
                        pos[2] + zOffset
                    };
                    const float left[3] = {
                        pos[0] + lineForward[0] * baseDistance + right[0] * layerWidth,
                        pos[1] + lineForward[1] * baseDistance + right[1] * layerWidth,
                        pos[2] + zOffset
                    };
                    const float notch[3] = {
                        pos[0] + lineForward[0] * notchDistance,
                        pos[1] + lineForward[1] * notchDistance,
                        pos[2] + zOffset
                    };
                    const float rightPt[3] = {
                        pos[0] + lineForward[0] * baseDistance - right[0] * layerWidth,
                        pos[1] + lineForward[1] * baseDistance - right[1] * layerWidth,
                        pos[2] + zOffset
                    };

                    d->SetPixelShaderConstantF(0, layerColor, 1);
                    const D3DCOLOR packed = D3DCOLOR_COLORVALUE(
                        layerColor[0], layerColor[1], layerColor[2], layerColor[3]);
                    if (shape.arrowhead)
                    {
                        const OverlayChevronVertex vertices[3] = {
                            { tip[0], tip[1], tip[2], packed }, { left[0], left[1], left[2], packed },
                            { rightPt[0], rightPt[1], rightPt[2], packed },
                        };
                        d->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, vertices, sizeof(OverlayChevronVertex));
                    }
                    else
                    {
                        const OverlayChevronVertex vertices[6] = {
                            { tip[0], tip[1], tip[2], packed }, { left[0], left[1], left[2], packed },
                            { notch[0], notch[1], notch[2], packed }, { tip[0], tip[1], tip[2], packed },
                            { notch[0], notch[1], notch[2], packed }, { rightPt[0], rightPt[1], rightPt[2], packed },
                        };
                        d->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, vertices, sizeof(OverlayChevronVertex));
                    }
                };

                const float rimColor[4] = {
                    0.025f + color[0] * 0.10f,
                    0.025f + color[1] * 0.10f,
                    0.025f + color[2] * 0.10f,
                    std::min(0.72f, color[3] + 0.16f)
                };
                const float ridgeColor[4] = {
                    color[0] + (1.0f - color[0]) * 0.58f,
                    color[1] + (1.0f - color[1]) * 0.58f,
                    color[2] + (1.0f - color[2]) * 0.58f,
                    color[3] * 0.38f
                };

                drawLayer(tipDistance,                         0.52f, 0.32f, 0.70f, 0.038f, rimColor);
                drawLayer(tipDistance - 0.025f * markerScale,  0.46f, 0.29f, 0.61f, 0.041f, color);
                drawLayer(tipDistance - 0.075f * markerScale,  0.28f, 0.17f, 0.38f, 0.044f, ridgeColor);
            };

            if (marker.enabled)
            {
                for (int index = marker.count - 1; index >= 1; --index)
                {
                    const float accentScale = marker.scale * (marker.boss ? 0.55f : 0.72f);
                    const float accentTip = std::max(0.15f, marker.tipDistance - marker.spacing * index);
                    const float accent[4] = {
                        marker.boss ? 1.0f : target.color[0] + (1.0f - target.color[0]) * 0.25f,
                        marker.boss ? 0.58f : target.color[1] + (1.0f - target.color[1]) * 0.25f,
                        marker.boss ? 0.08f : target.color[2] + (1.0f - target.color[2]) * 0.25f,
                        marker.boss ? std::min(1.0f, marker.opacity + 0.03f) : marker.opacity * 0.85f
                    };
                    drawChevron(1.0f, accentTip, accentScale, marker.width, marker.length, accent);
                }
            }
            if (drawTail)
            {
                const float tailWarning[4] = { 1.0f, 0.12f, 0.02f, settings_.tailOpacity };
                drawChevron(-1.0f, marker.tipDistance * settings_.tailDistanceMultiplier,
                            marker.scale * settings_.tailScaleMultiplier,
                            marker.width * settings_.tailWidth, marker.length * settings_.tailLength,
                            tailWarning);
            }
            if (marker.enabled)
            {
                const float reactionColor[4] = { target.color[0], target.color[1], target.color[2], marker.opacity };
                drawChevron(1.0f, marker.tipDistance, marker.scale, marker.width, marker.length, reactionColor);
            }
        }
    }

    void Outline::OnM2Batch(const ev::M2BatchDrawArgs& a)
    {
        if (a.uiModelPass) return;
        if (count_ == 0) return;
        const int idx = FindTarget(a.model);
        if (idx < 0 || !ValidateTarget(idx)) return;

        gx::Device9 dev(a.device);
        if (!IsOpaqueBatch(dev)) return;

        // Stamp while the native batch's vertex/index data is still bound, before any guide draws.
        // Immediate stamping also supports the client's transient CPU-skinned vertex buffers.
        if (targets_[idx].drawOutline && settings_.outlineEnabled && EnsureOutlineResources(dev))
            StampSilhouette(dev, a, idx);

        // Issue the ground guide after the target tree's first opaque batch. Existing terrain/WMO depth
        // plus that first batch clip it, while every later target batch naturally draws over it.
        if (readability_.dangerVisibility == combat::DangerGround &&
            targets_[idx].currentTarget && !targets_[idx].dangerAttempted)
        {
            targets_[idx].dangerAttempted = true;
            DrawDangerGuide(dev, idx);
        }

        // Restore the proven post-first-batch chevron slot. Its cached corner
        // heights make it terrain conforming without changing this depth ordering.
        if (targets_[idx].drawMarker && settings_.facingMode == kFacingMarkerGround &&
            !targets_[idx].markerDrawn && EnsureMarkerResources(dev) &&
            DrawFacingChevron(dev, idx))
            targets_[idx].markerDrawn = true;
    }

    void Outline::OnWorldLeave(ev::WorldLeaveArgs const&)
    {
        ReleaseMask();
        ReleaseStateBlock();
        count_ = 0;
        stateCapturedThisFrame_ = false;
    }

    void Outline::OnEndScene(const ev::EndSceneArgs& a)
    {
        gx::Device9 dev(a.device);
        if (!dev) return;

        // On the first/idle frame no M2 pass consumed the old snapshot, so pick up the configured values
        // before allocating resources. Active frames refresh below after their ground/outline work.
        if (count_ == 0)
            RefreshSettings();

        // Composite only the outline mask from the completed frame. Marker shader readiness cannot
        // suppress this pass, and outline allocation failures cannot suppress facing markers below.
        if (settings_.outlineEnabled && maskCleared_ && EnsureOutlineResources(dev))
            EdgePass(dev);

        // The silhouette mask belongs to the completed frame, but overlay geometry dereferences live world
        // objects. Refresh both settings and GUID-resolved targets after compositing the old mask, then render
        // the overlay from pointers acquired at this EndScene boundary rather than prior-frame raw pointers.
        RefreshSettings();
        RebuildTargets();
        PrepareGroundCaches();

        // Prepare each feature independently for the new snapshot. Ground geometry
        // consumes the cached terrain heights during the next target batch; explicit
        // overlay modes still render here without depth.
        if (settings_.outlineEnabled)
            EnsureOutlineResources(dev);
        if (settings_.facingMode == kFacingMarkerGround)
            EnsureMarkerResources(dev);
        else if (settings_.facingMode == kFacingMarkerOverlay && EnsureMarkerResources(dev))
            DrawFacingChevronsOverlay(dev);

        if (readability_.dangerVisibility == combat::DangerOverlay)
        {
            for (int i = 0; i < count_; ++i)
            {
                if (!targets_[i].currentTarget || !ValidateTarget(i))
                    continue;
                targets_[i].dangerAttempted = true;
                DrawDangerGuide(dev, i);
                break;
            }
        }
        maskCleared_ = false;

        // D3DSBT_ALL retains COM references to the captured streams/textures. Keep the reusable block
        // while outlining is active, but do not let an idle outline pin the last world's resources.
        if (count_ == 0 || !stateCapturedThisFrame_)
            ReleaseStateBlock();
        stateCapturedThisFrame_ = false;
    }

    // NATIVE: the Windows build self-registered with a file-scope instance at DLL load. Static
    // initialisation order is not safe here (the profile reader is another translation unit's static), so the
    // engine creates the instance explicitly once startup has finished.
    Outline* g_outline = nullptr;
}

void WxlUnitOutlineInit()
{
    if (!wxl::scripts::outline::g_outline)
        wxl::scripts::outline::g_outline = new wxl::scripts::outline::Outline();
}
