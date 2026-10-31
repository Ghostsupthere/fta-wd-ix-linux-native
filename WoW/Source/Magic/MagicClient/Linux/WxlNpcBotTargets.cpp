// Native port of the supplied wxl-npcbot-targets (GPL-3.0-or-later, Copyright (C) 2026 WarcraftXL);
// see WxlNpcBotTargets.h. Unlike the Windows module this needs no instruction-signature checks or
// SEH: the reconstructed types are used directly.
#include "WoW/Source/Magic/MagicClient/Linux/WxlNpcBotTargets.h"

#if defined(WOWDECOMP_LINUX)

#include "Engine/Source/ModernAssets/WxlNpcBot/NpcBotTargetRules.hpp"
#include "WoW/Source/DB/DBClient/DBClient.h"
#include "WoW/Source/DB/Rec/SpellRec.h"
#include "WoW/Source/Magic/MagicClient/Spell_C.h"
#include "WoW/Source/Object/ObjectClient/Unit_C.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/Ui/GameUI.h"
#include "WoW/Source/WorldClient/Linux/WxlVisualEnvironment.h"

namespace {

namespace rules = wxl::npcbot;

bool Enabled(const char* key) {
    static const bool targets = WxlProfileEnabled("WXL_NPCBOT_TARGETS", true);
    static const bool resurrection = WxlProfileEnabled("WXL_NPCBOT_RESURRECTION", true);
    return key[0] == 't' ? targets : resurrection;
}

bool IsLocalPlayer(const CGUnit_C* unit, uint64_t local) {
    return unit && unit->m_obj && (unit->m_obj->m_type & TYPE_PLAYER) && unit->m_obj->m_guid.m_guid == local;
}

// A resurrection of a dead bot in the local player's roster. The roster test is the raid one, which
// also covers the party.
bool DeadGroupedBot(uint32_t spell, const CGUnit_C* caster, const CGUnit_C* target) {
    if (!rules::IsResurrectionSpell(spell) || !caster || !target) return false;
    if (!WxlNpcBotGroupedMember(caster, target, true)) return false;
    return target->m_unit && target->m_unit->m_unitFieldHealth <= 0;
}

const CGUnit_C* UnitByGuid(const WOWGUID& guid) {
    return static_cast<const CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, __FILE__, __LINE__));
}

}  // namespace

bool WxlNpcBotGroupedMember(const CGUnit_C* self, const CGUnit_C* other, bool raid) {
    if (!Enabled("targets") || !self || !other || !self->m_obj || !other->m_obj) return false;
    const uint64_t local = ClntObjMgrGetActivePlayer().m_guid;
    if (!local) return false;

    const CGUnit_C* member = nullptr;
    if (IsLocalPlayer(self, local)) member = other;
    else if (IsLocalPlayer(other, local)) member = self;
    if (!member) return false;

    const uint32_t type = member->m_obj->m_type;
    if (!(type & TYPE_UNIT) || (type & TYPE_PLAYER)) return false;
    const WOWGUID guid = member->m_obj->m_guid;
    if (!rules::IsCreatureGuid(guid.m_guid)) return false;
    return (raid ? CGGameUI::IsPartyMember(guid) : CGGameUI::IsInParty(guid)) != 0;
}

const SpellRec* WxlNpcBotResurrectionRangeView(CGUnit_C* caster, const SpellRec* spell, WOWGUID target, SpellRec& storage) {
    if (!Enabled("resurrection") || !spell || !(static_cast<uint32_t>(spell->m_targets) & rules::kFriendlyCorpse)) return spell;
    const uint32_t id = static_cast<uint32_t>(spell->m_id);
    if (!rules::IsResurrectionSpell(id)) return spell;
    if (!target) target = CGGameUI::m_lockedTarget;
    if (!DeadGroupedBot(id, caster, UnitByGuid(target))) return spell;
    // A call-local view leaves the shared DBC record untouched; the native validator still checks
    // life state, CanAssist and the real spell range.
    storage = *spell;
    storage.m_targets = static_cast<int32_t>(rules::DeadBotTargetMask(static_cast<uint32_t>(spell->m_targets)));
    return &storage;
}

uint32_t WxlNpcBotResurrectionMaskFor(uint32_t original, const CGObject_C* target, const PendingSpellCast* pending) {
    if (!Enabled("resurrection") || !(original & rules::kFriendlyCorpse) || !target || !pending) return original;
    const uint32_t spell = static_cast<uint32_t>(pending->m_data.m_spellId);
    if (!rules::IsResurrectionSpell(spell) || !target->m_obj || !(target->m_obj->m_type & TYPE_UNIT)) return original;
    if (!DeadGroupedBot(spell, UnitByGuid(pending->m_data.m_caster), static_cast<const CGUnit_C*>(target))) return original;
    return rules::DeadBotTargetMask(original);
}

#endif  // WOWDECOMP_LINUX
