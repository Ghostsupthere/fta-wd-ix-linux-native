#include "WoW/Source/Ui/Linux/WxlNamePlates.h"

#if defined(WOWDECOMP_LINUX)

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "Engine/Source/ModernAssets/WxlNamePlates/Registry.hpp"
#include "WoW/Source/Object/WGUID.h"
#include "WoW/Source/Ui/NamePlateFrame.h"
#include "WoW/Source/Ui/ScriptEvents.h"
#include "WoW/Source/Ui/Types.h"

bool Script_GetGUIDFromToken(const char* token, WOWGUID& guid, bool defaultToTarget);

namespace {

constexpr float kDefaultDistance = 41.0f;

wxl::nameplates::Registry g_registry;
CVar* g_distanceCVar = nullptr;

enum ExtraEvent : uint32_t { Created, UnitAdded, UnitRemoved, OwnerChanged, ExtraCount };
const char* const kExtraNames[ExtraCount] = {"NAME_PLATE_CREATED", "NAME_PLATE_UNIT_ADDED", "NAME_PLATE_UNIT_REMOVED",
                                             "NAME_PLATE_OWNER_CHANGED"};

void Signal(ExtraEvent which, const char* format, ...) {
    va_list args;
    va_start(args, format);
    vFrameScript_SignalEvent(NUM_SCRIPT_EVENTS + which, format, args);
    va_end(args);
}

void TokenName(int slot, char* out, size_t size) { std::snprintf(out, size, "nameplate%d", slot + 1); }

// The Lua object of a frame, registered on demand.
bool PushFrame(lua_State* L, CGNamePlateFrame* frame) {
    if (!frame) return false;
    if (!frame->m_luaRegistered) frame->RegisterScriptObject(nullptr);
    lua_rawgeti(L, LUA_REGISTRYINDEX, frame->m_luaObjectRef);
    return true;
}

int ActiveSlotForGuid(const WOWGUID& guid) { return g_registry.ActiveSlotForGuid(guid.m_guid); }

// A "0x..." guid string, or any unit token the client understands.
bool GuidFromArgument(lua_State* L, int index, WOWGUID& guid) {
    if (lua_type(L, index) == LUA_TNUMBER) {
        guid.m_guid = static_cast<uint64_t>(lua_tonumber(L, index));
        return true;
    }
    if (!lua_isstring(L, index)) return false;
    const char* text = lua_tostring(L, index);
    if (!text || !*text) return false;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        char* end = nullptr;
        guid.m_guid = std::strtoull(text + 2, &end, 16);
        return end && end != text + 2 && !*end;
    }
    return Script_GetGUIDFromToken(text, guid, false);
}

int GetNamePlates(lua_State* L) {
    lua_newtable(L);
    int n = 0;
    for (int i = 0; i < static_cast<int>(wxl::nameplates::Registry::kMaxPlates); ++i) {
        if (!g_registry.ActiveAt(i)) continue;
        if (PushFrame(L, const_cast<CGNamePlateFrame*>(static_cast<const CGNamePlateFrame*>(g_registry.FrameAt(i))))) lua_rawseti(L, -2, ++n);
    }
    return 1;
}

int GetNamePlateForUnit(lua_State* L) {
    WOWGUID guid;
    const int slot = GuidFromArgument(L, 1, guid) ? ActiveSlotForGuid(guid) : -1;
    if (slot < 0 || !PushFrame(L, const_cast<CGNamePlateFrame*>(static_cast<const CGNamePlateFrame*>(g_registry.FrameAt(slot))))) return 0;
    return 1;
}

int GetNamePlateByGUID(lua_State* L) { return GetNamePlateForUnit(L); }

int GetNamePlateTokenByGUID(lua_State* L) {
    WOWGUID guid;
    const int slot = GuidFromArgument(L, 1, guid) ? ActiveSlotForGuid(guid) : -1;
    if (slot < 0) return 0;
    char token[16];
    TokenName(slot, token, sizeof(token));
    lua_pushstring(L, token);
    return 1;
}

bool DistanceChanged(CVar*, const char*, const char*, void*) { return true; }

}  // namespace

void WxlCreateScriptEvents() {
    WxlNamePlatesReset();  // a new UI context: frames and Lua references from the old one are gone
    static const char* names[NUM_SCRIPT_EVENTS + ExtraCount];
    for (uint32_t i = 0; i < NUM_SCRIPT_EVENTS; ++i) names[i] = g_scriptEvents[i];
    for (uint32_t i = 0; i < ExtraCount; ++i) names[NUM_SCRIPT_EVENTS + i] = kExtraNames[i];
    FrameScript_CreateEvents(names, NUM_SCRIPT_EVENTS + ExtraCount);
}

void WxlRegisterNamePlatesLua() {
    lua_State* L = FrameScript_GetContext();
    if (!L) return;
    lua_newtable(L);
    lua_pushcfunction(L, &GetNamePlates);
    lua_setfield(L, -2, "GetNamePlates");
    lua_pushcfunction(L, &GetNamePlateForUnit);
    lua_setfield(L, -2, "GetNamePlateForUnit");
    lua_pushcfunction(L, &GetNamePlateByGUID);
    lua_setfield(L, -2, "GetNamePlateByGUID");
    lua_pushcfunction(L, &GetNamePlateTokenByGUID);
    lua_setfield(L, -2, "GetNamePlateTokenByGUID");
    lua_setglobal(L, "C_NamePlate");
}

void WxlNamePlatesRegisterCVars() {
    g_distanceCVar = CVar::Register("nameplateDistance", "How far, in yards, a nameplate may be shown", 0x20, "41", &DistanceChanged,
                                    CATEGORY_GAME, false, nullptr, false);
}

void WxlNamePlateShown(CGNamePlateFrame* frame) {
    if (!frame) return;
    const auto shown = g_registry.Show(frame, frame->m_guid.m_guid);
    if (shown.slot < 0) return;  // more than 64 frames: the extra ones get no token
    char token[16];
    TokenName(shown.slot, token, sizeof(token));
    if (shown.created) {
        if (!frame->m_luaRegistered) frame->RegisterScriptObject(nullptr);
        Signal(Created, "%r", frame->m_luaObjectRef);
    }
    Signal(shown.ownerChanged ? OwnerChanged : UnitAdded, "%s", token);
}

void WxlNamePlateHidden(CGNamePlateFrame* frame) {
    const int slot = g_registry.Hide(frame);
    if (slot < 0) return;
    char token[16];
    TokenName(slot, token, sizeof(token));
    // The token still resolves while the event runs, so handlers can read the unit that is leaving.
    Signal(UnitRemoved, "%s", token);
    g_registry.Release(slot);
}

void WxlNamePlateFrameDestroyed(CGNamePlateFrame* frame) { g_registry.Destroy(frame); }

void WxlNamePlatesReset() { g_registry.Reset(); }

bool WxlNamePlateGuidFromIndex(uint32_t oneBasedIndex, WOWGUID& guid) {
    uint64_t value = 0;
    if (!g_registry.GuidAtIndex(oneBasedIndex, value)) return false;
    guid.m_guid = value;
    return true;
}

float WxlNamePlateDistanceSquared() {
    float yards = kDefaultDistance;
    if (g_distanceCVar && g_distanceCVar->m_floatValue > 0.0f) yards = g_distanceCVar->m_floatValue;
    if (yards > 400.0f) yards = 400.0f;
    return yards * yards;
}

#endif  // WOWDECOMP_LINUX
