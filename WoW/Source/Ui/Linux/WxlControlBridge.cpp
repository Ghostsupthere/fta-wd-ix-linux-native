#include "WoW/Source/Ui/Linux/WxlControlBridge.h"

#if defined(WOWDECOMP_LINUX)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "Engine/Source/FrameScript/FrameScript.h"
#include "WoW/Source/WorldClient/Linux/WxlGrassMotion.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/lua-5.1/src/lua.h"

namespace {

// Matches wxl::gpu::control::kApiVersion (= 2) from the real WarcraftXL
// control ABI, so bridgeVersion round-trips a plausible number. Nothing on
// the Lua side currently gates on a minimum version.
constexpr double kApiVersion = 2.0;

// Field layout mirrors wxl::gpu::control::Settings byte-for-byte in spirit
// (same names/defaults/ranges as Control.hpp/Control.cpp), minus the ABI
// envelope fields (abiSize/apiVersion/activeRenderProfile/savedRenderProfile)
// that only mattered for the DLL<->proxy boundary this port doesn't have.
struct Settings {
    int32_t outlineEnabled = 1;
    int32_t facingMode = 1;  // 0=off, 1=ground, 2=overlay
    int32_t showTarget = 1;
    int32_t showMouseover = 1;

    int32_t normalEnabled = 1;
    int32_t normalShape = 0;
    int32_t normalCount = 1;
    float normalTipDistance = 3.00f;
    float normalSpacing = 0.50f;
    float normalScale = 1.00f;
    float normalWidth = 1.00f;
    float normalLength = 1.00f;
    float normalOpacity = 0.56f;
    float normalMaxModelFactor = 1.50f;
    float normalDistanceModelWeight = 0.50f;
    float normalSizeModelWeight = 0.75f;

    int32_t eliteEnabled = 1;
    int32_t eliteShape = 0;
    int32_t eliteCount = 1;
    float eliteTipDistance = 4.50f;
    float eliteSpacing = 0.75f;
    float eliteScale = 1.35f;
    float eliteWidth = 1.10f;
    float eliteLength = 1.00f;
    float eliteOpacity = 0.56f;
    float eliteMaxModelFactor = 1.60f;
    float eliteDistanceModelWeight = 0.60f;
    float eliteSizeModelWeight = 0.75f;

    int32_t bossEnabled = 1;
    int32_t bossShape = 0;
    int32_t bossCount = 2;
    float bossTipDistance = 8.50f;
    float bossSpacing = 1.20f;
    float bossScale = 2.40f;
    float bossWidth = 1.00f;
    float bossLength = 1.00f;
    float bossOpacity = 0.56f;
    float bossMaxModelFactor = 1.70f;
    float bossDistanceModelWeight = 0.70f;
    float bossSizeModelWeight = 0.75f;

