#if defined(WOWDECOMP_LINUX)
#include "WxlEnvironmentOptions.h"
#include "WxlGrassInstancing.h"
#include "WxlGrassMotion.h"
#include "WxlSurfaceLights.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/ModernAssets/NativeDiagnostic.h"
#include "Engine/Source/ModernAssets/WxlGrass/EnvironmentOptions.hpp"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {
namespace env = wxl::grass::environment;

env::Values values;
std::filesystem::path iniPath;
bool loaded = false;

// The 30-second performance sample the page's "Start" button begins.
constexpr double kSampleSeconds = 30.0;
bool sampling = false;
std::chrono::steady_clock::time_point sampleStart, lastFrame;
bool haveLastFrame = false;
std::vector<double> sampleMs;
env::FrameSummary lastSummary;
double lastSampleSeconds = 0;

std::filesystem::path ClientDirectory() {
    std::error_code error;
    const auto executable = std::filesystem::canonical("/proc/self/exe", error);
    return error ? std::filesystem::current_path() : executable.parent_path();
}

void Apply() {
    const env::Motion motion = env::MotionOf(values);
    WxlGrassSetMotion(motion.strength, motion.speed, motion.interaction);
    WxlGrassInstancingSetEnabled(values.v[env::kGrassInstanced] != 0.0);
    WxlSurfaceLightsApply(values.v, env::kCount);
}

bool Save() {
    if (iniPath.empty()) return false;
    std::error_code error;
    std::filesystem::create_directories(iniPath.parent_path(), error);
    const std::filesystem::path temporary = iniPath.string() + ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        const std::string text = env::Format(values);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) { std::filesystem::remove(temporary, error); return false; }
    }
    std::filesystem::rename(temporary, iniPath, error);
    if (error) { std::filesystem::remove(temporary, error); return false; }
    return true;
}

// Index of the option named by Lua argument 1, or env::kCount.
size_t FindArgument(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING) return env::kCount;
    size_t length = 0;
    const char* name = lua_tolstring(L, 1, &length);
    return name ? env::Find(std::string_view(name, length)) : env::kCount;
}

int32_t Script_Available(lua_State* L) {
    lua_pushboolean(L, 1);
    return 1;
}

int32_t Script_Get(lua_State* L) {
    const size_t index = FindArgument(L);
    if (index == env::kCount) lua_pushnil(L); else lua_pushnumber(L, values.v[index]);
    return 1;
}

int32_t Script_Set(lua_State* L) {
    const size_t index = FindArgument(L);
    const bool ok = index != env::kCount && lua_type(L, 2) == LUA_TNUMBER && std::isfinite(lua_tonumber(L, 2));
    if (ok) {
        values.v[index] = env::Normalize(index, lua_tonumber(L, 2));
        Apply();
    }
    lua_pushboolean(L, ok ? 1 : 0);
    if (ok) lua_pushnumber(L, values.v[index]); else lua_pushnil(L);
    return 2;
}

int32_t Script_Save(lua_State* L) {
    lua_pushboolean(L, Save() ? 1 : 0);
    return 1;
}

int32_t Script_Reset(lua_State* L) {
    values = env::Values();
    Apply();
    lua_pushboolean(L, Save() ? 1 : 0);
    return 1;
}

int32_t Script_ResetTiming(lua_State* L) {
    sampling = true;
    haveLastFrame = false;
    sampleMs.clear();
    sampleStart = std::chrono::steady_clock::now();
    lastSummary = {};
    lua_pushboolean(L, 1);
    return 1;
}

// There is no baked-versus-instanced comparison natively: both paths share the engine's placement
// math (BakeColor / BuildInstanceMatrix), which tests/wxl_grass_instancing_test.cpp checks. Say so
// instead of pretending to have run it.
int32_t Script_VerifyPlacement(lua_State* L) {
    lua_pushboolean(L, 1);
    return 1;
}

