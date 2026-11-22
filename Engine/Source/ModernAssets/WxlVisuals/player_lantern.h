#pragma once

#include <algorithm>
#include <cmath>

namespace wxl::atmosphere {

struct PlayerLantern
{
    static constexpr float Color[3] = {1.0f, 0.66f, 0.28f};
    static constexpr float SurfaceRadius = 9.0f;
    float position[3] = {};
    float strength = 0;
};

inline PlayerLantern BeltLantern(const float* riderToWorld, float modelHeight, float modelWidth, float strength)
{
    if (!(strength > 0 && strength <= 2))
        return {};

    const float height = modelHeight > 0 ? std::clamp(modelHeight, 0.6f, 4.0f) : 2.0f;
    const float side = modelWidth > 0 ? std::clamp(modelWidth * 0.60f, 0.16f, 0.85f) : height * 0.175f;
    const float waist = height * 0.48f;
    PlayerLantern light;
    for (int i = 0; i < 3; ++i)
    {
        light.position[i] = riderToWorld[4 + i] * side + riderToWorld[8 + i] * waist + riderToWorld[12 + i];
        if (!(std::abs(light.position[i]) < 100000.0f))
            return {};
    }
    light.strength = strength;
    return light;
}

}
