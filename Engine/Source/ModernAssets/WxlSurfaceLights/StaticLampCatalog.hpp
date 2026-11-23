#pragma once

#include "LightPool.hpp"
#include <array>
#include <charconv>
#include <cmath>
#include <istream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace wxl::surfacelights {

struct StaticLamp {
    float offset[3];
    float color[3];
    float radius;
    float strength;
};

using StaticLampCatalog = std::unordered_map<std::string, StaticLamp>;
struct WmoLamp {
    unsigned group;
    StaticLamp lamp;
};
using StaticWmoLampCatalog = std::unordered_map<std::string, std::vector<WmoLamp>>;

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

// Map placements carry the authored transform. Visit them first so that a
// model found in both owner lists can only submit one light.
template <class Model, class Visit>
void VisitStaticPlacements(const std::vector<Model>& map, const std::vector<Model>& scene, Visit visit) {
    std::unordered_set<const void*> mapped;
    mapped.reserve(map.size());
    for (const auto& model : map)
        if (mapped.insert(model.model).second) visit(model);
    for (const auto& model : scene)
        if (mapped.find(model.model) == mapped.end()) visit(model);
}

inline float StaticLampScore(const StaticLampPlacement& lamp, const float eye[3], const float forward[3]) {
    const float x = lamp.pos[0] - eye[0], y = lamp.pos[1] - eye[1], z = lamp.pos[2] - eye[2];
    const float d2 = x*x + y*y + z*z;
    if (!std::isfinite(d2) || !(lamp.radius > 0) || !std::isfinite(lamp.radius)) return 0;
    const float toward = (x*forward[0] + y*forward[1] + z*forward[2]) / std::sqrt(std::max(d2, 1.0f));
    return lamp.strength * lamp.radius * (1.0f + 0.5f * std::clamp(toward, 0.0f, 1.0f)) / std::max(d2, 4.0f);
}

struct StaticLampRank {
    float score;
    float distance;
    bool eligible;

    bool operator<(const StaticLampRank& other) const {
        if (eligible != other.eligible) return eligible;
        if (score != other.score) return score > other.score;
        return distance < other.distance;
    }
};

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

inline StaticLampCatalog ReadStaticLampCatalog(std::istream& input, StaticWmoLampCatalog* mesh = nullptr) {
    StaticLampCatalog result;
    unsigned lines = 0;
    for (std::string line; lines < 2048 && std::getline(input, line); ++lines) {
        if (line.empty() || line.front() == '#' || line.size() > 512) continue;
        const size_t comma = line.find(',');
        if (comma == std::string::npos) continue;
        const auto key = std::string_view(line).substr(0, comma);
        const size_t hash = key.find('#');
        const bool isMesh = hash != std::string_view::npos;
        auto name = NormalizeLampPath(key.substr(0, hash));
        if (name.compare(0, 6, "world\\") || name.size() < 10) continue;
        unsigned group = 0;
        if (isMesh) {
            if (!mesh || name.compare(name.size() - 4, 4, ".wmo")) continue;
            const auto id = key.substr(hash + 1);
            const auto parsed = std::from_chars(id.data(), id.data() + id.size(), group);
            if (parsed.ec != std::errc{} || parsed.ptr != id.data() + id.size() || group >= 512) continue;
        } else if (name.compare(name.size() - 3, 3, ".m2")) {
            continue;
        }
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
            valid &= std::abs(lamp.offset[axis]) < (isMesh ? 10000 : 100) && lamp.color[axis] >= 0 && lamp.color[axis] <= 1;
        if (valid && isMesh) (*mesh)[std::move(name)].push_back({group, lamp});
        else if (valid) result.try_emplace(std::move(name), lamp);
    }
    return result;
}

}
