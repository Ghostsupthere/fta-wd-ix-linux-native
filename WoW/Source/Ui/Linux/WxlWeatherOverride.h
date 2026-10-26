#ifndef WOW_SOURCE_UI_LINUX_WXLWEATHEROVERRIDE_H
#define WOW_SOURCE_UI_LINUX_WXLWEATHEROVERRIDE_H

// Linux-only port of WarcraftXL's wxl-weather-control extension (see
// Server/From_The_Ashes_Source/WarcraftXL/scripts/wxl-weather-control in the
// FTA-Project tree). The original hooks the client's native SetWeather
// function via MinHook against a specific retail Wow.exe's memory offsets;
// this client already ships the equivalent mechanism as real, first-class
// source -- a "force-weather-type-on" registry-driven feature in
// WoW/Source/WorldClient/MapWeather.cpp (Weather::Weather()'s constructor,
// and the s_weatherTypeForced guard inside Weather::SetType()) that already
// locks weather in place once set. This port exposes that same mechanism
// live via Lua instead of a one-time registry value read at world-load.
//
// Entirely guarded behind WOWDECOMP_LINUX; no effect on a non-Linux build of
// the same shared source.

#if defined(WOWDECOMP_LINUX)

#include <cstdint>

class WeatherRec;

// Called from GameUI.cpp's SMSG_WEATHER handler, right before the real,
// unmodified World::s_weather->SetType(...) call -- a pure recording side
// channel (matches wxl-weather-control's own g_naturalWeather tracking) so
// WarcraftXL_SetWeatherOverride(Automatic) has a real natural state to
// restore to, instead of just releasing the lock and waiting for the next
// server weather push.
void WxlTrackNaturalWeather(uint32_t type, float intensity, const WeatherRec* rec, float transition);

void WxlRegisterWeatherLua();

#endif  // WOWDECOMP_LINUX

#endif
