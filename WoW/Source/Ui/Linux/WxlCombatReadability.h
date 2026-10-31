#ifndef WOW_SOURCE_UI_LINUX_WXLCOMBATREADABILITY_H
#define WOW_SOURCE_UI_LINUX_WXLCOMBATREADABILITY_H

// Native port of WarcraftXL's wxl-combat-readability: the WarcraftXL_ReadabilityAvailable /
// Get / Set / Reset Lua bridge the control add-on uses for its optional frontal guide. It only
// stores the settings; the renderer that reads them (wxl-unit-outline, enabled by
// WXL_UNIT_OVERLAYS) is not part of the native port, so changing them has no visible effect yet.

#if defined(WOWDECOMP_LINUX)

#include "Engine/Source/ModernAssets/WxlCombatReadability/ReadabilitySettings.hpp"

void WxlRegisterCombatReadabilityLua();
wxl::readability::Settings WxlReadabilitySnapshot();

#endif  // WOWDECOMP_LINUX

#endif
