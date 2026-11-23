#pragma once

#include "ground_floor.h"

namespace wxl::atmosphere {

using TerrainShade = std::array<uint32_t, GroundFloor::CellCount>;

inline void BuildTerrainShade(const FloorTexel* heights, float sunX, float sunY, float sunZ, TerrainShade& output)
{
    output.fill(0);
    const float horizontal = std::hypot(sunX, sunY);
    if (!(horizontal > 0.05f && sunZ > 0.02f && sunZ < horizontal))
        return;

    constexpr int side = GroundFloor::Side;
    struct Step { int x, y; float rise; };
    std::array<Step, 8> steps;
    for (unsigned i = 0; i < steps.size(); ++i) {
        const float distance = 2.0f * (i + 1);
        const int x = std::lround(distance * sunX / horizontal);
        const int y = std::lround(distance * sunY / horizontal);
        steps[i] = {x, y, std::hypot(float(x), float(y)) * GroundFloor::CellYards * sunZ / horizontal};
    }
    for (int y = 0; y < side; ++y) {
        for (int x = 0; x < side; ++x) {
            const auto& surface = heights[y * side + x];
            if (!surface.weight) continue;
            float coverage = 0;
            for (const Step& step : steps) {
                const int sx = x + step.x, sy = y + step.y;
                if (sx < 0 || sx >= side || sy < 0 || sy >= side) break;
                const auto& blocker = heights[sy * side + sx];
                if (blocker.weight) {
                    const float delta = blocker.height - surface.height - step.rise;
                    coverage = std::max(coverage, std::clamp((delta - 3.0f) / 8.0f, 0.0f, 1.0f));
                }
            }
            // Fade the camera-relative field at its outer four cells.
            const int border = std::min({x, y, side - 1 - x, side - 1 - y});
            const float borderFade = std::min(border * 0.25f, 1.0f);
            output[y * side + x] = uint32_t(coverage * borderFade * 255.0f + 0.5f) << 24;
        }
    }
}

}
