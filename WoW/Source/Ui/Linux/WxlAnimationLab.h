#ifndef WOW_SOURCE_UI_LINUX_WXLANIMATIONLAB_H
#define WOW_SOURCE_UI_LINUX_WXLANIMATIONLAB_H

// Native port of WarcraftXL's wxl-character-animation-lab: the WarcraftXL_AnimationLab* Lua API used
// by the WarcraftXLControl add-on's Character Animation Studio (/wxlanim), plus the Levitate /
// Wraith Walk hover and shoulder-hold presentation. The Windows module subscribes to two core
// events (an M2 bone-sequence event carrying the outer unit request, and a unit-animation
// completion event) and detours the attack validator. Here CGUnit_C::PlayBaseAnimation owns a
// WxlUnitAnimationScope, CM2Model::SetBoneSequence calls NativeVisualHooks::m2SetBoneSequence, and
// CGUnit_C::VerifyAttackIconPressed calls WxlAnimationLabBeforeAttackCheck.

#if defined(WOWDECOMP_LINUX)

#include <cstdint>

class CGUnit_C;

void WxlRegisterAnimationLabLua();
void WxlAnimationLabFrame();
void WxlAnimationLabWorldLeave();

// Cancels a local Wraith Walk channel before the native attack checks run.
void WxlAnimationLabBeforeAttackCheck(CGUnit_C* owner);

// One outer CGUnit_C::PlayBaseAnimation request. Nested requests get distinct tokens and complete
// inside-out. Main thread only.
struct WxlUnitAnimationScope {
    WxlUnitAnimationScope(CGUnit_C* unit, uint32_t animationId, int32_t flags);
    ~WxlUnitAnimationScope();
    WxlUnitAnimationScope(const WxlUnitAnimationScope&) = delete;
    WxlUnitAnimationScope& operator=(const WxlUnitAnimationScope&) = delete;

private:
    bool m_pushed;
};

#endif  // WOWDECOMP_LINUX

#endif