    int32_t tailEnabled = 0;
    float tailDistanceMultiplier = 1.50f;
    float tailScaleMultiplier = 1.20f;
    float tailWidth = 1.00f;
    float tailLength = 1.00f;
    float tailOpacity = 0.50f;
};

Settings g_settings;

enum class ValueType { Integer, Float };

struct Option {
    const char* name;
    size_t offset;
    ValueType type;
    double minimum;
    double maximum;
};

#define WXL_OPT_INT(name, field, minimum, maximum) \
    Option{name, offsetof(Settings, field), ValueType::Integer, minimum, maximum}
#define WXL_OPT_FLOAT(name, field, minimum, maximum) \
    Option{name, offsetof(Settings, field), ValueType::Float, minimum, maximum}

constexpr std::array<Option, 46> kOptions = {{
    WXL_OPT_INT("outline.enabled", outlineEnabled, 0.0, 1.0),
    WXL_OPT_INT("facing.mode", facingMode, 0.0, 2.0),
    WXL_OPT_INT("facing.showTarget", showTarget, 0.0, 1.0),
    WXL_OPT_INT("facing.showMouseover", showMouseover, 0.0, 1.0),

    WXL_OPT_INT("normal.enabled", normalEnabled, 0.0, 1.0),
    WXL_OPT_INT("normal.shape", normalShape, 0.0, 3.0),
    WXL_OPT_INT("normal.count", normalCount, 1.0, 3.0),
    WXL_OPT_FLOAT("normal.distance", normalTipDistance, 0.25, 20.0),
    WXL_OPT_FLOAT("normal.spacing", normalSpacing, 0.10, 5.0),
    WXL_OPT_FLOAT("normal.scale", normalScale, 0.25, 6.0),
    WXL_OPT_FLOAT("normal.width", normalWidth, 0.35, 3.0),
    WXL_OPT_FLOAT("normal.length", normalLength, 0.35, 3.0),
    WXL_OPT_FLOAT("normal.opacity", normalOpacity, 0.05, 1.0),
    WXL_OPT_FLOAT("normal.maxModelFactor", normalMaxModelFactor, 1.0, 4.0),
    WXL_OPT_FLOAT("normal.distanceModelWeight", normalDistanceModelWeight, 0.0, 2.0),
    WXL_OPT_FLOAT("normal.sizeModelWeight", normalSizeModelWeight, 0.0, 2.0),

    WXL_OPT_INT("elite.enabled", eliteEnabled, 0.0, 1.0),
    WXL_OPT_INT("elite.shape", eliteShape, 0.0, 3.0),
    WXL_OPT_INT("elite.count", eliteCount, 1.0, 3.0),
    WXL_OPT_FLOAT("elite.distance", eliteTipDistance, 0.25, 30.0),
    WXL_OPT_FLOAT("elite.spacing", eliteSpacing, 0.10, 5.0),
    WXL_OPT_FLOAT("elite.scale", eliteScale, 0.25, 7.0),
    WXL_OPT_FLOAT("elite.width", eliteWidth, 0.35, 3.0),
    WXL_OPT_FLOAT("elite.length", eliteLength, 0.35, 3.0),
    WXL_OPT_FLOAT("elite.opacity", eliteOpacity, 0.05, 1.0),
    WXL_OPT_FLOAT("elite.maxModelFactor", eliteMaxModelFactor, 1.0, 5.0),
    WXL_OPT_FLOAT("elite.distanceModelWeight", eliteDistanceModelWeight, 0.0, 2.0),
    WXL_OPT_FLOAT("elite.sizeModelWeight", eliteSizeModelWeight, 0.0, 2.0),

    WXL_OPT_INT("boss.enabled", bossEnabled, 0.0, 1.0),
    WXL_OPT_INT("boss.shape", bossShape, 0.0, 3.0),
    WXL_OPT_INT("boss.count", bossCount, 1.0, 3.0),
    WXL_OPT_FLOAT("boss.distance", bossTipDistance, 0.50, 40.0),
    WXL_OPT_FLOAT("boss.spacing", bossSpacing, 0.10, 5.0),
    WXL_OPT_FLOAT("boss.scale", bossScale, 0.25, 8.0),
    WXL_OPT_FLOAT("boss.width", bossWidth, 0.35, 3.0),
    WXL_OPT_FLOAT("boss.length", bossLength, 0.35, 3.0),
    WXL_OPT_FLOAT("boss.opacity", bossOpacity, 0.05, 1.0),
    WXL_OPT_FLOAT("boss.maxModelFactor", bossMaxModelFactor, 1.0, 6.0),
    WXL_OPT_FLOAT("boss.distanceModelWeight", bossDistanceModelWeight, 0.0, 2.0),
    WXL_OPT_FLOAT("boss.sizeModelWeight", bossSizeModelWeight, 0.0, 2.0),

    WXL_OPT_INT("tail.enabled", tailEnabled, 0.0, 1.0),
    WXL_OPT_FLOAT("tail.distanceMultiplier", tailDistanceMultiplier, 1.0, 4.0),
    WXL_OPT_FLOAT("tail.scaleMultiplier", tailScaleMultiplier, 0.25, 4.0),
    WXL_OPT_FLOAT("tail.width", tailWidth, 0.35, 3.0),
    WXL_OPT_FLOAT("tail.length", tailLength, 0.35, 3.0),
    WXL_OPT_FLOAT("tail.opacity", tailOpacity, 0.05, 1.0),
}};

#undef WXL_OPT_FLOAT
#undef WXL_OPT_INT

const Option* FindOption(const char* name) {
    if (!name || !*name) {
        return nullptr;
    }

    for (const Option& option : kOptions) {
        if (std::strcmp(option.name, name) == 0) {
            return &option;
        }
    }

    return nullptr;
}

double ReadOptionValue(const Option& option) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(&g_settings);

    if (option.type == ValueType::Float) {
        return static_cast<double>(*reinterpret_cast<const float*>(bytes + option.offset));
    }

    return static_cast<double>(*reinterpret_cast<const int32_t*>(bytes + option.offset));
}

void WriteOptionValue(const Option& option, double value) {
    auto* bytes = reinterpret_cast<unsigned char*>(&g_settings);

    if (option.type == ValueType::Float) {
        *reinterpret_cast<float*>(bytes + option.offset) =
            static_cast<float>(std::clamp(value, option.minimum, option.maximum));
        return;
    }

    long rounded = std::lround(value);
    *reinterpret_cast<int32_t*>(bytes + option.offset) =
        static_cast<int32_t>(std::clamp<double>(static_cast<double>(rounded), option.minimum, option.maximum));
}

