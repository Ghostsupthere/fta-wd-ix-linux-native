// Option table, clamping and Environment.ini round-trip for the control add-on's Environment page.
#include "Engine/Source/ModernAssets/WxlGrass/EnvironmentOptions.hpp"
#include "Engine/Source/ModernAssets/WxlGrass/GrassBudget.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace wxl::grass::environment;
static void Require(bool ok, const char* what) { if (!ok) { std::fprintf(stderr, "FAILED: %s\n", what); std::exit(1); } }

int main() {
    Values defaults;
    Require(kCount == 17, "17 options");
    Require(defaults.v[kLightsEnabled] == 0 && defaults.v[kLightsModels] == 0 && defaults.v[kGrassMemoryMB] == 64, "defaults (surface lights off)");

    // Clamping, integer rounding, non-finite input.
    Require(Normalize(kGrassWindStrength, 9) == 2, "clamp high");
    Require(Normalize(kGrassWindStrength, -3) == 0, "clamp low");
    Require(Normalize(kLightsPerCell, 7.6) == 8, "integer rounding");
    Require(Normalize(kLightsRange, NAN) == 160, "NaN -> initial");
    Require(Normalize(kLightsRange, INFINITY) == 160, "inf -> initial");

    // Find is exact.
    Require(Find("lights.range") == kLightsRange, "find");
    Require(Find("lights.rang") == kCount && Find("lights.rangeX") == kCount && Find("") == kCount, "find is exact");

    // The saved file the Windows client wrote on this install.
    const char* saved =
        "; Live WarcraftXL grass and surface-light settings.\r\n[Environment]\r\n"
        "grass.instanced=1\r\ngrass.memoryMB=64\r\ngrass.buildMs=2\r\ngrass.windStrength=1\r\n"
        "grass.windSpeed=1\r\ngrass.interaction=1\r\nlights.enabled=0\r\nlights.models=0\r\n"
        "lights.brightness=1.4\r\nlights.radius=20\r\nlights.range=120\r\nlights.perCell=8\r\n"
        "lights.flicker=0.2\r\nlights.flickerSpeed=1\r\nlights.occlusion=0\r\nlights.raysPerFrame=256\r\n"
        "lights.indoorIsolation=0\r\n";
    Values loaded;
    Require(Parse(saved, loaded) == kCount, "all 17 read");
    Require(loaded.v[kLightsEnabled] == 0 && loaded.v[kLightsBrightness] == 1.4 && loaded.v[kLightsRange] == 120, "values");

    // Round trip.
    Values again;
    Require(Parse(Format(loaded), again) == kCount, "round trip count");
    for (size_t i = 0; i < kCount; ++i) Require(again.v[i] == loaded.v[i], "round trip value");

    // Hostile input: other sections, bad numbers, unknown keys, out-of-range, no trailing newline,
    // a huge value token, comments, keys before any section.
    Values hostile;
    const std::string text =
        "lights.range=200\n[Other]\nlights.range=210\n[Environment]\nlights.range=abc\nlights.radius=nan\n"
        "lights.perCell=1000\nunknown.key=3\n  grass.windSpeed = 0.5  \n;grass.windStrength=0\n"
        "lights.flicker=" + std::string(200, '9') + "\nlights.brightness=1e999\ngrass.interaction=0";
    const size_t read = Parse(text, hostile);
    Require(hostile.v[kLightsRange] == 160, "bad number leaves the default, other sections ignored");
    Require(hostile.v[kLightsRadius] == 16, "nan rejected");
    Require(hostile.v[kLightsPerCell] == 16, "clamped");
    Require(hostile.v[kGrassWindSpeed] == 0.5, "whitespace trimmed");
    Require(hostile.v[kGrassWindStrength] == 1, "comment ignored");
    Require(hostile.v[kLightsFlicker] == 0.15, "overlong token rejected");
    Require(hostile.v[kLightsBrightness] == 1, "overflow rejected");
    Require(hostile.v[kGrassInteraction] == 0, "last line without newline");
    Require(read == 3, "only accepted lines counted");

    Values empty;
    Require(Parse("", empty) == 0 && Parse("\n\n[Environment]", empty) == 0, "empty");

    const Motion m = MotionOf(loaded);
    Require(m.strength == 1.0f && m.speed == 1.0f && m.interaction, "motion");
    // Frame summary: nearest-rank percentiles, invalid samples dropped.
    std::vector<double> ms;
    for (int i = 1; i <= 100; ++i) ms.push_back(i);
    ms.push_back(NAN); ms.push_back(-1);
    const FrameSummary f = Summarize(ms);
    Require(f.frames == 100 && f.medianMs == 50 && f.p95Ms == 95 && f.p99Ms == 99 && f.worstMs == 100, "percentiles");
    Require(Summarize({}).frames == 0 && Summarize({NAN}).frames == 0, "empty summary");
    Require(Summarize({7.0}).medianMs == 7.0 && Summarize({7.0}).p99Ms == 7.0, "single sample");
    // Grass cache and build budget.
    {
        namespace gb = wxl::grass::budget;
        Require(gb::LimitBytes(64) == 64ull * 1024 * 1024 && gb::LimitBytes(0) == 0 && gb::LimitBytes(-5) == 0 && gb::LimitBytes(NAN) == 0, "limit bytes");
        int a = 0, b = 0, c = 0;
        std::vector<gb::CachedItem> items = {{&a, 400, 5}, {&b, 300, 1}, {&c, 200, 3}};
        auto roomy = gb::PlanEvictions(items, 100, 1000);
        Require(roomy.fits && roomy.evict.empty(), "under the limit: nothing evicted");
        auto plan = gb::PlanEvictions(items, 300, 1000);   // 900 + 300 = 1200 > 1000
        Require(plan.fits && plan.evict.size() == 1 && plan.evict[0] == &b, "oldest evicted first");
        plan = gb::PlanEvictions(items, 800, 1000);        // needs 700 freed: b (300) + c (200) + a (400)
        Require(plan.fits && plan.evict.size() == 3 && plan.evict[0] == &b && plan.evict[1] == &c && plan.evict[2] == &a, "evicts in least-recently-used order until it fits");
        plan = gb::PlanEvictions(items, 1001, 1000);
        Require(!plan.fits && plan.evict.empty(), "a batch bigger than the whole limit is refused");
        plan = gb::PlanEvictions({}, 10, 0);
        Require(!plan.fits, "zero limit refuses everything");
        plan = gb::PlanEvictions({}, 0, 0);
        Require(plan.fits, "nothing to add fits");
        std::vector<gb::CachedItem> tie = {{&c, 10, 7}, {&a, 10, 7}};
        Require(gb::PlanEvictions(tie, 10, 20).evict.size() == 1, "ties are resolved deterministically");
        gb::BuildBudget budget;
        budget.SetMilliseconds(2.0);
        Require(budget.MayStart(), "first build may start");
        budget.Spend(5.0);
        Require(!budget.MayStart(), "an overrun build blocks later ones this frame (soft budget)");
        budget.NewFrame();
        Require(budget.MayStart() && budget.Spent() == 0.0, "new frame resets");
        budget.Spend(-3.0); budget.Spend(NAN);
        Require(budget.Spent() == 0.0, "negative and NaN samples are ignored");
        budget.SetMilliseconds(-1);
        Require(!budget.MayStart(), "zero budget blocks all builds");
    }
    std::puts("ok");
    return 0;
}
