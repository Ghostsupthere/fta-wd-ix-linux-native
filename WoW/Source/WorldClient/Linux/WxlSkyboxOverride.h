#ifndef WOW_SOURCE_WORLDCLIENT_LINUX_WXLSKYBOXOVERRIDE_H
#define WOW_SOURCE_WORLDCLIENT_LINUX_WXLSKYBOXOVERRIDE_H

// Linux-only port of WarcraftXL's wxl-skybox-control extension (see
// Server/From_The_Ashes_Source/WarcraftXL/scripts/wxl-skybox-control in the
// FTA-Project tree). The original hooks the client's RebuildSkyboxes via
// MinHook against a specific retail Wow.exe's memory offsets, which has no
// meaning against this recompiled Linux binary. This reimplements the same
// Lua-facing behavior (WarcraftXL_SetSkyboxOverride/GetSkyboxOverride) as a
// direct call from DayNight::SetColors(), guarded entirely behind
// WOWDECOMP_LINUX so the shared/decompiled source stays unaffected otherwise.

#if defined(WOWDECOMP_LINUX)

namespace DayNight {
struct DNInfo;
}

// Called once per DayNight::SetColors() pass, after the natural skybox blend
// is computed. If a Lua-requested override is active, forces it as the
// opaque primary sky layer and clears the natural blend layers -- matching
// wxl-skybox-control's ApplyForcedSkybox()/setSlot(1/2, nullptr, ...).
void WxlApplySkyboxOverride(DayNight::DNInfo& info);

// Registers WarcraftXL_SetSkyboxOverride/WarcraftXL_GetSkyboxOverride as Lua
// globals, matching the existing WarcraftXLControl addon's NativeFunction
// probes (Core.lua) so it works unmodified.
void WxlRegisterSkyboxLua();

#endif  // WOWDECOMP_LINUX

#endif
