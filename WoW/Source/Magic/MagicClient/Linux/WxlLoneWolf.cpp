// Native port of the supplied wxl-lone-wolf (GPL-3.0-or-later, Copyright (C) 2026 WarcraftXL); see
// WxlLoneWolf.h. The accepted-shot handoff follows scripts/wxl-lone-wolf/src/LoneWolfHandoff.cpp: an
// intent is recorded only after the local hunter's plain CMSG_CAST_SPELL has really been sent, and
// consumed by the matching SMSG_SPELL_GO, whose normal completion (not an early transport finish,
// hidden cast or release) selects Auto Shot and re-enters combat. The Windows module's register-ABI
// gateways, byte pins and SEH guards are not needed with the reconstructed types.
#include "WoW/Source/Magic/MagicClient/Linux/WxlLoneWolf.h"

#if defined(WOWDECOMP_LINUX)

#include <cstdint>

#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/ModernAssets/NativeDiagnostic.h"
#include "Engine/Source/ModernAssets/WxlLoneWolf/LoneWolfIntent.hpp"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "WoW/Source/DB/DBClient/DBClient.h"
#include "WoW/Source/DB/Rec/SpellRec.h"
#include "WoW/Source/Magic/MagicClient/Spell_C.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "WoW/Source/Object/ObjectClient/Unit_C.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/Ui/GameUI.h"
#include "WoW/Source/Ui/Linux/WxlGameScripts.h"
#include "WoW/Source/Ui/SpellBookFrame.h"
#include "WoW/Source/WorldClient/Linux/WxlVisualEnvironment.h"

namespace {

namespace intent = wxl::lonewolf::intent;

constexpr uint32_t kHunterClass = 3;
constexpr int32_t kHunterSpellFamily = 9;
constexpr int32_t kRangedDamageClass = 3;
constexpr uint32_t kAutoShot = 75;
constexpr int32_t kLoneWolfAura = 601666;

bool g_enabled = false;
uint64_t g_activeHunterGuid = 0;
intent::State g_state;
bool g_selectBypass = false;

// Lua, spell casting and the handoff all run on the client's main thread.
CGUnit_C* PlayerByGuid(uint64_t guid) {
    return guid ? static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(WOWGUID{guid}, TYPE_PLAYER, __FILE__, __LINE__)) : nullptr;
}

uint64_t ActiveGuid() { return ClntObjMgrGetActivePlayer().m_guid; }

bool IsHunterShot(const SpellRec* spell) {
    return spell && spell->m_spellClassSet == kHunterSpellFamily && spell->m_defenseType == kRangedDamageClass;
}

CGUnit_C* ActiveLoneWolfHunter() {
    if (!g_enabled || !g_activeHunterGuid || ActiveGuid() != g_activeHunterGuid) return nullptr;
    return PlayerByGuid(g_activeHunterGuid);
}

bool LiveAura(CGUnit_C* player) {
    const uint32_t count = player->GetAuraCount();
    if (count > 256) return false;
    for (uint32_t index = 0; index < count; ++index) {
        const AuraData* aura = player->GetAura(index);
        if (aura && aura->m_spellId == kLoneWolfAura && (aura->m_flags & 7) && !(aura->m_flags & 0x80)) return true;
    }
    return false;
}

