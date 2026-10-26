#include "WoW/Source/Ui/Linux/WxlWeatherOverride.h"

#if defined(WOWDECOMP_LINUX)

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "WoW/Source/WorldClient/MapWeather.h"
#include "WoW/Source/WorldClient/World.h"

namespace {

// Mirrors wxl-weather-control's WeatherMode enum/NativeWeatherType mapping:
// 0=Automatic, 1=Disabled, 2=Rain, 3=Snow, 4=Sandstorm (Lua-facing values);
// the client's own Weather::SetType() takes native type 0=none/1=rain/
// 2=snow/3=sandstorm and rejects anything above 3.
enum class WeatherMode : int32_t {
    Automatic = 0,
    Disabled = 1,
    Rain = 2,
    Snow = 3,
    Sandstorm = 4,
};

int32_t g_mode = static_cast<int32_t>(WeatherMode::Automatic);
float g_intensity = 1.0f;

bool g_haveNatural = false;
uint32_t g_naturalType = 0;
float g_naturalIntensity = 0.0f;
const WeatherRec* g_naturalRec = nullptr;
float g_naturalTransition = 1.0f;

WeatherMode NormalizeMode(double value) {
    long mode = std::lround(value);
    mode = std::clamp<long>(mode, static_cast<long>(WeatherMode::Automatic), static_cast<long>(WeatherMode::Sandstorm));
    return static_cast<WeatherMode>(mode);
}

float NormalizeIntensity(double value) {
    if (!std::isfinite(value)) {
        return 1.0f;
    }

    return std::clamp(static_cast<float>(value), 0.1f, 1.0f);
}

uint32_t NativeWeatherType(WeatherMode mode) {
    switch (mode) {
        case WeatherMode::Rain:
            return 1;
        case WeatherMode::Snow:
            return 2;
        case WeatherMode::Sandstorm:
            return 3;
        default:
            return 0;
    }
}

bool ApplyMode(WeatherMode mode, float intensity) {
    if (!World::s_weather) {
        return false;
    }

    if (mode == WeatherMode::Automatic) {
        s_weatherTypeForced = 0;

        if (g_haveNatural) {
            World::s_weather->SetType(g_naturalType, g_naturalIntensity, g_naturalRec, true, g_naturalTransition);
        }

        return true;
    }

    uint32_t nativeType = mode == WeatherMode::Disabled ? 0 : NativeWeatherType(mode);
    float appliedIntensity = mode == WeatherMode::Disabled ? 0.0f : intensity;

    // Set the forced type first, exactly like the registry-driven
    // "force-weather-type-on" path this reuses (Weather::Weather()'s
    // constructor calls SetType() before raising s_weatherTypeForced) --
    // SetType() itself refuses to run at all once the flag is already set.
    World::s_weather->SetType(nativeType, appliedIntensity, nullptr, true, 1.0f);
    s_weatherTypeForced = 1;
    return true;
}

int32_t Script_WarcraftXL_SetWeatherOverride(lua_State* L) {
    if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
        lua_pushboolean(L, 0);
        return 1;
    }

    WeatherMode mode = NormalizeMode(lua_tonumber(L, 1));
    float intensity = NormalizeIntensity(lua_tonumber(L, 2));
    bool accepted = ApplyMode(mode, intensity);

    if (accepted) {
        g_mode = static_cast<int32_t>(mode);
        g_intensity = intensity;
    }

    lua_pushboolean(L, accepted ? 1 : 0);
    lua_pushnumber(L, static_cast<double>(static_cast<int32_t>(mode)));
    lua_pushnumber(L, static_cast<double>(intensity));
    return 3;
}

int32_t Script_WarcraftXL_GetWeatherOverride(lua_State* L) {
    lua_pushnumber(L, static_cast<double>(g_mode));
    lua_pushnumber(L, static_cast<double>(g_intensity));
    return 2;
}

}  // namespace

void WxlTrackNaturalWeather(uint32_t type, float intensity, const WeatherRec* rec, float transition) {
    g_haveNatural = true;
    g_naturalType = type;
    g_naturalIntensity = intensity;
    g_naturalRec = rec;
    g_naturalTransition = transition;
}

void WxlRegisterWeatherLua() {
    FrameScript_RegisterFunction("WarcraftXL_SetWeatherOverride", &Script_WarcraftXL_SetWeatherOverride);
    FrameScript_RegisterFunction("WarcraftXL_GetWeatherOverride", &Script_WarcraftXL_GetWeatherOverride);
}

#endif  // WOWDECOMP_LINUX
