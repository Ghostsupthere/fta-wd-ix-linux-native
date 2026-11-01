#include "WoW/Source/Ui/Linux/WxlProfiler.h"

#if defined(WOWDECOMP_LINUX)

#include "Engine/Source/Base/Profiler.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/lua-5.1/src/lua.h"

namespace {

void SetNumber(lua_State* L, const char* key, double value) {
    lua_pushnumber(L, value);
    lua_setfield(L, -2, key);
}

int GetZones(lua_State* L) {
    uint32_t frames = 120;
    if (lua_isnumber(L, 1)) {
        const double wanted = lua_tonumber(L, 1);
        frames = wanted < 1.0 ? 1u : (wanted > 240.0 ? 240u : static_cast<uint32_t>(wanted));
    }
    Profiler::Summary summary;
    if (!Profiler::Summarize(frames, summary)) {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, 0, 6);
    SetNumber(L, "frames", summary.frames);
    SetNumber(L, "frameMs", summary.frameMs);
    SetNumber(L, "fps", summary.fps);
    SetNumber(L, "drawCalls", summary.drawCalls);
    SetNumber(L, "triangles", summary.triangles);
    lua_createtable(L, Profiler::Count, 0);
    for (uint32_t i = 0; i < Profiler::Count; ++i) {
        lua_createtable(L, 0, 4);
        lua_pushstring(L, summary.zones[i].name);
        lua_setfield(L, -2, "name");
        SetNumber(L, "selfMs", summary.zones[i].selfMs);
        SetNumber(L, "totalMs", summary.zones[i].totalMs);
        SetNumber(L, "calls", summary.zones[i].calls);
        lua_rawseti(L, -2, static_cast<int>(i) + 1);
    }
    lua_setfield(L, -2, "zones");
    return 1;
}

int SetEnabled(lua_State* L) {
    Profiler::SetEnabled(lua_toboolean(L, 1) != 0);
    lua_pushboolean(L, Profiler::Enabled() ? 1 : 0);
    return 1;
}

int IsEnabled(lua_State* L) {
    lua_pushboolean(L, Profiler::Enabled() ? 1 : 0);
    return 1;
}

}  // namespace

void WxlRegisterProfilerLua() {
    FrameScript_RegisterFunction("GetProfilerZones", &GetZones);
    FrameScript_RegisterFunction("SetProfilerEnabled", &SetEnabled);
    FrameScript_RegisterFunction("IsProfilerEnabled", &IsEnabled);
}

#endif  // WOWDECOMP_LINUX
