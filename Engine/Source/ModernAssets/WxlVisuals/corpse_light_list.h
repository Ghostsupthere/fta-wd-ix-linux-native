#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace wxl::atmosphere {

struct CorpseLight
{
    uint64_t id = 0;
    float position[3] = {};
    float distanceSquared = 0;
};

struct CorpseLightList
{
    static constexpr float Range = 26.0f;
    std::array<CorpseLight, 4> lights{};
    unsigned count = 0;

    void Add(uint64_t id, const float* position, const float* eye)
    {
        const float dx = position[0] - eye[0], dy = position[1] - eye[1], dz = position[2] - eye[2];
        const float squared = dx * dx + dy * dy + dz * dz;
        if (!(squared < Range * Range))
            return;
        unsigned at = 0;
        while (at < count && (lights[at].distanceSquared < squared ||
               (lights[at].distanceSquared == squared && lights[at].id < id)))
            ++at;
        if (at == lights.size())
            return;
        count = std::min(count + 1, static_cast<unsigned>(lights.size()));
        for (unsigned i = count - 1; i > at; --i)
            lights[i] = lights[i - 1];
        lights[at] = {id, {position[0], position[1], position[2] + 0.28f}, squared};
    }
};

}
