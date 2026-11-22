#pragma once

#include "water_contacts.h"
#include <cmath>
#include <cstdint>

inline uint32_t WaterRainHash(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

inline bool WaterRainImpact(const float* centre, float extent, double seconds, float intensity,
                            unsigned index, WaterRippleDisturbance& impact)
{
    const auto tick = static_cast<uint32_t>(static_cast<uint64_t>(std::llround(seconds * 30.0)));
    const uint32_t seed = tick * 2 + index;
    const auto random = [](uint32_t value) { return static_cast<float>(WaterRainHash(value) >> 8) / 16777216.0f; };
    if (!(random(seed ^ 0xa5a5a5a5u) < intensity))
        return false;

    impact = {};
    for (unsigned axis = 0; axis < 2; ++axis)
        impact.from[axis] = impact.to[axis] =
            centre[axis] + (random(seed ^ (0x9e3779b9u * (axis + 1))) - 0.5f) * extent * 0.8f;
    impact.radius = 0.20f;
    impact.amplitude = 0.12f + 0.14f * intensity;
    return true;
}
