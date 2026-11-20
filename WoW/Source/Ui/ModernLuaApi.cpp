#include "ModernLuaApi.h"

#include "ModernLuaApiEmbedded.h"
#include "AddOns.h"
#include "ContainerFrame.h"
#include "PaperDollInfoFrame.h"
#include "ScriptEvents.h"
#include "GameUI.h"
#include "SpellBookFrame.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "WoW/Source/DB/DBClient/DBCacheInstances.h"
#include "WoW/Source/DB/DBClient/DBClient.h"
#include "WoW/Source/DB/Rec/ItemRec.h"
#include "WoW/Source/DB/Rec/SpellRec.h"
#include "WoW/Source/Object/ItemStats.h"
#include "WoW/Source/Object/ObjectClient/Item_C.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/UIUtil/Tooltip.h"
#include "Engine/Source/Base/Status.h"
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "WoW/Source/WowSvcs/WowSvcsClient/ClientServices.h"

#include <cstdio>

namespace {

int CaptureCallbackTrace(lua_State* state) {
    const int limit = 10;
    lua_createtable(state, 2, 0);
    lua_pushvalue(state, 1);
    lua_rawseti(state, -2, 1);
    lua_createtable(state, limit, 0);
    lua_Debug frame{};
    for (int level = 1; level <= limit && lua_getstack(state, level, &frame); ++level) {
        lua_getinfo(state, "Sl", &frame);
        lua_pushfstring(state, "%s:%d", frame.short_src, frame.currentline);
        lua_rawseti(state, -2, level);
    }
    lua_rawseti(state, -2, 2);
    return 1;
}

int InvokeCallback(lua_State* state) {
    const int count = lua_gettop(state);
    luaL_checkstack(state, 5, "modern callback dispatch");
    const bool trace = lua_toboolean(state, lua_upvalueindex(4));
    const int traceIndex = trace ? 1 : 0;
    if (trace) {
        lua_pushcfunction(state, CaptureCallbackTrace);
        lua_insert(state, traceIndex);
    }
    lua_pushvalue(state, lua_upvalueindex(1));
    lua_insert(state, traceIndex + 1);
    const auto* registrationTaint = lua_tostring(state, lua_upvalueindex(2));

    // Neither a secure Lua function nor a native binding gains authority by being
    // registered with an asynchronous API. Keep the shared dispatcher out of addon taint.
    const auto* savedTaint = lua_tainted;
    const int savedExpected = lua_taintexpected;
    const int savedClosure = lua_taintedclosure;
    lua_tainted = savedTaint ? savedTaint : registrationTaint;
    lua_taintexpected = savedExpected ? savedExpected : 1;
    lua_taintedclosure = 0;
    const int resultCount = lua_toboolean(state, lua_upvalueindex(3)) ? 1 : 0;
    const int result = lua_pcall(state, count, resultCount, traceIndex);
    lua_tainted = savedTaint;
    lua_taintexpected = savedExpected;
    lua_taintedclosure = savedClosure;

    if (result == 0) {
        lua_pushboolean(state, true);
        if (resultCount != 0) {
            lua_insert(state, -2);
        }
        return resultCount + 1;
    }
    if (trace && result == LUA_ERRRUN && lua_istable(state, -1)) {
        lua_pushboolean(state, false);
        lua_rawgeti(state, -2, 1);
        lua_rawgeti(state, -3, 2);
        return 3;
    }
    lua_pushboolean(state, false);
    lua_insert(state, -2);
    return 2;
}

int BindCallback(lua_State* state) {
    luaL_checktype(state, 1, LUA_TFUNCTION);
    const bool keepResult = lua_toboolean(state, 2);
    const bool trace = lua_toboolean(state, 3);
    lua_pushvalue(state, 1);
    lua_pushstring(state, lua_tainted ? lua_tainted : "FTA modern callback");
    lua_pushboolean(state, keepResult);
    lua_pushboolean(state, trace);
    lua_pushcclosure(state, InvokeCallback, 4);
    return 1;
}

const char* ResolveAddon(lua_State* state) {
    if (lua_type(state, 1) == LUA_TNUMBER) {
        const double index = lua_tonumber(state, 1);
        if (index >= 1 && index <= GetNumAddOns() && index == static_cast<uint32_t>(index)) {
            return GetAddOnByIndex(static_cast<uint32_t>(index) - 1);
        }
        luaL_argerror(state, 1, "add-on index out of range");
        return nullptr;
    }
    return GetAddOnName(luaL_checkstring(state, 1));
}

int AddonName(lua_State* state) {
    lua_pushstring(state, ResolveAddon(state));
    return 1;
}

const char* AddonCharacter(lua_State* state) {
    if (lua_isnoneornil(state, 2)) {
        // An absent character must not turn a current-character change into an account-wide change.
        const char* name = ClientServices::GetCharacterName();
        return name ? name : "";
    }
    return luaL_checkstring(state, 2);
}

int AddonState(lua_State* state) {
    const auto* name = ResolveAddon(state);
    const auto* character = AddonCharacter(state);
    lua_pushinteger(state, name ? GetAddOnEnableState(name, character, true) : 0);
    return 1;
}

int SetAddonState(lua_State* state) {
    const auto* name = ResolveAddon(state);
    const auto* character = AddonCharacter(state);
    if (name) {
        SetAddOnEnableState(name, character, lua_toboolean(state, 3) ? 2 : 0);
    }
    return 0;
}

int OptionalDependencies(lua_State* state) {
    const auto* name = ResolveAddon(state);
    const auto* dependencies = name ? GetAddonOptionalDeps(name) : nullptr;
    if (!dependencies) {
        return 0;
    }

    luaL_checkstack(state, dependencies->m_count, "add-on dependencies");
    for (uint32_t i = 0; i < dependencies->m_count; ++i) {
        lua_pushstring(state, dependencies->m_data[i]);
    }
    return dependencies->m_count;
}

int SaveAddonStates(lua_State*) {
    SaveAddOnEnableState();
    return 0;
}

int ResolveSpell(lua_State* state) {
    int32_t pet = 0;
    int32_t id = 0;
    if (lua_type(state, 1) == LUA_TNUMBER) {
        const double value = lua_tonumber(state, 1);
        if (value >= 1 && value <= INT32_MAX && value == static_cast<int32_t>(value)) {
            id = static_cast<int32_t>(value);
        }
    } else if (lua_type(state, 1) == LUA_TSTRING) {
        id = GetSpellIdByName(lua_tostring(state, 1), &pet);
    }
    SpellRec spell;
    if (id <= 0 || !g_spellDB.GetRecord(id, &spell)) {
        return 0;
    }
    lua_pushinteger(state, id);
    lua_pushboolean(state, pet != 0);
    return 2;
}

int ResolveItemName(lua_State* state) {
    if (lua_type(state, 1) != LUA_TSTRING) {
        return 0;
    }

    const auto id = GetItemIDByName(lua_tostring(state, 1));
    if (id <= 0) {
        return 0;
    }
    lua_pushinteger(state, id);
    return 1;
}

int ItemRecord(lua_State* state) {
    const auto id = static_cast<int32_t>(lua_tointeger(state, 1));
    auto* cached = g_itemDBCache.GetRecord(id, WOWGUID(), nullptr, nullptr, false);
    const auto* dbc = g_itemDB.GetRecord(id);
    if (!dbc && !cached) {
        return 0;
    }

    const auto inventory = cached ? cached->m_inventoryType : static_cast<uint32_t>(dbc->m_inventoryType);
    if (inventory < 29) {
        lua_pushstring(state, s_inventoryTypeTokens[inventory]);
    } else {
        lua_pushnil(state);
    }
    lua_pushinteger(state, cached ? cached->m_class : dbc->m_classID);
    lua_pushinteger(state, cached ? cached->m_subClass : dbc->m_subclassID);
    lua_pushinteger(state, cached ? cached->m_displayId : dbc->m_displayInfoID);
    if (!cached) {
        return 4;
    }
    lua_pushinteger(state, cached->m_bonding);
    lua_pushinteger(state, cached->m_itemSet);
    lua_pushinteger(state, cached->m_itemLevel);
    lua_pushinteger(state, cached->m_quality);
    return 8;
}

bool IsInteger(lua_State* state, int index, int minimum, int maximum) {
    if (lua_type(state, index) != LUA_TNUMBER) {
        return false;
    }
    const auto value = lua_tonumber(state, index);
    return value >= minimum && value <= maximum && value == static_cast<int>(value);
}

CGItem_C* BagItem(lua_State* state) {
    // Use the same slot mapping and access checks as Wrath's container API.
    // The unchecked legacy adapter accepts extra negative IDs as raw slots.
    if (!IsInteger(state, 1, -2, 11) || !IsInteger(state, 2, 1, 65536)) {
        return nullptr;
    }
    CGBag_C* bag;
    int32_t slot, bank;
    return CGContainerInfo::GetLuaBagAndSlot(state, &bag, &slot, &bank)
        ? bag->GetItemPointer(slot) : nullptr;
}


int GetCursorBagItem(lua_State* state) {
    auto* player = ClntObjMgrGetActivePlayerObj();
    if (!player || !CGGameUI::GetCursorItem()) {
        return 0;
    }

    WOWGUID item{}, container{};
    uint32_t slot = 0;
    CGGameUI::GetCursorItem(item, container, slot);
    if (!container) {
        return 0;
    }

    uint32_t bagID = 0;
    if (container == player->m_bag.m_guid) {
        // The player's native bag also contains equipment and bank slots.
        if (slot < 23 || slot > 38) {
            return 0;
        }
        slot -= 23;
    } else {
        for (uint32_t index = 0; index < 4; ++index) {
            if (container == GetBagAtIndex(index)) {
                bagID = index + 1;
                break;
            }
        }
        if (!bagID) {
            return 0;
        }
    }

    lua_pushinteger(state, bagID);
    lua_pushinteger(state, slot + 1);
    lua_pushnumber(state, static_cast<uint32_t>(item.m_guid));
    lua_pushinteger(state, CGGameUI::m_stackSplit);
    return 4;
}

CGItem_C* InventoryItem(lua_State* state) {
    int32_t slot = -1;
    if (!IsInteger(state, 1, 1, 118) || !ResolveInventorySlotArg(state, 1, slot)) {
        return nullptr;
    }
    auto* player = ClntObjMgrGetActivePlayerObj();
    return player ? player->m_bag.GetItemPointer(slot) : nullptr;
}

int ItemBinding(lua_State* state, CGItem_C* item) {
    if (!item) {
        return 0;
    }
    const bool bound = item->IsBound();
    const auto* stats = item->GetStats();
    lua_pushboolean(state, bound);
    if (bound && !stats) {
        return 1;
    }
    // Follow the binding category used by the native tooltip, independent
    // of localization and of any other lines above the binding line.
    lua_pushboolean(state, bound && !(stats->m_flags[0] & 0x08000000)
        && stats->m_bonding != 4 && stats->m_class != 10);
    return 2;
}

int ItemLevel(lua_State* state, const CGItem_C* item) {
    if (!item) {
        return 0;
    }
    const auto* stats = item->GetStats();
    if (!stats) {
        return 0;
    }
    lua_pushinteger(state, stats->m_itemLevel);
    return 1;
}

void ItemField(lua_State* state, const char* name, lua_Integer value) {
    lua_pushinteger(state, value);
    lua_setfield(state, -2, name);
}

int ItemSnapshot(lua_State* state, CGItem_C* item) {
    if (!item) {
        return 0;
    }
    const auto* stats = item->GetStats();
    lua_createtable(state, 0, 15);
    ItemField(state, "itemID", item->m_obj->m_entryID);
    const auto display = stats ? stats->m_displayId : item->GetDisplayID();
    if (display) {
        ItemField(state, "displayID", display);
    }
    ItemField(state, "stackCount", item->m_item->m_itemFieldStackCount);
    char guid[17];
    std::snprintf(guid, sizeof(guid), "%016llx", static_cast<unsigned long long>(item->m_obj->m_guid.m_guid));
    lua_pushstring(state, guid);
    lua_setfield(state, -2, "itemGUID");
    lua_pushstring(state, CGTooltip::GetItemHyperlinkString(item));
    lua_setfield(state, -2, "hyperlink");
    const int bindingFields = ItemBinding(state, item);
    if (bindingFields == 2) {
        lua_setfield(state, -3, "isSoulbound");
    }
    const bool bound = lua_toboolean(state, -1);
    lua_setfield(state, -2, "isBound");
    if (stats) {
        lua_pushboolean(state, bound && (stats->m_flags[0] & 0x08000000));
        lua_setfield(state, -2, "isAccountBound");
        ItemField(state, "baseItemLevel", stats->m_itemLevel);
        ItemField(state, "itemLevel", stats->m_itemLevel);
        ItemField(state, "quality", stats->m_quality);
    }
    return 1;
}

int IsItemCached(lua_State* state) {
    const bool cached = IsInteger(state, 1, 1, INT32_MAX)
        && g_itemDBCache.GetRecord(static_cast<int32_t>(lua_tointeger(state, 1)),
            WOWGUID(), nullptr, nullptr, false);
    lua_pushboolean(state, cached);
    return 1;
}

void ItemDataReady(int32_t id, const WOWGUID&, void*, bool success) {
    if (FrameScript_GetContext() && CGGameUI::m_initialized) {
        FrameScript_SignalEvent(SCRIPT_EVENT_ITEM_DATA_LOAD_RESULT, "%d%b", id, static_cast<int>(success));
    }
}

int RequestItem(lua_State* state) {
    if (!IsInteger(state, 1, 1, INT32_MAX)) {
        return luaL_argerror(state, 1, "itemID must be a positive integer");
    }
    const bool connected = ClientServices::s_currentConnection != nullptr;
    if (connected) {
        // Uses the same per-pump limits, duplicate suppression, and shutdown
        // cleanup as tooltip requests. The callback never retains a Lua value.
        g_itemDBCache.GetRecord(static_cast<int32_t>(lua_tointeger(state, 1)),
            WOWGUID(), ItemDataReady, nullptr, true);
    }
    lua_pushboolean(state, connected);
    return 1;
}

constexpr luaL_Reg nativeFunctions[] = {
    { "resolveSpell", ResolveSpell },
    { "resolveItemName", ResolveItemName },
    { "itemRecord", ItemRecord },
    { "getBagItem", [](lua_State* state) { return ItemSnapshot(state, BagItem(state)); } },
    { "getCursorBagItem", GetCursorBagItem },
    { "getInventoryItem", [](lua_State* state) { return ItemSnapshot(state, InventoryItem(state)); } },
    { "getBagItemBinding", [](lua_State* state) { return ItemBinding(state, BagItem(state)); } },
    { "getInventoryItemBinding", [](lua_State* state) { return ItemBinding(state, InventoryItem(state)); } },
    { "getBagItemLevel", [](lua_State* state) { return ItemLevel(state, BagItem(state)); } },
    { "getInventoryItemLevel", [](lua_State* state) { return ItemLevel(state, InventoryItem(state)); } },
    { "isItemCached", IsItemCached },
    { "requestItem", RequestItem },
    { "bindCallback", BindCallback },
    { "addonName", AddonName },
    { "addonState", AddonState },
    { "setAddonState", SetAddonState },
    { "optionalDependencies", OptionalDependencies },
    { "saveAddonStates", SaveAddonStates },
    { nullptr, nullptr }
};

} // namespace

namespace ModernLuaApi {

int ReadEnabledGroups() {
    auto* setting = CVar::Register("ftaModernLuaAPI",
        "Experimental FTA Lua groups: 1 foundation, 2 game data, 4 UI, 8 diagnostics",
        1, "0", nullptr, 5, false, nullptr, false);
    return setting && setting->m_intValue >= 0 ? setting->m_intValue & 15 : 0;
}

bool Install(lua_State* state, int groups, CStatus* status) {
    const int originalTop = lua_gettop(state);
    int result = luaL_loadbuffer(state, ModernLuaApiScript, sizeof(ModernLuaApiScript) - 1,
        "@Interface\\FTA\\ModernLuaApi.lua");
    if (result == 0) {
        lua_pushinteger(state, groups);
        lua_newtable(state);
        luaL_register(state, nullptr, nativeFunctions);
        result = lua_pcall(state, 2, 0, 0);
    }

    if (result != 0 && status) {
        const char* error = lua_tostring(state, -1);
        status->Add(STATUS_WARNING, "FTA modern Lua API: %s", error ? error : "Lua initialization failed");
    }
    lua_settop(state, originalTop);
    return result == 0;
}

} // namespace ModernLuaApi
