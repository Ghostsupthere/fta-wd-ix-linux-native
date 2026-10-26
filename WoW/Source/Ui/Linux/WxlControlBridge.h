#ifndef WOW_SOURCE_UI_LINUX_WXLCONTROLBRIDGE_H
#define WOW_SOURCE_UI_LINUX_WXLCONTROLBRIDGE_H

// Linux-only port of WarcraftXL's wxl-control-panel bridge (see
// Server/From_The_Ashes_Source/WarcraftXL/scripts/wxl-control-panel in the
// FTA-Project tree). The real WarcraftXL.dll/d3d9-proxy pair that originally
// answered these calls isn't part of the available source (only the Lua
// bridge and the options-storage ABI are) -- in particular, the "chevron"
// facing-marker renderer that the stored options describe has no known
// implementation anywhere in the checked-out WarcraftXL source tree, so
// those specific options (outline/facing/normal/elite/boss/tail.*) round-trip
// correctly but currently drive no renderer. This bridge exists so
// WXL.DetectBridge() in the installed WarcraftXLControl addon reports
// available=true at all, which unblocks every OTHER extension's own
// NativeFunction gate (skybox, weather, visual-time, etc.) -- those are
// independently implemented elsewhere and don't depend on the chevron
// renderer existing.
//
// Entirely guarded behind WOWDECOMP_LINUX; no effect on a non-Linux build of
// the same shared source.

#if defined(WOWDECOMP_LINUX)

void WxlRegisterControlBridgeLua();

#endif  // WOWDECOMP_LINUX

#endif