// The same checks the native client makes before it would restore Auto Shot itself.
bool Eligible(const intent::Request& request, uint64_t target, CGUnit_C*& actor) {
    actor = ActiveLoneWolfHunter();
    if (!actor || !request.count || request.spell == kAutoShot || !target || request.caster != ActiveGuid() ||
        target != CGGameUI::m_lockedTarget.m_guid || PlayerByGuid(request.caster) != actor || !LiveAura(actor)) {
        return false;
    }
    SpellRec spell;
    if (request.spell > INT32_MAX || !g_spellDB.GetRecord(static_cast<int32_t>(request.spell), &spell) || !IsHunterShot(&spell) ||
        (spell.m_attributes & 0x20)) {
        return false;
    }
    const int32_t repeat = Spell_C_GetAutoRepeatingSpell();
    if (!s_cvAutoRangedCombat || !s_cvAutoRangedCombat->m_intValue || CGSpellBook::m_autoRangedSpell != static_cast<int32_t>(kAutoShot) ||
        (repeat && repeat != static_cast<int32_t>(kAutoShot)) || !CGGameUI::m_hasControl) {
        return false;
    }
    CGPlayer_C* player = static_cast<CGPlayer_C*>(actor);
    if (!actor->m_unit || !player->m_player || actor->m_unit->m_unitFieldCharmedby.m_guid || (actor->m_unit->m_unitFieldFlags & 0x20000) ||
        (player->m_player->m_playerFlags & 0x1000000)) {
        return false;
    }
    const CGUnit_C* victim = static_cast<const CGUnit_C*>(ClntObjMgrObjectPtr(WOWGUID{target}, TYPE_UNIT, __FILE__, __LINE__));
    return victim && CGGameUI::CanPerformAction(1) && actor->CanAttackNow(victim);
}

void Accept(const intent::Stamp& stamp, uint64_t target) {
    CGUnit_C* actor = nullptr;
    if (!Eligible(stamp.request, target, actor) || !g_state.Current(stamp)) return;
    // Own the selection before choosing Auto Shot, so a START_AUTOREPEAT_SPELL script that cancels or
    // selects something newer through the public entry is not overwritten.
    const uint64_t ownership = g_state.Select();
    g_selectBypass = true;
    Spell_C_SetAutoRangedCombatSpell(static_cast<int32_t>(kAutoShot));
    g_selectBypass = false;
    if (g_state.CurrentSelection(ownership) && Eligible(stamp.request, target, actor)) {
        static_cast<CGPlayer_C*>(actor)->CombatModeEnter(WOWGUID{target}, 0, 1);
        return;
    }
    if (g_state.ReleaseSelection(ownership)) {
        g_selectBypass = true;
        Spell_C_SetAutoRangedCombatSpell(0);
        g_selectBypass = false;
    }
}

struct Snapshot {
    intent::Request request;
    uint32_t flags = 0;
    bool item = false;
};

Snapshot SnapshotCast(const PendingSpellCast* cast) {
    Snapshot snapshot;
    snapshot.request.cast = reinterpret_cast<std::uintptr_t>(cast);
    snapshot.request.caster = cast->m_data.m_caster.m_guid;
    snapshot.request.spell = cast->m_data.m_spellId;
    snapshot.request.count = cast->m_data.m_castCount;
    snapshot.request.target = cast->m_data.m_objectTarget.m_guid;
    snapshot.flags = cast->m_data.m_castFlags;
    snapshot.item = cast->m_data.m_castItem.m_guid && cast->m_data.m_castItem.m_guid != snapshot.request.caster;
    return snapshot;
}

WxlLoneWolfSendScope* g_sending = nullptr;

}  // namespace

struct WxlLoneWolfGoScope::Impl {
    Impl* previous = nullptr;
    PendingSpellCast* cast = nullptr;
    intent::Completion completion;
    uint64_t target = 0;
};

namespace {
WxlLoneWolfGoScope::Impl* g_completing = nullptr;
}

// ---------------------------------------------------------------------------------------------
// Range
// ---------------------------------------------------------------------------------------------

void WxlLoneWolfAdjustMinimum(const CGUnit_C* caster, const SpellRec* spell, float* minimum) {
    if (caster && minimum && IsHunterShot(spell) && caster == ActiveLoneWolfHunter()) *minimum = 0.0f;
}

void WxlLoneWolfAdjustDisplayMinimum(const SpellRec* spell, float* minimum) {
    if (minimum && IsHunterShot(spell) && ActiveLoneWolfHunter()) *minimum = 0.0f;
}

