#ifndef WOW_SOURCE_WORLDCLIENT_LINUX_WXLQUESTTRACKER_H
#define WOW_SOURCE_WORLDCLIENT_LINUX_WXLQUESTTRACKER_H

// Native port of the supplied wxl-quest-marker and the objective-ring half of wxl-selcircle-poc.
// Both are off unless WXL_QUEST_RENDERING is on in WarcraftXL.cfg (the checked profile has it at 0).
class CGUnit_C;

// Registers the WarcraftXL_Tracker* Lua natives the WarcraftXLControl Tracker tab calls.
void WxlRegisterQuestTrackerLua();
// Draws the quest billboard, corpse marker and screen-space diamond. Call once per frame inside the
// native world pass, after the loot beams, while the world depth buffer is still bound.
void WxlDrawQuestTracker();
// Leaving a world drops every marker, corpse mark and objective entry.
void WxlQuestTrackerWorldLeave();
// Draws a quest-objective ring under this unit when its creature entry is in the selected quest's set.
// Called from CGUnit_C::Render, which the engine runs once per visible unit each frame.
void WxlObjectiveRingDraw(const CGUnit_C* unit);

#endif
