// Native stand-in for the core's world-pick facade (segment intersection against terrain and WMO faces).
#pragma once

namespace wxl::game::world {
struct Vec3 { float x; float y; float z; };
struct WorldHit { int type; Vec3 pos; void* objLo; void* objHi; float t; };
// Returns 0 on a miss, 1 for terrain, 3 for WMO geometry (the native picker does not tell them apart, so
// both report 1); M2 geometry never blocks, as the supplied accepted-ground-types logic expects.
int IntersectSegment(const Vec3& start, const Vec3& end, WorldHit& out);
}  // namespace wxl::game::world
