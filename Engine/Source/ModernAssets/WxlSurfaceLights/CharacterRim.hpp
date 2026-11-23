#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace wxl::surfacelights {
struct CharacterRim {
    std::array<float, 3> color;
    float strength;
    float exponent;
};

inline bool ReadAuthoredRim(const int32_t (&procedures)[4], const float (&rgb)[4],
                            const float (&strength)[4], const float (&power)[4],
                            const float (&mode)[4], CharacterRim& rim) {
    for (unsigned i = 0; i < 4; ++i) {
        if (procedures[i] != 1000) continue;
        if (!(rgb[i] >= 0 && rgb[i] <= 16777215 && std::floor(rgb[i]) == rgb[i] &&
              strength[i] >= 0 && strength[i] <= 2 && power[i] >= 1 && power[i] <= 8 &&
              (mode[i] == 0 || mode[i] == 1))) return false;
        const auto color = static_cast<uint32_t>(rgb[i]);
        rim = {{{float((color >> 16) & 255) / 255, float((color >> 8) & 255) / 255,
                 float(color & 255) / 255}}, strength[i], power[i]};
        return true;
    }
    return false;
}
}