int32_t Script_WarcraftXL_IsAvailable(lua_State* L) {
    lua_pushboolean(L, 1);
    return 1;
}

int32_t Script_WarcraftXL_GetVersion(lua_State* L) {
    lua_pushnumber(L, kApiVersion);
    return 1;
}

int32_t Script_WarcraftXL_GetOption(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        lua_pushnil(L);
        return 1;
    }

    const Option* option = FindOption(lua_tolstring(L, 1, nullptr));

    if (!option) {
        lua_pushnil(L);
        return 1;
    }

    lua_pushnumber(L, ReadOptionValue(*option));
    return 1;
}

int32_t Script_WarcraftXL_SetOption(lua_State* L) {
    if (!lua_isstring(L, 1) || !lua_isnumber(L, 2)) {
        lua_pushboolean(L, 0);
        lua_pushnil(L);
        return 2;
    }

    const Option* option = FindOption(lua_tolstring(L, 1, nullptr));

    if (!option) {
        lua_pushboolean(L, 0);
        lua_pushnil(L);
        return 2;
    }

    WriteOptionValue(*option, lua_tonumber(L, 2));
    lua_pushboolean(L, 1);
    lua_pushnumber(L, ReadOptionValue(*option));
    return 2;
}

int32_t Script_WarcraftXL_SaveOptions(lua_State* L) {
    // No persistence yet -- g_settings lives for the process lifetime only.
    // Still a true/false-correct answer: the in-memory state asked to be
    // saved genuinely is the state GetOption will keep returning.
    lua_pushboolean(L, 1);
    return 1;
}

int32_t Script_WarcraftXL_ResetOptions(lua_State* L) {
    g_settings = Settings{};
    lua_pushboolean(L, 1);
    return 1;
}

// Grass sway control for problem zones (the addon decides; the renderer only gets a boolean).
int32_t Script_WarcraftXL_SetHellfireGrassSwayDisabled(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }

    WxlGrassSetSwayDisabled(lua_tonumber(L, 1) != 0.0);
    lua_pushboolean(L, 1);
    return 1;
}

int32_t Script_WarcraftXL_GetHellfireGrassSwayDisabled(lua_State* L) {
    lua_pushboolean(L, WxlGrassSwayDisabled() ? 1 : 0);
    return 1;
}

}  // namespace

void WxlRegisterControlBridgeLua() {
    FrameScript_RegisterFunction("WarcraftXL_IsAvailable", &Script_WarcraftXL_IsAvailable);
    FrameScript_RegisterFunction("WarcraftXL_GetVersion", &Script_WarcraftXL_GetVersion);
    FrameScript_RegisterFunction("WarcraftXL_GetOption", &Script_WarcraftXL_GetOption);
    FrameScript_RegisterFunction("WarcraftXL_SetOption", &Script_WarcraftXL_SetOption);
    FrameScript_RegisterFunction("WarcraftXL_SaveOptions", &Script_WarcraftXL_SaveOptions);
    FrameScript_RegisterFunction("WarcraftXL_ResetOptions", &Script_WarcraftXL_ResetOptions);
    FrameScript_RegisterFunction("WarcraftXL_SetHellfireGrassSwayDisabled", &Script_WarcraftXL_SetHellfireGrassSwayDisabled);
    FrameScript_RegisterFunction("WarcraftXL_GetHellfireGrassSwayDisabled", &Script_WarcraftXL_GetHellfireGrassSwayDisabled);
}


// The settings snapshot the unit-outline renderer reads, in the supplied 200-byte wxl::gpu::control::Settings
// layout: a 16-byte envelope (size, API version, two retired profile fields) then the option fields above.
static_assert(sizeof(Settings) + 16 == 200, "control settings no longer match the supplied ABI size");
bool WxlGetControlSettings(void* out, size_t size) {
    if (!out || size != sizeof(Settings) + 16) {
        return false;
    }
    const uint32_t envelope[4] = {static_cast<uint32_t>(size), static_cast<uint32_t>(kApiVersion), 0, 0};
    unsigned char* bytes = static_cast<unsigned char*>(out);
    std::memcpy(bytes, envelope, sizeof(envelope));
    std::memcpy(bytes + sizeof(envelope), &g_settings, sizeof(Settings));
    return true;
}

#endif  // WOWDECOMP_LINUX
