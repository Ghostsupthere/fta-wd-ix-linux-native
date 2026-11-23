#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace wxl::atmosphere {

struct SpellLight
{
    float position[3];
    float colour[3];
    float distanceSquared;
    float reach = 5.0f;
};

struct SpellLightList
{
    static constexpr unsigned Capacity = 4;
    std::array<SpellLight, Capacity> lights{};
    unsigned count = 0;

    bool Add(const float* position, const float* colour, const float* eye, float alpha)
    {
        const float dx = position[0] - eye[0], dy = position[1] - eye[1], dz = position[2] - eye[2];
        const float squared = dx * dx + dy * dy + dz * dz;
        const float peak = std::max({colour[0], colour[1], colour[2]});
        if (!(squared < 18.0f * 18.0f) || !(peak > 0) || !std::isfinite(peak) || !(alpha > 0))
            return false;
        for (int i = 0; i < 3; ++i)
            if (!(colour[i] >= 0 && colour[i] <= peak))
                return false;
        for (unsigned i = 0; i < count; ++i)
        {
            const auto& other = lights[i];
            const float x = position[0] - other.position[0], y = position[1] - other.position[1];
            const float z = position[2] - other.position[2];
            if (x * x + y * y + z * z < 1.0f)
                return true;
        }
        unsigned index = 0;
        while (index < count && lights[index].distanceSquared <= squared)
            ++index;
        if (index == lights.size())
            return false;
        count = std::min(count + 1, static_cast<unsigned>(lights.size()));
        for (unsigned i = count - 1; i > index; --i)
            lights[i] = lights[i - 1];
        const float strength = 0.55f * std::min(alpha, 1.0f);
        lights[index] = {{position[0], position[1], position[2]},
                         {strength * (colour[0] / peak), strength * (colour[1] / peak), strength * (colour[2] / peak)},
                         squared, 5.0f};
        return true;
    }
};

}