bool WxlLoneWolfSuppressMeleeSwitch(const CGUnit_C* caster, const CGUnit_C* target) {
    // Prevents the separate close-range switch from Auto Shot to melee; otherwise the native
    // behaviour is untouched.
    return g_enabled && caster && target && Spell_C_GetAutoRangedCombatSpell() == static_cast<int32_t>(kAutoShot) &&
           caster == ActiveLoneWolfHunter();
}

// ---------------------------------------------------------------------------------------------
// Handoff observers
// ---------------------------------------------------------------------------------------------

WxlLoneWolfSendScope::WxlLoneWolfSendScope(PendingSpellCast* cast) : m_previous(g_sending), m_cast(cast) {
    if (!g_enabled) return;
    const Snapshot snapshot = SnapshotCast(cast);
    // A newer local ability, including melee, supersedes an older shot.
    if (snapshot.request.count && snapshot.request.caster == ActiveGuid()) g_state.Invalidate();
    g_sending = this;
}

WxlLoneWolfSendScope::~WxlLoneWolfSendScope() {
    if (g_sending == this) g_sending = m_previous;
}

WxlLoneWolfCastSend WxlLoneWolfBeforeCastSend(PendingSpellCast* cast, bool plainCastPacket) {
    WxlLoneWolfCastSend send;
    if (!g_enabled || !plainCastPacket || !g_sending || g_sending->m_cast != cast || g_sending->m_released) return send;
    const Snapshot snapshot = SnapshotCast(cast);
    CGUnit_C* actor = nullptr;
    if (snapshot.item || !Eligible(snapshot.request, snapshot.request.target, actor)) return send;
    send.candidate = true;
    send.revision = g_state.Revision();
    send.cast = cast;
    send.castId = snapshot.request.cast;
    send.caster = snapshot.request.caster;
    send.target = snapshot.request.target;
    send.spell = snapshot.request.spell;
    send.count = snapshot.request.count;
    return send;
}

void WxlLoneWolfAfterCastSend(const WxlLoneWolfCastSend& send) {
    if (!send.candidate || !g_sending || g_sending->m_cast != send.cast || g_sending->m_released) return;
    // The real packet has been sent; the cast's storage is not read after this boundary.
    if (g_state.Revision() != send.revision) return;
    intent::Request request;
    request.cast = send.castId;
    request.caster = send.caster;
    request.target = send.target;
    request.spell = send.spell;
    request.count = send.count;
    g_state.Sent(request);
}

WxlLoneWolfGoScope::WxlLoneWolfGoScope(PendingSpellCast* cast) : m_impl(nullptr) {
    if (!g_enabled) return;
    const Snapshot snapshot = SnapshotCast(cast);
    m_impl = new Impl();
    m_impl->previous = g_completing;
    m_impl->cast = cast;
    const intent::Stamp stamp = g_state.Consume(snapshot.request);
    m_impl->completion = intent::Completion(stamp, snapshot.flags, true, snapshot.request.target);
    m_impl->target = snapshot.request.target;
    g_completing = m_impl;
}

WxlLoneWolfGoScope::~WxlLoneWolfGoScope() {
    if (!m_impl) return;
    g_completing = m_impl->previous;
    const bool accepted = m_impl->completion.Accepted(g_state);
    const intent::Stamp stamp = m_impl->completion.stamp;
    const uint64_t target = m_impl->target;
    delete m_impl;
    // No native cast pointer is read here: the cast has been finished and may be reused.
    if (accepted) Accept(stamp, target);
}

void WxlLoneWolfObserveFinish(PendingSpellCast* cast, bool normal, std::uint64_t target) {
    for (WxlLoneWolfGoScope::Impl* frame = g_completing; frame; frame = frame->previous) {
        if (frame->cast != cast) continue;
        frame->completion.Finish(normal);
        if (normal) frame->target = target;
    }
}

