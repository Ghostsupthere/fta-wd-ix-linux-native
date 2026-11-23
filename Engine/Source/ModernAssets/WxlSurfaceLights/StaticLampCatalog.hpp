#pragma once

#include "LightPool.hpp"
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

inline std::string NormalizeLampPath(std::string_view path) {
    auto name = NormalizePath(path);
    if (name.size() >= 4 && name.compare(name.size() - 4, 4, ".mdx") == 0)
        name.replace(name.size() - 4, 4, ".m2");
    return name;
}

struct StaticLampPlacement {
    float pos[3];
    float radius;
    float strength;
};

inline StaticLampPlacement PlaceStaticLamp(const StaticLamp& lamp, const float world[16]) {
    StaticLampPlacement placement{};
    MulPoint(lamp.offset, world, placement.pos);
    const float scale = std::clamp(MatrixScale(world), 0.75f, 1.5f);
    placement.radius = lamp.radius * scale;
    // Street lamps commonly sit 4–6 yards above street geometry. Compensate
    // for that height in the legacy inverse-distance shader.
    placement.strength = lamp.strength * (lamp.radius >= 10.0f ? 2.5f : 1.0f);
    return placement;
}

inline float StaticLampRadius(float radius, const Settings& settings) {
    return std::clamp(radius * settings.radius / 16.0f, 0.01f, 128.0f);
}

inline unsigned CountGridCells(const Grid& grid, uint16_t index, const ActiveLight& light) {
    if (!grid.cells) return 0;
    const auto bound = [&](float p, float origin) {
        return std::clamp(static_cast<int>(std::floor((p - origin) / grid.cellSize)), 0, grid.cells - 1);
    };
    unsigned cells = 0;
    for (int y = bound(light.pos[1] - light.radius, grid.originY); y <= bound(light.pos[1] + light.radius, grid.originY); ++y)
        for (int x = bound(light.pos[0] - light.radius, grid.originX); x <= bound(light.pos[0] + light.radius, grid.originX); ++x) {
            const size_t cell = static_cast<size_t>(y) * grid.cells + x;
            const auto first = grid.index.begin() + cell * grid.perCell;
            cells += std::find(first, first + grid.count[cell], index) != first + grid.count[cell];
        }
    return cells;
}

inline StaticLampCatalog ReadStaticLampCatalog(std::istream& input) {
    StaticLampCatalog result;
    unsigned lines = 0;
    for (std::string line; lines < 2048 && std::getline(input, line); ++lines) {
        if (line.empty() || line.front() == '#' || line.size() > 512) continue;
        const size_t comma = line.find(',');
        if (comma == std::string::npos) continue;
        const auto name = NormalizeLampPath(std::string_view(line).substr(0, comma));
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
