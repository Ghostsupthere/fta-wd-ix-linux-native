#include "Engine/Source/ModernAssets/WxlSelectionCircle/SelectionCircleMatrix.hpp"
#include <cassert>
#include <cmath>
#include <limits>

static bool Near(float a, float b) { return std::fabs(a - b) < 1e-5f; }

int main() {
    using namespace wxl::selcircle;
    float facing = 0;
    assert(FacingFromWorldRow(1, 0, facing) && Near(facing, 0));
    assert(FacingFromWorldRow(0, 2, facing) && Near(facing, kPi / 2));
    assert(FacingFromWorldRow(-1, 0, facing) && Near(std::fabs(facing), kPi));
    assert(!FacingFromWorldRow(0, 0, facing) && !FacingFromWorldRow(1e-6f, 0, facing));
    assert(!FacingFromWorldRow(std::numeric_limits<float>::quiet_NaN(), 1, facing));
    assert(!FacingFromWorldRow(std::numeric_limits<float>::infinity(), 1, facing));

    float m[16];
    for (float& x : m) x = 7.0f;
    WriteWorldFacingMatrix(m, 0.0f);   // angle = pi: marker points opposite the forward axis
    assert(Near(m[0], -1) && Near(m[1], 0) && Near(m[4], 0) && Near(m[5], -1));
    assert(m[10] == 1 && m[15] == 1 && m[2] == 0 && m[3] == 0 && m[8] == 0 && m[12] == 0 && m[14] == 0);
    WriteWorldFacingMatrix(m, kPi);    // angle = 0: identity rotation
    assert(Near(m[0], 1) && Near(m[1], 0) && Near(m[4], 0) && Near(m[5], 1));
    WriteWorldFacingMatrix(m, kPi / 2);   // angle = pi/2
    assert(Near(m[0], 0) && Near(m[1], 1) && Near(m[4], -1) && Near(m[5], 0));
    // A proper rotation: orthonormal rows, determinant 1.
    assert(Near(m[0] * m[5] - m[1] * m[4], 1));
    return 0;
}
