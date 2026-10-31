#ifndef WOW_SOURCE_OBJECT_LINUX_WXLGOBLINGLIDER_H
#define WOW_SOURCE_OBJECT_LINUX_WXLGOBLINGLIDER_H

// Native port of WarcraftXL's wxl-goblin-glider: turning an existing airborne knockback trajectory
// with the character's own facing while the server grants it (aura 126389), plus the opening
// animation of the private glider attachment. The server owns the spell, impulse, duration,
// cooldown and validation; the client keeps native gravity, collision, landing and movement packets.
// The Windows module detours the movement step (0x6EAC40) and the fall-distance solver (0x986F00)
// and shares a Lua grant handshake ("WXLGLIDE" self-whispers). Here:
//   CMovement_C::ApplyMovement   -> WxlGliderStepScope around the stock step
//   CMovementShared::CalcFallDistance -> WxlGliderTerminalSink()
//   CM2Model::Animate / AttachToParent -> NativeVisualHooks glider callbacks

#if defined(WOWDECOMP_LINUX)

#include <cstdint>

class CMovement_C;

void WxlRegisterGoblinGliderLua();
void WxlGoblinGliderWorldReset();

// The terminal fall speed override for the current step, or 0 (no override).
float WxlGliderTerminalSink();

struct WxlGliderStepScope {
    WxlGliderStepScope(CMovement_C* movement, uint32_t now, uint32_t delta);
    ~WxlGliderStepScope();
    WxlGliderStepScope(const WxlGliderStepScope&) = delete;
    WxlGliderStepScope& operator=(const WxlGliderStepScope&) = delete;

private:
    CMovement_C* m_movement;
    float m_previousSink;
    uint64_t m_guid = 0;
    bool m_engaged = false;
    bool m_local = false;
};

#endif  // WOWDECOMP_LINUX

#endif
