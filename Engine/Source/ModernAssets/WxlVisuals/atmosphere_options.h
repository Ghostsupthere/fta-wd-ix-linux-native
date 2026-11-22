#pragma once

#include "config.h"
#include "Engine/Source/ModernAssets/WxlGrass/EnvironmentOptions.hpp"

namespace wxl::atmosphere {
namespace env = wxl::grass::environment;

inline Config ApplyOptions(const Config& base, const env::Values& values)
{
    const auto on = [&](env::Index key) { return values.v[key] != 0; };
    const auto number = [&](env::Index key) { return static_cast<float>(values.v[key]); };
    if (!on(env::kAtmosphereOverride) || !base.enable || !base.hooks)
        return base;

    Config config = base;
    const bool fog = on(env::kFog) && number(env::kDensity) > 0;
    // The fog always marches at quarter resolution (quality 1): the higher levels cost 1-2 ms and look the same.
    // The saved atmosphere.quality value is ignored.
    config.quality = 1;
    config.density = fog ? 1.0f : 0.0f;
    config.fogDensityScale = number(env::kDensity);
    config.haze = fog ? 1.0f : 0.0f;
    config.farFog = fog ? 1.0f : 0.0f;
    config.groundFog = fog && on(env::kMist) ? number(env::kMistStrength) : 0.0f;
    config.stockFog = fog ? 1 : 0;
    config.sunScatter = fog ? 1.0f : 0.0f;
    config.transparentFog = fog && on(env::kTransparent);
    config.godRays = on(env::kShafts) ? number(env::kShaftStrength) : 0.0f;
    config.interiorAware = fog || config.godRays > 0.0f;
    config.weatherDrift = on(env::kWeatherDrift);
    config.shaftEdges = on(env::kShaftEdges);
    const float variation = fog && on(env::kDrift) ? number(env::kVariation) : 0.0f;
    config.noiseAmount = variation * 0.15f;
    config.fogDetailBoost = std::max(0.0f, variation - 1.0f);
    config.noiseScale = 1.0f / number(env::kPatch);
    config.noiseWindSpeed = number(env::kWind);
    config.localLights = fog && (on(env::kLights) || on(env::kLantern));
    config.localLightIntensity = on(env::kLights) ? number(env::kLightStrength) : 1.0f;

    config.water = on(env::kWater);
    config.waterQuality = static_cast<int>(values.v[env::kWaterQuality]);
    config.waterWaves = on(env::kWaves) ? number(env::kWaveStrength) : 0.0f;
    config.waterRipples = on(env::kWaterRipples) ? number(env::kRippleStrength) : 0.0f;
    config.waterFoam = number(env::kWaterFoam);
    config.waterReflections = number(env::kWaterReflections);
    config.waterClarity = number(env::kWaterClarity);
    return config;
}
}