void WxlLoneWolfObserveRelease(PendingSpellCast* cast) {
    if (!g_enabled) return;
    for (WxlLoneWolfSendScope* frame = g_sending; frame; frame = frame->m_previous) {
        if (frame->m_cast == cast) frame->m_released = true;
    }
    for (WxlLoneWolfGoScope::Impl* frame = g_completing; frame; frame = frame->previous) {
        if (frame->cast == cast) frame->completion.Released();
    }
    g_state.Release(reinterpret_cast<std::uintptr_t>(cast));
}

void WxlLoneWolfObserveSelect() {
    // Even a repeated zero is an explicit cancellation/selection boundary.
    if (g_enabled && !g_selectBypass) g_state.Select();
}

void WxlLoneWolfObserveRepeat() {
    // Do not let a repeated native stop revive an older shot.
    if (g_enabled && !g_selectBypass) g_state.Invalidate();
}

// ---------------------------------------------------------------------------------------------
// Lua
// ---------------------------------------------------------------------------------------------

namespace {

// Runs in each in-world Lua state. UNIT_AURA updates immediately; the slow fallback covers initial
// login, reload and event ordering. Only an observed aura enables the client exception.
constexpr char kAuraWatcher[] = R"lua(
WarcraftXL_UpdateLoneWolfRange(0)
if type(CreateFrame) ~= "function" or type(UnitBuff) ~= "function" then return end
local frame = CreateFrame("Frame")
local inWorld = false
local elapsed = 0
local function Refresh()
    local active = 0
    if inWorld then
        local _, class = UnitClass("player")
        if class == "HUNTER" then
            for index = 1, 40 do
                local name, _, _, _, _, _, _, _, _, _, spellId = UnitBuff("player", index)
                if not name then break end
                if spellId == 601666 then active = 1; break end
            end
        end
    end
    WarcraftXL_UpdateLoneWolfRange(active)
end
frame:RegisterEvent("UNIT_AURA")
frame:RegisterEvent("PLAYER_ENTERING_WORLD")
frame:RegisterEvent("PLAYER_LEAVING_WORLD")
frame:RegisterEvent("PLAYER_LOGOUT")
frame:SetScript("OnEvent", function(self, event, unit)
    if event == "PLAYER_ENTERING_WORLD" then
        inWorld = true
    elseif event == "PLAYER_LEAVING_WORLD" or event == "PLAYER_LOGOUT" then
        inWorld = false
    elseif unit ~= "player" then
        return
    end
    Refresh()
end)
frame:SetScript("OnUpdate", function(self, delta)
    elapsed = elapsed + delta
    if elapsed < 0.25 then return end
    elapsed = 0
    Refresh()
end)
-- Registration may occur after PLAYER_ENTERING_WORLD on injection or /reload.
inWorld = type(IsLoggedIn) == "function" and IsLoggedIn() or false
Refresh()
)lua";

int UpdateAuraState(lua_State* L) {
    uint64_t guid = 0;
    if (g_enabled && lua_isnumber(L, 1) && lua_tonumber(L, 1) == 1.0) {
        const uint64_t playerGuid = ActiveGuid();
        const CGUnit_C* player = PlayerByGuid(playerGuid);
        if (player && player->m_unit && player->m_unit->m_unitFieldBytes0[1] == kHunterClass) guid = playerGuid;
    }
    g_activeHunterGuid = guid;
    g_state.Authority(guid);
    return 0;
}

}  // namespace

void WxlRegisterLoneWolfLua() {
    g_enabled = WxlProfileEnabled("WXL_LONE_WOLF", true);
    if (!g_enabled) return;
    FrameScript_RegisterFunction("WarcraftXL_UpdateLoneWolfRange", &UpdateAuraState);
    WxlRegisterGameScript("WarcraftXL_LoneWolfRange", kAuraWatcher);
    WxlNativeDiagnostic("lone-wolf: range hooks and handoff ready; waiting for hunter aura %d", kLoneWolfAura);
}

void WxlLoneWolfWorldLeave() {
    g_activeHunterGuid = 0;
    g_state.Reset();
}

void WxlLoneWolfTargetChanged() {
    if (g_enabled) g_state.Invalidate();
}

#endif  // WOWDECOMP_LINUX
