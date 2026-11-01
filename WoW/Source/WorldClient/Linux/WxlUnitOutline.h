#ifndef WOW_SOURCE_WORLDCLIENT_LINUX_WXLUNITOUTLINE_H
#define WOW_SOURCE_WORLDCLIENT_LINUX_WXLUNITOUTLINE_H

// Native port of the supplied wxl-unit-outline (reaction-coloured silhouette outline, facing chevrons and the
// frontal-cone/ring danger guide on the target and mouseover units). Off unless WXL_UNIT_OVERLAYS is on in
// WarcraftXL.cfg (the checked profile has it at 0). The facing and outline options come from the control
// bridge and the combat-readability bridge.

// Installs the hooks once startup has finished; does nothing when the profile switch is off.
void WxlUnitOutlineInstall();
// Leaving a world drops the masks and target list.
void WxlUnitOutlineWorldLeave();

#endif
