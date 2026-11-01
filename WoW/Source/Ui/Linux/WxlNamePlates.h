#ifndef WOW_SOURCE_UI_LINUX_WXLNAMEPLATES_H
#define WOW_SOURCE_UI_LINUX_WXLNAMEPLATES_H

// The AwesomeWotLK-style nameplate API (https://github.com/FrostAtom/awesome_wotlk, GPL-3.0), written natively
// against the client's own nameplate frames. Addons such as DBM, Questie, Immersion and ElvUI use it when it
// exists.
//
// Every nameplate frame the client creates gets a token "nameplate1" .. "nameplate64" that stays with that
// frame. While the frame shows a unit, the token works anywhere a unit token does (UnitName("nameplate1"), ...).
//   C_NamePlate.GetNamePlates()                 list of the plate frames currently shown
//   C_NamePlate.GetNamePlateForUnit(unit)       the frame showing that unit token ("target", "nameplate3", ...)
//   C_NamePlate.GetNamePlateByGUID(guid)        the frame for a "0x..." guid string or a unit token
//   C_NamePlate.GetNamePlateTokenByGUID(guid)   "nameplateN" or nothing
// Events: NAME_PLATE_CREATED(frame), NAME_PLATE_UNIT_ADDED(token), NAME_PLATE_UNIT_REMOVED(token),
// NAME_PLATE_OWNER_CHANGED(token). CVar: nameplateDistance (yards, default 41).
// The stacking CVars of AwesomeWotLK (nameplateStacking and its spacing settings) are not implemented.

#if defined(WOWDECOMP_LINUX)

#include <cstdint>
#include "WoW/Source/Object/WGUID.h"

class CGNamePlateFrame;

// The script event names the client registers with the stock ones; returns how many entries `out` holds.
// Call it instead of FrameScript_CreateEvents(g_scriptEvents, NUM_SCRIPT_EVENTS).
void WxlCreateScriptEvents();

void WxlRegisterNamePlatesLua();
void WxlNamePlatesRegisterCVars();

// Engine hooks.
void WxlNamePlateShown(CGNamePlateFrame* frame);   // a unit was assigned to the frame and it was shown
void WxlNamePlateHidden(CGNamePlateFrame* frame);  // the frame is about to be hidden and recycled
void WxlNamePlateFrameDestroyed(CGNamePlateFrame* frame);
void WxlNamePlatesReset();                          // world teardown

// "nameplate7" -> the guid of the unit shown on that plate; false when the token is not a live plate.
bool WxlNamePlateGuidFromIndex(uint32_t oneBasedIndex, WOWGUID& guid);

// Squared distance, in yards, beyond which the client hides a plate (nameplateDistance).
float WxlNamePlateDistanceSquared();

#endif  // WOWDECOMP_LINUX

#endif
