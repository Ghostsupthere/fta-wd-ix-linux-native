#include "Engine/Source/ModernAssets/WxlCombatReadability/ReadabilitySettings.hpp"
#include <cassert>
#include <limits>

int main() {
    using namespace wxl::readability;
    Store store;
    double v = -1;
    // Defaults match the Windows module.
    assert(store.Get("enabled", v) && v == 0);
    assert(store.Get("danger.coneRange", v) && v == 8.0f);
    assert(store.Get("danger.category", v) && v == static_cast<double>(DangerEliteAndBoss));
    assert(store.Get("danger.visibility", v) && v == static_cast<double>(DangerGround));
    assert(store.Get("outline.protectedPalette", v) && v == 2);
    assert(!store.Get("nope", v) && !store.Get(nullptr, v) && !store.Set("nope", 1) && !store.Set(nullptr, 1));
    // Every key is unique and readable.
    for (const Option& a : kOptions) {
        assert(store.Get(a.key, v));
        int same = 0;
        for (const Option& b : kOptions) same += std::strcmp(a.key, b.key) == 0;
        assert(same == 1);
    }
    // Clamping, rounding and non-finite handling.
    assert(store.Set("danger.coneAngle", 500) && store.Get("danger.coneAngle", v) && v == 180.0);
    assert(store.Set("danger.coneAngle", -3) && store.Get("danger.coneAngle", v) && v == 15.0);
    assert(store.Set("danger.coneRange", 12.5) && store.Get("danger.coneRange", v) && v == 12.5);
    assert(store.Set("enabled", 0.6) && store.Get("enabled", v) && v == 1);
    assert(store.Set("enabled", 7) && store.Get("enabled", v) && v == 1);
    assert(store.Set("danger.palette", std::numeric_limits<double>::quiet_NaN()) && store.Get("danger.palette", v) && v == 0);
    assert(store.Set("danger.maxDistance", std::numeric_limits<double>::infinity()) && store.Get("danger.maxDistance", v) && v == 5.0);
    assert(store.Set("danger.visibility", 9) && store.Get("danger.visibility", v) && v == static_cast<double>(DangerOverlay));
    assert(store.Set("danger.visibility", 0) && store.Get("danger.visibility", v) && v == static_cast<double>(DangerGround));
    Settings snap = store.Snapshot();
    assert(snap.coneRange == 12.5f && snap.enabled == 1);
    store.Reset();
    assert(store.Get("danger.coneRange", v) && v == 8.0f && store.Snapshot().enabled == 0);
    float rgb[3];
    PaletteColor(1, rgb);
    assert(rgb[0] == 1.0f && rgb[1] == 0.06f);
    PaletteColor(99, rgb); assert(rgb[0] == 1.00f && rgb[1] == 0.86f);
    PaletteColor(-4, rgb); assert(rgb[1] == 0.38f);
    return 0;
}
