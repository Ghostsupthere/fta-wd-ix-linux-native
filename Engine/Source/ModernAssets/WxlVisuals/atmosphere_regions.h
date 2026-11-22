#pragma once

#include "config.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>

namespace wxl::atmosphere {

using AreaPath = std::array<int, 8>;
using FogMultipliers = std::array<float, 6>;

struct RegionRow {
    int area, time, weather;
    FogMultipliers fog;
};

inline constexpr RegionRow kRegionRows[] = {
#include "atmosphere_regions.inc"
};

class RegionalFog {
public:
    void Reset() { started = false; }
    int ProfileArea() const { return started ? profileArea : 0; }

    void Apply(Config& config, int map, const AreaPath& path, float dayFraction,
               unsigned weather, float intensity, double seconds)
    {
        if (!std::isfinite(dayFraction) || !std::isfinite(seconds))
            return;
        const bool snap = !started || map != lastMap || seconds < lastTime || seconds - lastTime > 2.0;
        if (snap || path != lastPath) {
            Compile(path);
            from = current;
            changedAt = seconds;
        }

        const float hours = (dayFraction - std::floor(dayFraction)) * 24.0f;
        constexpr float times[] = {-2.0f, 5.5f, 12.0f, 19.0f, 22.0f};
        constexpr unsigned slots[] = {3, 0, 1, 2, 3};
        unsigned part = 0;
        while (part < 3 && hours >= times[part + 1])
            ++part;
        const float dayBlend = std::clamp((hours - times[part]) / (times[part + 1] - times[part]), 0.0f, 1.0f);
        const float weatherBlend = std::isfinite(intensity) ? std::clamp(intensity, 0.0f, 1.0f) : 0.0f;
        weather = std::min(weather, 3u);
        const float t = snap ? 1.0f : std::clamp(float((seconds - changedAt) / 4.0), 0.0f, 1.0f);
        constexpr float Config::* fields[] = {
            &Config::density, &Config::haze, &Config::groundFog,
            &Config::farFog, &Config::sunScatter, &Config::ambient,
        };
        for (unsigned field = 0; field < current.size(); ++field) {
            const auto value = [&](unsigned slot) {
                const auto& clear = cells[slot][0];
                return clear[field] + (cells[slot][weather][field] - clear[field]) * weatherBlend;
            };
            const float first = value(slots[part]);
            const float target = first + (value(slots[part + 1]) - first) * dayBlend;
            current[field] = from[field] + (target - from[field]) * (t * t * (3.0f - 2.0f * t));
            config.*fields[field] *= current[field];
        }
        if (snap) {
            from = current;
            changedAt = seconds - 4.0;
        }
        started = true;
        lastMap = map;
        lastPath = path;
        lastTime = seconds;
    }

private:
    void Compile(const AreaPath& path)
    {
        profileArea = 0;
        for (int day = 0; day < 4; ++day) {
            for (int sky = 0; sky < 4; ++sky) {
                auto& cell = cells[day][sky];
                cell.fill(-1.0f);
                for (int area : path) {
                    if (!area)
                        break;
                    const auto begin = std::lower_bound(std::begin(kRegionRows), std::end(kRegionRows),
                        area, [](const RegionRow& row, int key) { return row.area < key; });
                    if (!profileArea && begin != std::end(kRegionRows) && begin->area == area)
                        profileArea = area;
                    const std::array<std::array<int, 2>, 6> order{{
                        {day, sky}, {-1, sky}, {day, -1}, {-1, -1}, {day, 0}, {-1, 0}
                    }};
                    for (const auto& slot : order) {
                        for (auto it = begin; it != std::end(kRegionRows) && it->area == area; ++it) {
                            if (it->time != slot[0] || it->weather != slot[1])
                                continue;
                            for (unsigned field = 0; field < cell.size(); ++field) {
                                if (cell[field] < 0.0f && it->fog[field] >= 0.0f)
                                    cell[field] = it->fog[field];
                            }
                        }
                    }
                }
                for (auto& value : cell) {
                    if (value < 0.0f)
                        value = 1.0f;
                }
            }
        }
    }

    FogMultipliers cells[4][4]{};
    FogMultipliers current{1, 1, 1, 1, 1, 1};
    FogMultipliers from = current;
    AreaPath lastPath{};
    int lastMap = -1;
    int profileArea = 0;
    double changedAt = 0, lastTime = 0;
    bool started = false;
};
}
