// Native port of the supplied wxl-combat-readability (GPL-3.0-or-later, Copyright (C) 2026
// WarcraftXL); see WxlCombatReadability.h. Key names, ranges and defaults are in
// Engine/Source/ModernAssets/WxlCombatReadability/ReadabilitySettings.hpp.
#include "WoW/Source/Ui/Linux/WxlCombatReadability.h"

#if defined(WOWDECOMP_LINUX)

#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/ModernAssets/NativeDiagnostic.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "WoW/Source/WorldClient/Linux/WxlVisualEnvironment.h"

namespace {

wxl::readability::Store g_store;

int Available(lua_State* L) {
    lua_pushboolean(L, 1);
    return 1;
}

int Get(lua_State* L) {
    size_t length = 0;
    const char* key = lua_isstring(L, 1) ? lua_tolstring(L, 1, &length) : nullptr;
    double value = 0.0;
    if (!key || !length || !g_store.Get(key, value)) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushnumber(L, value);
    return 1;
}

int Set(lua_State* L) {
    if (!lua_isstring(L, 1) || !lua_isnumber(L, 2)) {
        lua_pushboolean(L, 0);
        lua_pushnil(L);
        return 2;
    }
    size_t length = 0;
    const char* key = lua_tolstring(L, 1, &length);
    double normalized = 0.0;
    // Reports the stored (clamped) value back, so the add-on sees what the bridge actually kept.
    const bool readable = key && length && g_store.Set(key, lua_tonumber(L, 2)) && g_store.Get(key, normalized);
    lua_pushboolean(L, readable ? 1 : 0);
    if (readable) lua_pushnumber(L, normalized);
    else lua_pushnil(L);
    return 2;
}

int Reset(lua_State* L) {
    g_store.Reset();
    lua_pushboolean(L, 1);
    return 1;
}

}  // namespace

void WxlRegisterCombatReadabilityLua() {
    if (!WxlProfileEnabled("WXL_COMBAT_READABILITY", true)) return;
    FrameScript_RegisterFunction("WarcraftXL_ReadabilityAvailable", &Available);
    FrameScript_RegisterFunction("WarcraftXL_GetReadabilityOption", &Get);
    FrameScript_RegisterFunction("WarcraftXL_SetReadabilityOption", &Set);
    FrameScript_RegisterFunction("WarcraftXL_ResetReadabilityOptions", &Reset);
    WxlNativeDiagnostic("combat-readability: Lua control bridge ready");
}

wxl::readability::Settings WxlReadabilitySnapshot() { return g_store.Snapshot(); }

#endif  // WOWDECOMP_LINUX
