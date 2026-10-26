#include "WoW/Source/Game/Linux/WxlVisualTimeOverride.h"

#if defined(WOWDECOMP_LINUX)

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/lua-5.1/src/lua.h"

namespace {

bool g_enabled = false;
int32_t g_hour = 12;
int32_t g_minute = 0;

int32_t ClampHour(double value) {
    long hour = std::lround(value);
    return static_cast<int32_t>(std::clamp<long>(hour, 0, 23));
}

int32_t ClampMinute(double value) {
    long minute = std::lround(value);
    return static_cast<int32_t>(std::clamp<long>(minute, 0, 59));
}

int32_t Script_WarcraftXL_SetVisualTime(lua_State* L) {
    if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
        lua_pushboolean(L, 0);
        return 1;
    }

    g_hour = ClampHour(lua_tonumber(L, 1));
    g_minute = ClampMinute(lua_tonumber(L, 2));
    g_enabled = true;

    lua_pushboolean(L, 1);
    lua_pushnumber(L, static_cast<double>(g_hour));
    lua_pushnumber(L, static_cast<double>(g_minute));
    return 3;
}

int32_t Script_WarcraftXL_ClearVisualTime(lua_State* L) {
    g_enabled = false;
    lua_pushboolean(L, 1);
    return 1;
}

int32_t Script_WarcraftXL_GetVisualTime(lua_State* L) {
    lua_pushboolean(L, g_enabled ? 1 : 0);
    lua_pushnumber(L, static_cast<double>(g_hour));
    lua_pushnumber(L, static_cast<double>(g_minute));
    return 3;
}

}  // namespace

bool WxlGetVisualTimeOverride(int32_t* hour, int32_t* minute) {
    if (!g_enabled) {
        return false;
    }

    *hour = g_hour;
    *minute = g_minute;
    return true;
}

void WxlRegisterVisualTimeLua() {
    FrameScript_RegisterFunction("WarcraftXL_SetVisualTime", &Script_WarcraftXL_SetVisualTime);
    FrameScript_RegisterFunction("WarcraftXL_ClearVisualTime", &Script_WarcraftXL_ClearVisualTime);
    FrameScript_RegisterFunction("WarcraftXL_GetVisualTime", &Script_WarcraftXL_GetVisualTime);
}

#endif  // WOWDECOMP_LINUX
