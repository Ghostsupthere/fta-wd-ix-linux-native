#ifndef WOW_SOURCE_UI_LINUX_WXLCLICKTHROUGH_H
#define WOW_SOURCE_UI_LINUX_WXLCLICKTHROUGH_H

// Click-through for chosen units (NPCBotClickGuard). A world click on a unit in the list is ignored
// by the click hit test, so the click lands on whatever is behind it (ground, another unit), unless
// the chosen modifier key is held. Only the click path is affected: mouseover, tooltips, the unit
// frames, nameplates and spell targeting by other means are untouched.
//
// Lua: WXL_SetClickThrough(button, modifier, unit1, unit2, ...)
//   button   "left", "right" or "both"
//   modifier "alt", "shift" or "ctrl": holding it (at the moment of the mouse-down) makes the click
//            behave normally
//   unitN    unit tokens ("party1", "raid3", "target", ...); they are resolved to GUIDs now.
// Replaces the whole list; with no units it clears it. Returns the number of units stored.

#include "WoW/Source/Object/WGUID.h"

#if defined(WOWDECOMP_LINUX)

void WxlRegisterClickThroughLua();

// SetupDefaultAction brackets the click hit test with these. button: 1 left (select), 2 right (action).
void WxlClickThroughBegin(int button);
void WxlClickThroughEnd();

// True when `guid` must be skipped by the click hit test that is running right now.
bool WxlClickThroughSkips(WOWGUID guid);

#endif  // WOWDECOMP_LINUX

#endif
