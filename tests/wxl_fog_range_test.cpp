#include "WoW/Source/WorldClient/Linux/WxlFogRange.h"

#include <cassert>
#include <limits>

int main() {
    float end = -1.0f;
    assert(WxlScaleFogRange(100.0f, 400.0f, 0.5f, end) && end == 700.0f);
    assert(WxlScaleFogRange(100.0f, 400.0f, 1.0f, end) && end == 400.0f);
    assert(WxlScaleFogRange(100.0f, 400.0f, 2.0f, end) && end == 250.0f);
    assert(WxlScaleFogRange(100.0f, 400.0f, 0.0f, end) && end == 1000000.0f);
    end = 123.0f;
    assert(!WxlScaleFogRange(400.0f, 100.0f, 0.5f, end) && end == 123.0f);
    assert(!WxlScaleFogRange(0.0f, 400.0f, -1.0f, end) && end == 123.0f);
    assert(!WxlScaleFogRange(0.0f, 400.0f, std::numeric_limits<float>::quiet_NaN(), end) && end == 123.0f);
    assert(!WxlScaleFogRange(0.0f, std::numeric_limits<float>::infinity(), 1.0f, end) && end == 123.0f);
}
