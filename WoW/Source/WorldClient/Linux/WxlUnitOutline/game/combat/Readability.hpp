// Native stand-in for the core's combat-readability header: the settings struct and palette are the
// native port's (Engine/Source/ModernAssets/WxlCombatReadability/ReadabilitySettings.hpp), whose
// layout and defaults match the supplied struct.
#pragma once

#include "Engine/Source/ModernAssets/WxlCombatReadability/ReadabilitySettings.hpp"

namespace wxl::game::combat {
using ReadabilitySettings = wxl::readability::Settings;
constexpr int DangerGround = wxl::readability::DangerGround;
constexpr int DangerOverlay = wxl::readability::DangerOverlay;
constexpr int DangerAllHostile = wxl::readability::DangerAllHostile;
constexpr int DangerEliteAndBoss = wxl::readability::DangerEliteAndBoss;
constexpr int DangerBossOnly = wxl::readability::DangerBossOnly;
ReadabilitySettings Snapshot();
inline void PaletteColor(int palette, float out[3]) { wxl::readability::PaletteColor(palette, out); }
}  // namespace wxl::game::combat
