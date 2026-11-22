#pragma once

#include <cmath>

namespace wxl::atmosphere {

inline float CloudShade(const float* cloud, float x, float y)
{
    const auto coverage = [](float value) {
        const float fraction = value - std::floor(value);
        return 1.0f - std::abs(fraction * 2.0f - 1.0f);
    };
    return 1.0f - cloud[0] * coverage(x * cloud[3] + cloud[1]) * coverage(y * cloud[3] + cloud[2]);
}

}
