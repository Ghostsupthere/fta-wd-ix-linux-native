// WarcraftXL live-control ABI shared by WarcraftXL.dll and the native d3d9 proxy.
// Copyright (C) 2026 WarcraftXL
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#pragma once

#include <cstdint>
#include <type_traits>

namespace wxl::gpu::control
{
    inline constexpr uint32_t kApiVersion = 2;

    enum FacingMode : int32_t
    {
        FacingOff     = 0,
        FacingGround  = 1,
        FacingOverlay = 2,
    };

    enum ChevronShape : int32_t
    {
        ShapeClassic   = 0,
        ShapeWide      = 1,
        ShapeSharp     = 2,
        ShapeArrowhead = 3,
        ShapeCount     = 4,
    };

    /**
     * @brief Fixed-layout snapshot copied across the WarcraftXL.dll <-> d3d9.dll boundary.
     *
     * Numeric values are validated and clamped by the proxy. The renderer copies one snapshot at
     * EndScene and uses it for the following frame, so a Lua slider cannot split one frame between
     * two marker configurations.
     */
    struct Settings
    {
        uint32_t abiSize = sizeof(Settings);
        uint32_t apiVersion = kApiVersion;

        // Retained as zeroed ABI padding for compatibility with the deployed v2 control structure.
        // WarcraftXL's private FXAA/SSAO renderer profiles have been removed.
        int32_t activeRenderProfile = 0;
        int32_t savedRenderProfile = 0;
        int32_t outlineEnabled = 1;
        int32_t facingMode = FacingGround;
        int32_t showTarget = 1;
        int32_t showMouseover = 1;

        int32_t normalEnabled = 1;
        int32_t normalShape = ShapeClassic;
        int32_t normalCount = 1;
        float normalTipDistance = 3.00f;
        float normalSpacing = 0.50f;
        float normalScale = 1.00f;
        float normalWidth = 1.00f;
        float normalLength = 1.00f;
        float normalOpacity = 0.56f;
        float normalMaxModelFactor = 1.50f;
        float normalDistanceModelWeight = 0.50f;
        float normalSizeModelWeight = 0.75f;

        int32_t bossEnabled = 1;
        int32_t bossShape = ShapeClassic;
        int32_t bossCount = 2;
        float bossTipDistance = 8.50f;
        float bossSpacing = 1.20f;
        float bossScale = 2.40f;
        float bossWidth = 1.00f;
        float bossLength = 1.00f;
        float bossOpacity = 0.56f;
        float bossMaxModelFactor = 1.70f;
        float bossDistanceModelWeight = 0.70f;
        float bossSizeModelWeight = 0.75f;

        int32_t tailEnabled = 0;
        float tailDistanceMultiplier = 1.50f;
        float tailScaleMultiplier = 1.20f;
        float tailWidth = 1.00f;
        float tailLength = 1.00f;
        float tailOpacity = 0.50f;

        // Appended in API v2 so the complete v1 field prefix remains stable across coordinated upgrades.
        // Elite, rare-elite, and rare creatures intentionally share this one presentation category.
        int32_t eliteEnabled = 1;
        int32_t eliteShape = ShapeClassic;
        int32_t eliteCount = 1;
        float eliteTipDistance = 4.50f;
        float eliteSpacing = 0.75f;
        float eliteScale = 1.35f;
        float eliteWidth = 1.10f;
        float eliteLength = 1.00f;
        float eliteOpacity = 0.56f;
        float eliteMaxModelFactor = 1.60f;
        float eliteDistanceModelWeight = 0.60f;
        float eliteSizeModelWeight = 0.75f;
    };

    static_assert(sizeof(Settings) == 200, "WarcraftXL control ABI size changed");
    static_assert(std::is_standard_layout<Settings>::value, "WarcraftXL control ABI must remain standard-layout");
}


