#ifndef WOW_SOURCE_MAGIC_MAGICCLIENT_LINUX_WXLLONEWOLF_H
#define WOW_SOURCE_MAGIC_MAGICCLIENT_LINUX_WXLLONEWOLF_H

// Native port of WarcraftXL's wxl-lone-wolf: while a hunter has aura 601666, ranged hunter spells
// (family 9, ranged damage class) get a zero minimum range, Auto Shot no longer switches to melee at
// point-blank range, and an accepted ranged shot hands the player back to Auto Shot. The Windows
// module detours seven register-ABI functions of the original executable; here the reconstructed
// functions call these helpers directly:
//   Spell_C_GetMinMaxRange (0x7FF480)        -> WxlLoneWolfAdjustMinimum
//   SpellRec::GetMinRange (0x7FF400)         -> WxlLoneWolfAdjustDisplayMinimum
//   CGUnit_C::IsInMeleeRange (0x71B820)      -> WxlLoneWolfSuppressMeleeSwitch
//   Spell_C::SendPendingCast (0x80AC90)      -> WxlLoneWolfSendScope + Before/AfterCastSend
//   PendingSpellCast::OnSpellGo (0x80E1B0)   -> WxlLoneWolfGoScope, ObserveFinish
//   ReleasePendingSpellCast (0x805100)       -> WxlLoneWolfObserveRelease
//   Spell_C_SetAutoRangedCombatSpell (0x7FE140) / SetAutoRepeatingSpell (0x800A00)
//                                            -> WxlLoneWolfObserveSelect / ObserveRepeat
// The server independently enforces eligibility on every cast.

#if defined(WOWDECOMP_LINUX)

#include <cstdint>

class CGUnit_C;
class PendingSpellCast;
class SpellRec;

void WxlRegisterLoneWolfLua();
void WxlLoneWolfWorldLeave();
void WxlLoneWolfTargetChanged();

void WxlLoneWolfAdjustMinimum(const CGUnit_C* caster, const SpellRec* spell, float* minimum);
void WxlLoneWolfAdjustDisplayMinimum(const SpellRec* spell, float* minimum);
bool WxlLoneWolfSuppressMeleeSwitch(const CGUnit_C* caster, const CGUnit_C* target);

// Handoff of an accepted ranged shot back to Auto Shot.
struct WxlLoneWolfSendScope {
    explicit WxlLoneWolfSendScope(PendingSpellCast* cast);
    ~WxlLoneWolfSendScope();
    WxlLoneWolfSendScope(const WxlLoneWolfSendScope&) = delete;
    WxlLoneWolfSendScope& operator=(const WxlLoneWolfSendScope&) = delete;
    WxlLoneWolfSendScope* m_previous;
    PendingSpellCast* m_cast;
    bool m_released = false;
};

struct WxlLoneWolfCastSend {
    bool candidate = false;
    std::uint64_t revision = 0;
    PendingSpellCast* cast = nullptr;
    std::uintptr_t castId = 0;
    std::uint64_t caster = 0, target = 0;
    std::uint32_t spell = 0;
    std::uint8_t count = 0;
};

// Around the CMSG_CAST_SPELL send; for any other packet kind the result is simply not a candidate.
WxlLoneWolfCastSend WxlLoneWolfBeforeCastSend(PendingSpellCast* cast, bool plainCastPacket);
void WxlLoneWolfAfterCastSend(const WxlLoneWolfCastSend& send);

struct WxlLoneWolfGoScope {
    explicit WxlLoneWolfGoScope(PendingSpellCast* cast);
    ~WxlLoneWolfGoScope();
    WxlLoneWolfGoScope(const WxlLoneWolfGoScope&) = delete;
    WxlLoneWolfGoScope& operator=(const WxlLoneWolfGoScope&) = delete;
    struct Impl;
    Impl* m_impl;
};

// Called immediately before FinishPendingSpellCast in OnSpellGo: `normal` for the ordinary tail,
// false for the early invalid-transport one. `target` is the authoritative object target.
void WxlLoneWolfObserveFinish(PendingSpellCast* cast, bool normal, std::uint64_t target);
void WxlLoneWolfObserveRelease(PendingSpellCast* cast);
void WxlLoneWolfObserveSelect();
void WxlLoneWolfObserveRepeat();

#endif  // WOWDECOMP_LINUX

#endif
