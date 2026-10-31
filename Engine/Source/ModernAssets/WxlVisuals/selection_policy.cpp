#include "selection_policy.h"
#include <algorithm>
#include <cmath>
namespace { float SelectedAmount(float value,float fallback) { return value>0.0f?value:fallback; } }
void ApplyVisualSelection(Config& config, const VisualSelection& selection)
{
    config.enable = selection.enabled;
    config.hooks = selection.enabled && config.hooks;
    config.water = selection.enabled && selection.water;
    config.multisampling = selection.enabled && selection.multisampling;
    config.fogDensityScale = selection.enabled && std::isfinite(selection.fogDensityScale)
                                 ? std::clamp(selection.fogDensityScale, 0.0f, 2.0f)
                                 : 1.0f;
    config.noiseAmount = selection.enabled && selection.densityVariation ? config.noiseAmount : 0.0f;
    config.farFog = selection.enabled && selection.distanceFog ? config.farFog : 0.0f;
    config.haze = selection.enabled && selection.distanceHaze ? config.haze : 0.0f;
    config.groundFog = selection.enabled && selection.groundMist ? SelectedAmount(config.groundFog, 0.6f) : 0.0f;
    config.godRays = selection.enabled && selection.godRays ? SelectedAmount(config.godRays, 0.2f) : 0.0f;
    config.stockFog = selection.enabled && selection.distanceFog && config.farFog > 0.0f &&
                      config.density > 0.0f && config.fogDensityScale > 0.0f;
    const bool medium = config.enable && config.density > 0.0f && config.fogDensityScale > 0.0f &&
                        (config.haze > 0.0f || config.farFog > 0.0f || config.groundFog > 0.0f);
    config.sunScatter = medium && selection.forwardScattering ? config.sunScatter : 0.0f;
    config.localLights = medium && selection.localLights;
    if (config.localLights)
        config.localLightIntensity = SelectedAmount(config.localLightIntensity, 1.0f);
    const bool atmosphere = medium || config.godRays > 0.0f;
    config.interiorAware = atmosphere && selection.interiorTransitions;
    config.glowCompensation = atmosphere && selection.glowCompensation;
    config.transparentFog = config.stockFog == 1 && selection.transparentFog;
    config.dataMode = 0;
    config.classicNoise = false;
    config.classicPhase = 0;
    config.foreverGlow = 0;
    config.colorGrading = 0.0f;
    config.farClipMax = selection.enabled && selection.extendedViewDistance
                           ? SelectedAmount(config.farClipMax, kEngineFarClipMax)
                           : 0.0f;
    config.underwater = false;
    config.overlay = false;
    config.sunMarker = false;
    config.debugView = 0;
    config.waterDebugView = 0;
    config.waterRipples = config.water && selection.waterRipples ? SelectedAmount(config.waterRipples, 0.5f) : 0.0f;
    config.waterClientSplashes = true;
}

bool VisualFogEnabled(const Config& config)
{
    return config.enable && (config.godRays > 0.0f ||
                            (config.density > 0.0f && config.fogDensityScale > 0.0f &&
                             (config.haze > 0.0f || config.farFog > 0.0f || config.groundFog > 0.0f)));
}
