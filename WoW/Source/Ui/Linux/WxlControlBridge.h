#ifndef WOW_SOURCE_UI_LINUX_WXLCONTROLBRIDGE_H
#define WOW_SOURCE_UI_LINUX_WXLCONTROLBRIDGE_H

// Linux-only port of WarcraftXL's wxl-control-panel bridge (see
// Server/From_The_Ashes_Source/WarcraftXL/scripts/wxl-control-panel in the
// FTA-Project tree). The real WarcraftXL.dll/d3d9-proxy pair that originally
// answered these calls isn't part of the available source (only the Lua
// bridge and the options-storage ABI are). The "chevron" facing-marker
// renderer those options describe is wxl-unit-outline, ported natively in
// WoW/Source/WorldClient/Linux/WxlUnitOutline; it reads them back through
// WxlGetControlSettings and only draws when WXL_UNIT_OVERLAYS is on. This bridge exists so
// WXL.DetectBridge() in the installed WarcraftXLControl addon reports
// available=true at all, which unblocks every OTHER extension's own
// NativeFunction gate (skybox, weather, visual-time, etc.) -- those are
// independently implemented elsewhere and don't depend on the chevron
// renderer existing.
//
// Entirely guarded behind WOWDECOMP_LINUX; no effect on a non-Linux build of
// the same shared source.

#if defined(WOWDECOMP_LINUX)

#include <cstddef>

void WxlRegisterControlBridgeLua();
// Snapshot of the stored outline/facing options in the supplied 200-byte layout; false for a wrong size.
bool WxlGetControlSettings(void* out, size_t size);

#endif  // WOWDECOMP_LINUX

#endif
