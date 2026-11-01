// Native stand-in for the core's camera globals: the world camera of the frame being drawn, in world
// coordinates. The native engine's own view matrix has the eye at zero and draws camera-relative geometry,
// so the translation is composed back in (as WxlLootBeam does) to let world positions project directly.
#pragma once

namespace wxl::game::camera {
const float* View();
const float* Projection();
// Stores the camera for the frame being drawn (called by the native glue, not by the supplied code).
void Store(const float* view, const float* projection);
}  // namespace wxl::game::camera
