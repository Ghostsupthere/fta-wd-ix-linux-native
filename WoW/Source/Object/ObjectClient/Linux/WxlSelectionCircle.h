#ifndef WOW_SOURCE_OBJECT_OBJECTCLIENT_LINUX_WXLSELECTIONCIRCLE_H
#define WOW_SOURCE_OBJECT_OBJECTCLIENT_LINUX_WXLSELECTIONCIRCLE_H

// Native port of WarcraftXL's wxl-selection-circle: the target selection circle is oriented along
// the unit's own forward direction instead of toward the camera. The Windows module detours
// CGObject_C::BuildSelectionRotMatrix (0x744150); the reconstructed function is a static with no
// object, so the two unit render paths that use it call WxlSelectionCircleApply right after it.
// Corpse circles and the ground move/attack cursor keep the stock camera-facing matrix.

#if defined(WOWDECOMP_LINUX)

class CGUnit_C;
class C44Matrix;

void WxlSelectionCircleApply(const CGUnit_C* unit, C44Matrix* matrix);

#endif  // WOWDECOMP_LINUX

#endif
