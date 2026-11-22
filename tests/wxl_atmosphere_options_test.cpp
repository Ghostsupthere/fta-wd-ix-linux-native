#include "Engine/Source/ModernAssets/WxlVisuals/atmosphere_options.h"
#include "Engine/Source/ModernAssets/WxlVisuals/atmosphere_regions.h"

#include "Engine/Source/ModernAssets/WxlVisuals/fog_wind.h"
#include "Engine/Source/ModernAssets/WxlVisuals/night_glow.h"
#include "Engine/Source/ModernAssets/WxlVisuals/cloud_shade.h"
#include "Engine/Source/ModernAssets/WxlVisuals/corpse_light_list.h"
#include "Engine/Source/ModernAssets/WxlVisuals/spell_light_list.h"
#include "Engine/Source/ModernAssets/WxlVisuals/water_rain.h"

#include <cassert>

int main()
{
    namespace options = wxl::grass::environment;
    using wxl::atmosphere::ApplyOptions;
    options::Values controls;
    Config base;
    base.density = 0.8f;
    base.water = false;
    assert(ApplyOptions(base, controls).density == 0.8f);
    assert(!ApplyOptions(base, controls).water);

    controls.v[options::kAtmosphereOverride] = 1;
    controls.v[options::kFog] = 0;
    controls.v[options::kShafts] = 1;
    controls.v[options::kLantern] = 1;
    controls.v[options::kWaves] = 0;
    const auto shaftsOnly = ApplyOptions(base, controls);
    assert(shaftsOnly.density == 0.0f && shaftsOnly.stockFog == 0 && shaftsOnly.godRays > 0.0f);
    assert(!shaftsOnly.localLights && shaftsOnly.water && shaftsOnly.waterWaves == 0.0f);
    controls.v[options::kFog] = 1;
    controls.v[options::kDensity] = 1.25;
    controls.v[options::kTransparent] = 1;
    const auto fog = ApplyOptions(base, controls);
    assert(fog.stockFog && fog.transparentFog && fog.localLights && fog.fogDensityScale == 1.25f);
    assert(fog.noiseAmount == 0.15f && fog.fogDetailBoost == 0.0f);
    assert(fog.noiseWindSpeed == 5.0f && fog.noiseScale == 1.0f / 50.0f);
    controls.v[options::kVariation] = 2.0;
    const auto high = ApplyOptions(base, controls);
    assert(high.noiseAmount == 0.30f && high.fogDetailBoost == 1.0f);
    controls.v[options::kDrift] = 0;
    const auto smooth = ApplyOptions(base, controls);
    assert(smooth.noiseAmount == 0.0f && smooth.fogDetailBoost == 0.0f);
    base.enable = false;
    assert(!ApplyOptions(base, controls).enable && !ApplyOptions(base, controls).water);

    wxl::atmosphere::RegionalFog regions;
    Config westfall;
    regions.Apply(westfall, 0, {40}, 0.5f, 0, 0.0f, 20.0);
    assert(std::abs(westfall.density - 1.05270f) < 0.0001f);
    Config child;
    regions.Apply(child, 1, {9999, 40}, 0.5f, 0, 0.0f, 20.5);
    assert(child.density == westfall.density);
    assert(regions.ProfileArea() == 40);
    Config crossing;
    regions.Apply(crossing, 1, {9998}, 0.5f, 0, 0.0f, 21.0);
    assert(crossing.density == child.density);
    Config interpolated;
    regions.Apply(interpolated, 1, {9998}, 0.5f, 0, 0.0f, 22.0);
    assert(interpolated.density > 1.0f && interpolated.density < crossing.density);
    Config otherMap;
    regions.Apply(otherMap, 530, {}, 0.5f, 0, 0.0f, 22.1);
    assert(otherMap.density == 1.0f && regions.ProfileArea() == 0);

    wxl::atmosphere::FogWind wind;
    assert(wind.Advance(10.0, 5.0f, 100.0) == 50.0f);
    assert(wind.Advance(11.0, 12.0f, 100.0) == 55.0f);
    assert(wind.Advance(12.0, 0.0f, 100.0) == 67.0f);
    assert(wind.Advance(30.0, 5.0f, 100.0) == 67.0f);
    assert(wind.Advance(37.0, 5.0f, 100.0) == 2.0f);

    wxl::atmosphere::NightGlowHandoff halo;
    assert(halo.Advance(0.0, false) == 1.0f);
    assert(halo.Advance(0.0, true) == 1.0f);
    assert(std::abs(halo.Advance(0.08, true) - 0.5f) < 0.0001f);
    assert(halo.Advance(0.17, true) == 0.0f);
    assert(halo.Advance(0.17, false) == 0.0f);
    assert(std::abs(halo.Advance(0.22, false) - 0.2f) < 0.0001f);
    halo.Reset();
    assert(halo.Advance(1.0, true) == 0.0f);

    float eye[3] = {};
    wxl::atmosphere::CorpseLightList corpses;
    for (unsigned i = 7; i > 0; --i) {
        const float position[] = {static_cast<float>(i), 0, 0};
        corpses.Add(i, position, eye);
    }
    const float farAway[] = {100, 0, 0};
    corpses.Add(100, farAway, eye);
    assert(corpses.count == 4 && corpses.lights[0].id == 1 && corpses.lights[3].id == 4);
    assert(corpses.lights[0].position[2] == 0.28f);

    wxl::atmosphere::SpellLightList spells;
    const float gold[] = {10, 5, 0};
    const float first[] = {4, 0, 0}, second[] = {2, 0, 0}, nearest[] = {0, 0, 0};
    assert(!spells.Add(farAway, gold, eye, 1));
    assert(spells.Add(first, gold, eye, 1) && spells.Add(first, gold, eye, 1) && spells.count == 1);
    assert(spells.Add(second, gold, eye, 1) && spells.Add(nearest, gold, eye, 1) && spells.count == 2);
    assert(spells.lights[1].distanceSquared == 4 && spells.lights[0].colour[1] == 0.275f);

    const float cloud[] = {0.24f, 0, 0, 0.5f};
    assert(wxl::atmosphere::CloudShade(cloud, 0, 1) == 1.0f);
    assert(wxl::atmosphere::CloudShade(cloud, 1, 1) == wxl::atmosphere::CloudShade(cloud, -1, -1));
    assert(std::abs(wxl::atmosphere::CloudShade(cloud, 1, 1) - 0.76f) < 0.0001f);

    WaterRippleDisturbance drop;
    assert(!WaterRainImpact(eye, 32, 0.5, 0, 0, drop));
    assert(WaterRainImpact(eye, 32, 0.5, 1, 0, drop));
    assert(std::abs(drop.to[0]) < 16 && std::abs(drop.to[1]) < 16 && drop.amplitude > 0);
}
