#ifndef WOW_SOURCE_GAME_LINUX_WXLVISUALTIMEOVERRIDE_H
#define WOW_SOURCE_GAME_LINUX_WXLVISUALTIMEOVERRIDE_H

// Linux-only port of WarcraftXL's wxl-visual-time extension (see
// Server/From_The_Ashes_Source/WarcraftXL/scripts/wxl-visual-time in the
// FTA-Project tree). The original re-asserts a forced hour/minute every
// frame via wxl::game::world::SetVisualTime(), calling the client's own
// "canonical" time setter (forceCallbacks=true) so sky lighting and timed
// world effects update, not just the raw clock value -- this client already
// has that exact setter (CGameTime::GameTimeSetTime(..., true), already used
// by its own SMSG_GAME_TIME_SET handler) and a per-frame tick handler
// (ClientGameTimeTickHandler) to hang the re-assertion on, so no new timer
// mechanism is needed, just a state holder Lua can set/clear and a query the
// tick handler polls.
//
// Entirely guarded behind WOWDECOMP_LINUX; no effect on a non-Linux build of
// the same shared source.

#if defined(WOWDECOMP_LINUX)

#include <cstdint>

// Polled once per frame from ClientGameTimeTickHandler (ClientGameTime.cpp).
// Returns true and fills *hour/*minute if an override is active.
bool WxlGetVisualTimeOverride(int32_t* hour, int32_t* minute);

void WxlRegisterVisualTimeLua();

#endif  // WOWDECOMP_LINUX

#endif
