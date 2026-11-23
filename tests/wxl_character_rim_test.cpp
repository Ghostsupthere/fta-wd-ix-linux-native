#include "Engine/Source/ModernAssets/WxlSurfaceLights/CharacterRim.hpp"
#include <cassert>
#include <limits>

int main() {
    using namespace wxl::surfacelights;
    CharacterRim rim{{1, 1, 1}, .35f, 4};
    int32_t procedures[4] = {1001, 0, 0, 0};
    float colors[4] = {0, 0xFF8040, 0, 0};
    float strength[4] = {0, .75f, 0, 0};
    float power[4] = {0, 6, 0, 0};
    float mode[4] = {};
    assert(!ReadAuthoredRim(procedures, colors, strength, power, mode, rim));
    assert(rim.strength == .35f);
    procedures[1] = 1000;
    assert(ReadAuthoredRim(procedures, colors, strength, power, mode, rim));
    assert(rim.color[0] == 1 && rim.color[1] == 128.0f/255 && rim.strength == .75f && rim.exponent == 6);
    power[1] = std::numeric_limits<float>::quiet_NaN();
    assert(!ReadAuthoredRim(procedures, colors, strength, power, mode, rim));
    power[1] = 4;
    mode[1] = 20;
    assert(!ReadAuthoredRim(procedures, colors, strength, power, mode, rim));
}
