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

#pragma once

#include "events/Event.hpp"
#include "events/EventScript.hpp"
#include "game/combat/Readability.hpp"
#include "game/gx/Gx.hpp"
#include "gpu/Control.hpp"

// Reference render script. It owns its shaders, its target list and its render targets, and draws
// purely through the core gx facade and the unit/world bindings. It never touches an offset or installs
// a hook: it binds two member functions to render events and the core does the rest.
//
// Pipeline (one-frame): EndScene of frame N rebuilds the target list for frame N+1; during frame N+1 the
// per-batch event stamps each target's silhouette into a mask; EndScene of N+1 edge-detects the mask
// into the frame, then rebuilds again.
// Legacy visibility is intentional: NPC outlines can show through world geometry, and players use
// only scene depth already present at their native batch. Completed-world occlusion is rolled back.
namespace wxl::scripts::outline
{
    class Outline final : public events::EventScript
    {
    public:
        Outline(); // binds the event handlers
        ~Outline();

    private:
        // --- event handlers ---
        void OnEndScene(const events::EndSceneArgs& a);
        void OnWorldLeave(events::WorldLeaveArgs const& a);
        void OnM2Batch(const events::M2BatchDrawArgs& a);
        void OnDeviceLost(const events::DeviceResetArgs& a);
        void OnDeviceReset(const events::DeviceResetArgs& a);

        // --- steps ---
        bool EnsureOutlineResources(game::gx::Device9 dev); // outline-only shaders and mask RT
        bool EnsureMarkerResources(game::gx::Device9 dev);  // facing-only shader for the active mode
        bool EnsureStateBlock(game::gx::Device9 dev); // reusable D3D state capture for every outline draw
        void ReleaseShaders();
        void ReleaseMask();
        void ReleaseStateBlock();
        bool EnsureChevronTexture(game::gx::Device9 dev, int shape); // build one shape texture lazily
        void ReleaseChevronTextures();
        void RefreshSettings();
        void RebuildTargets();                        // mouseover/target -> colored entries
        void PrepareGroundCaches();                  // collision samples outside the draw callback
        void PrepareDangerGroundCache(int targetIndex);
        void PrepareMarkerGroundCache(int targetIndex);
        void StampSilhouette(game::gx::Device9 dev, const events::M2BatchDrawArgs& a, int idx);
        void EdgePass(game::gx::Device9 dev);         // composite the mask into the frame
        bool DrawFacingChevron(game::gx::Device9 dev, int targetIndex); // depth-tested ground decal
        bool DrawDangerGuide(game::gx::Device9 dev, int targetIndex); // bounded target ring/frontal guide
        void DrawFacingChevronsOverlay(game::gx::Device9 dev);         // exact prior overlay path

        // --- helpers ---
        bool IsOpaqueBatch(game::gx::Device9 dev) const;
        int  FindTarget(void* model) const;           // model or any parent in the list
        bool ValidateTarget(int targetIndex) const;   // re-resolve GUID before dereferencing cached pointers
        void AddTarget(unsigned long long guid, void* player, bool drawOutline, bool drawMarker,
                       bool currentTarget, const float* colorOverride = nullptr);
        static void ColorForReaction(int reaction, float* outRgba);

        static constexpr int kMaxTargets = 2;
        static constexpr int kDangerRingSegments = 64;
        static constexpr int kDangerConeSegments = 32;
        static constexpr int kDangerConeBands = 4;
        static constexpr int kMaxGroundMarkerQuads = 8;
        struct Target
        {
            unsigned long long guid;
            void* model;
            void* unit;
            float color[4];
            bool isPlayer;
            bool drawOutline;
            bool drawMarker;
            bool markerDrawn;
            bool dangerAttempted;
            bool currentTarget;
            int creatureRank;
        };

        struct DangerGroundCache
        {
            unsigned long long guid = 0;
            uint32_t sampledAt = 0;
            float center[3]{};
            float facing = 0.0f;
            float ringRadius = 0.0f;
            float ringWidth = 0.0f;
            float coneRange = 0.0f;
            float coneAngle = 0.0f;
            bool ringValid = false;
            bool coneValid = false;
            float ringZ[2][kDangerRingSegments + 1]{};
            float coneZ[kDangerConeBands + 1][kDangerConeSegments + 1]{};
        };

        struct MarkerGroundQuad
        {
            uint32_t sampledAt = 0;
            bool valid = false;
            float xy[4][2]{};
            float z[4]{};
        };

        struct MarkerGroundCache
        {
            unsigned long long guid = 0;
            MarkerGroundQuad quads[kMaxGroundMarkerQuads]{};
        };

        Target                     targets_[kMaxTargets]{};
        int                        count_       = 0;
        game::gx::RenderTarget     mask_{};
        void*                      maskDevice_ = nullptr;
        void*                      colorPS_     = nullptr; // fills opaque batches into the silhouette mask
        void*                      cutoutPS_    = nullptr; // fills alpha-tested batches into the silhouette mask
        void*                      edgePS_      = nullptr; // edge-detects the mask into a line
        void*                      chevronPS_   = nullptr; // tints the textured facing decal
        void*                      overlayChevronPS_ = nullptr; // solid-color shader used by the prior overlay
        void*                      shaderDevice_ = nullptr;
        void*                      chevronTextures_[gpu::control::ShapeCount]{}; // managed textures by shape
        void*                      chevronDevice_ = nullptr;
        void*                      stateBlock_ = nullptr; // one reusable D3DSBT_ALL block for the live device
        void*                      stateDevice_ = nullptr;
        gpu::control::Settings     settings_{}; // immutable snapshot for one complete rendered frame
        game::combat::ReadabilitySettings readability_{};
        DangerGroundCache          dangerGround_{};
        MarkerGroundCache          markerGround_[kMaxTargets]{};
        bool                       maskCleared_ = false;
        bool                       stateCapturedThisFrame_ = false;
        float                      thickness_   = 1.5f;
    };
}
