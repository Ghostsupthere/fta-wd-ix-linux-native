#pragma once
// Native grass motion: a directional two-wave wind sway plus blades parting around the
// player. The engine's live ground-effect vertex shaders are patched offline into
// Utils/WarcraftXL/visuals/grass_shaders.pack (tools/build-wmo-shader-pack.py); the
// runtime substitutes them when the engine loads its shaders, feeds the free constant
// registers once per frame and marks rigid props (rocks, bones) so they stay still.
// Without the pack, or with WXL_GRASS_MOTION=0, the stock grass is untouched.
#include <cstdint>

#if defined(WOWDECOMP_LINUX)
// Before the engine loads its ground-effect shaders.
void WxlGrassInitialize();
// True when patched shaders are in use; the detail builder marks rigid props only then.
bool WxlGrassMotionActive();
// Fills c14..c22 (nine float4s) of the live grass constant block; call from the engine's
// per-chunk constant upload for the live (group 0) path.
void WxlGrassFillConstants(float* block36);
// Once per rendered frame.
void WxlGrassFrame();
// Classifies a ground-effect doodad model path; rigid props must not sway.
bool WxlGrassIsRigid(uint32_t doodadId, const char* modelPath);
// Environment page: wind amplitude and speed scale, player interaction on/off.
void WxlGrassSetMotion(float strength, float speed, bool interaction);
// Lua control bridge (Hellfire grass problem zones / global disable).
void WxlGrassSetSwayDisabled(bool disabled);
bool WxlGrassSwayDisabled();
#endif
