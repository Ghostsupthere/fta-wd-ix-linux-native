// Native port of the supplied wxl-npcbot-enchant client bridge (GPL-3.0-or-later, Copyright (C) 2026
// WarcraftXL); see WxlNpcBotEnchant.h. The Windows module validates instruction bytes and catches
// invalid reads because it addresses raw memory; here the pending cast is the reconstructed
// PendingSpellCast.
#include "WoW/Source/Ui/Linux/WxlNpcBotEnchant.h"

#if defined(WOWDECOMP_LINUX)

#include <cstdint>

#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "WoW/Source/Magic/MagicClient/Spell_C.h"
#include "WoW/Source/WorldClient/Linux/WxlVisualEnvironment.h"

namespace {

// Target-type bits of an item cursor (0x10 item, 0x4000 trade-slot item).
constexpr uint32_t kItemTargetMask = 0x4010;
// High word of an Item GUID (HighGuid::Item = 0x4000).
constexpr uint32_t kItemGuidHigh = 0x40000000;
constexpr int32_t kInterruptedReason = 0x1B;

struct PendingEnchant {
    uint32_t spell = 0;
    uint32_t count = 0;
    uint32_t sourceLow = 0;
    uint32_t sourceHigh = 0;
    uintptr_t token = 0;
};

bool ReadPending(PendingEnchant& out) {
    PendingSpellCast* pending = s_targetingSpell;
    if (!pending || !(s_needTargets & kItemTargetMask)) return false;

    const PendingSpellCastData& data = pending->m_data;
    out.spell = data.m_spellId;
    out.count = data.m_castCount;
    out.token = reinterpret_cast<uintptr_t>(pending);
    // A source equal to the caster is a learned spell; a different source must be an Item GUID,
    // never a pet or vehicle cast.
    if (data.m_castItem.m_guid != data.m_caster.m_guid) {
        out.sourceLow = static_cast<uint32_t>(data.m_castItem.m_guid);
        out.sourceHigh = static_cast<uint32_t>(data.m_castItem.m_guid >> 32);
        if (!out.sourceLow || out.sourceHigh != kItemGuidHigh) return false;
    }
    return out.spell != 0;
}

int GetPending(lua_State* L) {
    PendingEnchant pending;
    if (!ReadPending(pending)) return 0;
    lua_pushnumber(L, pending.spell);
    lua_pushnumber(L, pending.count);
    lua_pushnumber(L, pending.sourceLow);
    lua_pushnumber(L, pending.sourceHigh);
    lua_pushnumber(L, static_cast<double>(pending.token));
    return 5;
}

int CancelPendingFromLua(lua_State* L) {
    PendingEnchant pending;
    bool matches = lua_gettop(L) == 5 && ReadPending(pending);
    const double expected[] = {static_cast<double>(pending.spell), static_cast<double>(pending.count),
                               static_cast<double>(pending.sourceLow), static_cast<double>(pending.sourceHigh),
                               static_cast<double>(pending.token)};
    for (int index = 1; matches && index <= 5; ++index) {
        matches = lua_isnumber(L, index) && lua_tonumber(L, index) == expected[index - 1];
    }
    bool cancelled = false;
    // The cursor must still be the exact one the add-on inspected.
    if (matches && reinterpret_cast<uintptr_t>(s_targetingSpell) == pending.token) {
        Spell_C_CancelSpell(s_targetingSpell, true, true, kInterruptedReason);
        cancelled = s_targetingSpell == nullptr;
    }
    lua_pushboolean(L, cancelled ? 1 : 0);
    return 1;
}

}  // namespace

void WxlRegisterNpcBotEnchantLua() {
    if (!WxlProfileEnabled("WXL_NPCBOT_ENCHANT", true)) return;
    FrameScript_RegisterFunction("WarcraftXL_GetPendingItemEnchant", &GetPending);
    FrameScript_RegisterFunction("WarcraftXL_CancelPendingItemEnchant", &CancelPendingFromLua);
}

#endif  // WOWDECOMP_LINUX