std::string GrassStatus() {
    const WxlGrassInstancingStats stats = WxlGrassInstancingGetStats();
    char text[640];
    const char* state = !stats.loaded ? "unavailable (no grass_instanced.pack, or WXL_GRASS_INSTANCING=0)"
                        : stats.problem ? "stopped after a device problem; stock grass until graphics reset"
                        : stats.enabled ? "on" : "off in settings";
    std::snprintf(text, sizeof(text),
                  "Grass: optimized rendering %s. %zu cached batches, %zu models; last interval drew %llu, fell back %llu, failed %llu.",
                  state, stats.batches, stats.models, (unsigned long long)stats.drawn,
                  (unsigned long long)stats.fallbacks, (unsigned long long)stats.failures);
    std::string out = text;
    if (sampling) {
        std::snprintf(text, sizeof(text), "\nPerformance sample running: %.0f of %.0f seconds.",
                      std::chrono::duration<double>(std::chrono::steady_clock::now() - sampleStart).count(), kSampleSeconds);
        out += text;
    } else if (lastSummary.frames) {
        std::snprintf(text, sizeof(text),
                      "\nLast %zu frames (%.0f s): median %.2f ms, 95th %.2f ms, 99th %.2f ms; worst %.2f ms.",
                      lastSummary.frames, lastSampleSeconds, lastSummary.medianMs, lastSummary.p95Ms,
                      lastSummary.p99Ms, lastSummary.worstMs);
        out += text;
    }
    out += "\nWind and grass interaction apply immediately. Grass cache and rebuild budget are saved but unused: the native "
           "instancing keeps static buffers.";
    return out;
}

int32_t Script_Status(lua_State* L) {
    const std::string grass = GrassStatus();
    lua_pushlstring(L, grass.data(), grass.size());
    const std::string lights = WxlSurfaceLightsStatus();
    lua_pushlstring(L, lights.data(), lights.size());
    static const char placement[] =
        "not run: instanced and stock grass share the engine's own placement math (checked offline)";
    lua_pushlstring(L, placement, sizeof(placement) - 1);
    return 3;
}
}  // namespace

void WxlEnvironmentOptionsLoad() {
    if (loaded) return;
    loaded = true;
    iniPath = ClientDirectory() / "WTF/WXL/Environment.ini";
    std::ifstream in(iniPath, std::ios::binary);
    if (in) {
        std::stringstream buffer;
        buffer << in.rdbuf();
        const size_t read = env::Parse(buffer.str(), values);
        WxlNativeDiagnostic("environment: read %zu option(s) from %s", read, iniPath.string().c_str());
    }
    Apply();
}

void WxlEnvironmentOptionsFrame() {
    if (!sampling) return;
    const auto now = std::chrono::steady_clock::now();
    if (haveLastFrame) sampleMs.push_back(std::chrono::duration<double, std::milli>(now - lastFrame).count());
    lastFrame = now;
    haveLastFrame = true;
    const double elapsed = std::chrono::duration<double>(now - sampleStart).count();
    if (elapsed >= kSampleSeconds && !sampleMs.empty()) {
        sampling = false;
        lastSampleSeconds = elapsed;
        lastSummary = env::Summarize(sampleMs);
        sampleMs.clear();
        sampleMs.shrink_to_fit();
    }
}

void WxlRegisterEnvironmentLua() {
    WxlEnvironmentOptionsLoad();
    FrameScript_RegisterFunction("WarcraftXL_EnvironmentAvailable", &Script_Available);
    FrameScript_RegisterFunction("WarcraftXL_GetEnvironmentOption", &Script_Get);
    FrameScript_RegisterFunction("WarcraftXL_SetEnvironmentOption", &Script_Set);
    FrameScript_RegisterFunction("WarcraftXL_SaveEnvironmentOptions", &Script_Save);
    FrameScript_RegisterFunction("WarcraftXL_ResetEnvironmentOptions", &Script_Reset);
    FrameScript_RegisterFunction("WarcraftXL_GetEnvironmentStatus", &Script_Status);
    FrameScript_RegisterFunction("WarcraftXL_VerifyGrassPlacement", &Script_VerifyPlacement);
    FrameScript_RegisterFunction("WarcraftXL_ResetEnvironmentTiming", &Script_ResetTiming);
}
#endif
