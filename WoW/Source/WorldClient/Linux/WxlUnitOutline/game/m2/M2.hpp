// Native stand-in for the core's M2 accessors used by the supplied marker code.
#pragma once

namespace wxl::game::m2 {
// Rendered Z orientation from the model's world transform (row 0 is local +X, the forward direction).
bool VisualFacing(void* model, float& outFacing);
// Horizontal extents of the model's authored bounding box and the XY scale of its live transform.
// Returns false when either is unavailable.
bool FootprintInputs(void* model, float& extentX, float& extentY, float& instanceScale);
}  // namespace wxl::game::m2
