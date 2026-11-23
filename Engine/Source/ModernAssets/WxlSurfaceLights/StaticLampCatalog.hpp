#pragma once

#include "DoodadLightTables.hpp"
#include <array>
#include <cmath>
#include <istream>
#include <sstream>
#include <string>
#include <unordered_map>

namespace wxl::surfacelights {

struct StaticLamp {
    float offset[3];
    float color[3];
    float radius;
    float strength;
};

using StaticLampCatalog = std::unordered_map<std::string, StaticLamp>;

inline StaticLampCatalog ReadStaticLampCatalog(std::istream& input) {
    StaticLampCatalog result;
    unsigned lines = 0;
    for (std::string line; lines < 2048 && std::getline(input, line); ++lines) {
        if (line.empty() || line.front() == '#' || line.size() > 512) continue;
        const size_t comma = line.find(',');
        if (comma == std::string::npos) continue;
        const auto name = NormalizePath(std::string_view(line).substr(0, comma));
        if (name.compare(0, 6, "world\\") || name.size() < 10 || name.compare(name.size() - 3, 3, ".m2")) continue;
        std::istringstream values(line.substr(comma + 1));
        StaticLamp lamp{};
        std::array<float*, 8> fields{&lamp.offset[0], &lamp.offset[1], &lamp.offset[2],
            &lamp.color[0], &lamp.color[1], &lamp.color[2], &lamp.radius, &lamp.strength};
        bool valid = true;
        for (size_t i = 0; i < fields.size(); ++i) {
            char separator = ',';
            if (!(values >> *fields[i]) || !std::isfinite(*fields[i]) ||
                (i + 1 < fields.size() && (!(values >> separator) || separator != ','))) {
                valid = false;
                break;
            }
        }
        values >> std::ws;
        if (!valid || !values.eof() || !(lamp.radius >= 1 && lamp.radius <= 32) ||
            !(lamp.strength > 0 && lamp.strength <= 2)) continue;
        for (unsigned axis = 0; axis < 3; ++axis)
            valid &= std::abs(lamp.offset[axis]) < 100 && lamp.color[axis] >= 0 && lamp.color[axis] <= 1;
        if (valid) result.try_emplace(name, lamp);
    }
    return result;
}

}
