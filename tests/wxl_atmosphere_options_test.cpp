#include "Engine/Source/ModernAssets/WxlVisuals/atmosphere_options.h"
#include "Engine/Source/ModernAssets/WxlVisuals/atmosphere_regions.h"

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
    base.enable = false;
    assert(!ApplyOptions(base, controls).enable && !ApplyOptions(base, controls).water);

    wxl::atmosphere::RegionalFog regions;
    Config westfall;
    regions.Apply(westfall, 0, {40}, 0.5f, 0, 0.0f, 20.0);
    assert(std::abs(westfall.density - 1.05270f) < 0.0001f);
    Config child;
    regions.Apply(child, 1, {9999, 40}, 0.5f, 0, 0.0f, 20.5);
    assert(child.density == westfall.density);
    Config crossing;
    regions.Apply(crossing, 1, {9998}, 0.5f, 0, 0.0f, 21.0);
    assert(crossing.density == child.density);
    Config interpolated;
    regions.Apply(interpolated, 1, {9998}, 0.5f, 0, 0.0f, 22.0);
    assert(interpolated.density > 1.0f && interpolated.density < crossing.density);
    Config otherMap;
    regions.Apply(otherMap, 530, {}, 0.5f, 0, 0.0f, 22.1);
    assert(otherMap.density == 1.0f);
}
