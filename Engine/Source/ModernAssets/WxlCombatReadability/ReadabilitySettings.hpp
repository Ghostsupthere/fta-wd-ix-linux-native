#pragma once
// Settings store behind WarcraftXL's combat-readability Lua bridge (wxl-combat-readability,
// GPL-3.0-or-later, Copyright (C) 2026 WarcraftXL): the key names, ranges and defaults the
// WarcraftXLControl add-on writes. Pure, so it is covered by tests/wxl_readability_settings_test.cpp.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace wxl::readability {

enum DangerVisibility : int32_t { DangerGround = 1, DangerOverlay = 2 };
enum DangerCategory : int32_t { DangerAllHostile = 0, DangerEliteAndBoss = 1, DangerBossOnly = 2 };

struct Settings {
    // Legacy combat readability defaults off. The optional frontal guide opts into only the cone path.
    int32_t enabled = 0;
    int32_t targetOutline = 0;
    int32_t mouseoverOutline = 0;
    float outlineThickness = 2.0f;
    float outlineOpacity = 0.90f;
    int32_t pulseEnabled = 0;
    float pulseSpeed = 2.0f;
    float pulseAmount = 0.22f;
    int32_t castContrast = 0;
    int32_t castState = 0;  // 0 idle, 1 interruptible, 2 protected
    int32_t castPalette = 0;
    int32_t protectedPalette = 2;
    int32_t bossContrast = 0;
    int32_t bossPalette = 1;

    int32_t dangerEnabled = 0;
    int32_t dangerVisibility = DangerGround;
    int32_t dangerCategory = DangerEliteAndBoss;
    int32_t ringEnabled = 0;
    float ringRadius = 5.0f;
    float ringWidth = 0.40f;
    float ringOpacity = 0.28f;
    int32_t coneEnabled = 0;
    float coneRange = 8.0f;
    float coneAngle = 90.0f;
    float coneOpacity = 0.22f;
    float bossScale = 1.35f;
    float maxDistance = 60.0f;
    int32_t dangerPalette = 1;
};

struct Option {
    const char* key;
    bool isInt;
    int32_t Settings::*intMember;
    float Settings::*floatMember;
    double low, high;
};

#define WXL_RD_I(name, member, lo, hi) {name, true, &Settings::member, nullptr, lo, hi}
#define WXL_RD_F(name, member, lo, hi) {name, false, nullptr, &Settings::member, lo, hi}
inline constexpr Option kOptions[] = {
    WXL_RD_I("enabled", enabled, 0, 1),
    WXL_RD_I("outline.target", targetOutline, 0, 1),
    WXL_RD_I("outline.mouseover", mouseoverOutline, 0, 1),
    WXL_RD_F("outline.thickness", outlineThickness, 0.75, 5.0),
    WXL_RD_F("outline.opacity", outlineOpacity, 0.10, 1.0),
    WXL_RD_I("outline.pulse", pulseEnabled, 0, 1),
    WXL_RD_F("outline.pulseSpeed", pulseSpeed, 0.25, 6.0),
    WXL_RD_F("outline.pulseAmount", pulseAmount, 0.0, 0.75),
    WXL_RD_I("outline.castContrast", castContrast, 0, 1),
    WXL_RD_I("outline.castState", castState, 0, 2),
    WXL_RD_I("outline.castPalette", castPalette, 0, 4),
    WXL_RD_I("outline.protectedPalette", protectedPalette, 0, 4),
    WXL_RD_I("outline.bossContrast", bossContrast, 0, 1),
    WXL_RD_I("outline.bossPalette", bossPalette, 0, 4),
    WXL_RD_I("danger.enabled", dangerEnabled, 0, 1),
    WXL_RD_I("danger.visibility", dangerVisibility, DangerGround, DangerOverlay),
    WXL_RD_I("danger.category", dangerCategory, DangerAllHostile, DangerBossOnly),
    WXL_RD_I("danger.ring", ringEnabled, 0, 1),
    WXL_RD_F("danger.ringRadius", ringRadius, 1.0, 30.0),
    WXL_RD_F("danger.ringWidth", ringWidth, 0.10, 3.0),
    WXL_RD_F("danger.ringOpacity", ringOpacity, 0.05, 1.0),
    WXL_RD_I("danger.cone", coneEnabled, 0, 1),
    WXL_RD_F("danger.coneRange", coneRange, 1.0, 40.0),
    WXL_RD_F("danger.coneAngle", coneAngle, 15.0, 180.0),
    WXL_RD_F("danger.coneOpacity", coneOpacity, 0.05, 1.0),
    WXL_RD_F("danger.bossScale", bossScale, 1.0, 3.0),
    WXL_RD_F("danger.maxDistance", maxDistance, 5.0, 120.0),
    WXL_RD_I("danger.palette", dangerPalette, 0, 4),
};
#undef WXL_RD_I
#undef WXL_RD_F

inline const Option* Find(const char* key) {
    if (!key) return nullptr;
    for (const Option& option : kOptions) {
        if (std::strcmp(key, option.key) == 0) return &option;
    }
    return nullptr;
}

// Thread-safe store: the Lua bridge writes, a renderer snapshots.
class Store {
public:
    Settings Snapshot() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_settings;
    }

    bool Get(const char* key, double& value) const {
        const Option* option = Find(key);
        if (!option) return false;
        std::lock_guard<std::mutex> lock(m_mutex);
        value = option->isInt ? static_cast<double>(m_settings.*option->intMember)
                              : static_cast<double>(m_settings.*option->floatMember);
        return true;
    }

    // Non-finite input clamps to the option's low bound; integers round to nearest.
    bool Set(const char* key, double value) {
        const Option* option = Find(key);
        if (!option) return false;
        if (!std::isfinite(value)) value = option->low;
        std::lock_guard<std::mutex> lock(m_mutex);
        if (option->isInt) {
            m_settings.*option->intMember = static_cast<int32_t>(std::clamp(std::llround(value), static_cast<long long>(option->low), static_cast<long long>(option->high)));
        } else {
            m_settings.*option->floatMember = static_cast<float>(std::clamp(value, option->low, option->high));
        }
        return true;
    }

    void Reset() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_settings = Settings{};
    }

private:
    mutable std::mutex m_mutex;
    Settings m_settings;
};

// The five outline/danger colours (amber, red, violet, cyan, yellow).
inline void PaletteColor(int palette, float out[3]) {
    static constexpr float colors[5][3] = {
        {1.00f, 0.38f, 0.04f}, {1.00f, 0.06f, 0.04f}, {0.72f, 0.18f, 1.00f}, {0.04f, 0.78f, 1.00f}, {1.00f, 0.86f, 0.06f},
    };
    const int index = std::clamp(palette, 0, 4);
    out[0] = colors[index][0];
    out[1] = colors[index][1];
    out[2] = colors[index][2];
}

}  // namespace wxl::readability
