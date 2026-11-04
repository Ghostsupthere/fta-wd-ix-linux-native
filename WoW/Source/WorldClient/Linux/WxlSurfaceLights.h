#pragma once
// Native port of the supplied wxl-living-azeroth surface lights (Morfium's Living Azeroth,
// GPL-3.0-or-later): the torches, braziers and lamps the client knows about light the terrain, models
// and buildings around them, through shaders patched offline (tools/build-light-shader-pack.py ->
// Utils/WarcraftXL/visuals/light_shaders.pack) and one light texture.
//
// ON BY DEFAULT since 0.2.7: the profile setting WXL_SURFACE_LIGHTS defaults to 1 (WXL_SURFACE_LIGHTS=0 in the
// environment or WarcraftXL.cfg turns it off) and the lights need the pack to exist. Without the pack, or with
// the profile off, nothing about the client's shaders changes. With the profile on, the patched shaders stay
// inert (the stock lighting keeps working) while the Environment page's "Dynamic lights" option
// (lights.enabled, default on here) is switched off.
//
// Pure logic: Engine/Source/ModernAssets/WxlSurfaceLights/. This file is the engine adapter: the scene
// scan, the live CM2Light values, indoor and ray tests, the texture and the terrain-stage hook.
#include <cstddef>
#include <string>

#if defined(WOWDECOMP_LINUX)

// Registers the shader replacer (also done at static initialisation, so it is in place before the
// first shader is loaded).
void WxlSurfaceLightsInitialize();
// Patched shaders are in use (profile on, pack loaded and at least one shader replaced).
bool WxlSurfaceLightsAvailable();
// Environment page values (the 17 options of EnvironmentOptions.hpp, in order).
void WxlSurfaceLightsApply(const double* values, size_t count);
// Top of CWorldScene::RenderChunks: updates the light list and fills and binds the light texture.
void WxlSurfaceLightsBeforeTerrain();
// Once per rendered frame, after the world: puts the header back to "stock lighting only".
void WxlSurfaceLightsFrame();
// Device loss.
void WxlSurfaceLightsRelease();
// Leaving the world or the map.
void WxlSurfaceLightsWorldLeave();
// The Environment page's lighting readout.
std::string WxlSurfaceLightsStatus();

#endif
