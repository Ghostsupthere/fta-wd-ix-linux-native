#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>

namespace wxl::atmosphere {

enum class FloorSurface { Land, FreshWater, Sea };

struct FloorSample
{
    float height = 0;
    FloorSurface surface = FloorSurface::Land;
};

struct FloorTexel
{
    float height = 0;
    float lake = 0;
    float sea = 0;
    float weight = 0;
    bool operator==(const FloorTexel& other) const
    {
        return height == other.height && lake == other.lake && sea == other.sea && weight == other.weight;
    }
};

class GroundFloor
{
public:
    static constexpr int Side = 32;
    static constexpr int CellCount = Side * Side;
    static constexpr float CellYards = 8;
    static constexpr unsigned ProbeLimit = 16;
    static constexpr double ProbeBudgetMs = 0.15;

    using Probe = bool (*)(int map, float x, float y, FloorSample& sample);
    struct Stats { unsigned probes = 0, misses = 0; };

    Stats Update(int map, const float* camera, double seconds, Probe probe)
    {
        if (map < 0 || !probe || !std::isfinite(seconds) || !std::isfinite(camera[2]) ||
            !(std::abs(camera[0]) < 18000) || !(std::abs(camera[1]) < 18000))
        {
            Reset();
            return {};
        }

        const int x = static_cast<int>(std::floor(camera[0] / CellYards));
        const int y = static_cast<int>(std::floor(camera[1] / CellYards));
        if (map != m_map || seconds < m_seconds || seconds - m_seconds > 8 ||
            std::abs(x - m_x) >= Side / 2 || std::abs(y - m_y) >= Side / 2 ||
            std::abs(camera[2] - m_z) >= Side * CellYards / 2)
            Reset();
        const bool moved = m_map < 0 || m_x != x || m_y != y;
        if (moved)
            m_cursor = 0;
        m_map = map;
        m_x = x;
        m_y = y;
        m_z = camera[2];
        m_seconds = seconds;

        const auto start = std::chrono::steady_clock::now();
        Stats stats;
        bool changed = moved;
        for (unsigned scanned = 0; scanned < 64 && stats.probes < ProbeLimit; ++scanned)
        {
            if (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() >= ProbeBudgetMs)
                break;
            const Offset offset = m_order[m_cursor++ % CellCount];
            const int cx = x + offset.x;
            const int cy = y + offset.y;
            Cell& cell = At(cx, cy);
            const bool same = cell.x == cx && cell.y == cy && cell.used;
            if (same && seconds - cell.sampled < (cell.texel.weight > 0 ? 2.0 : 0.5))
                continue;

            FloorSample sample;
            FloorTexel texel;
            ++stats.probes;
            if (probe(map, (cx + 0.5f) * CellYards, (cy + 0.5f) * CellYards, sample) &&
                std::isfinite(sample.height))
            {
                texel = {sample.height, sample.surface == FloorSurface::FreshWater ? 1.0f : 0.0f,
                         sample.surface == FloorSurface::Sea ? 1.0f : 0.0f, 1.0f};
            }
            else
                ++stats.misses;

            changed |= !(cell.texel == texel);
            cell = {cx, cy, true, seconds, texel};
        }
        if (changed)
            Pack();
        return stats;
    }

    void Reset()
    {
        if (m_map < 0)
            return;
        m_map = -1;
        m_cells = {};
        m_texels = {};
        m_known = 0;
        ++m_version;
    }

    bool HasGrid() const { return m_map >= 0; }
    const FloorTexel* Texels() const { return m_texels.data(); }
    unsigned Known() const { return m_known; }
    uint64_t Version() const { return m_version; }

    void CameraUV(const float* camera, float* uv) const
    {
        uv[0] = (camera[0] / CellYards - (m_x - Side / 2)) / Side;
        uv[1] = (camera[1] / CellYards - (m_y - Side / 2)) / Side;
    }

private:
    struct Offset { int x, y; };
    struct Cell
    {
        int x = 0, y = 0;
        bool used = false;
        double sampled = 0;
        FloorTexel texel;
    };

    inline static constexpr auto m_order = [] {
        std::array<Offset, CellCount> order{};
        unsigned i = 0;
        for (int r = 0; r <= Side / 2; ++r)
            for (int y = -r; y <= r && y < Side / 2; ++y)
                for (int x = -r; x <= r && x < Side / 2; ++x)
                    if (x == -r || x == r || y == -r || y == r)
                        order[i++] = {x, y};
        return order;
    }();

    Cell& At(int x, int y)
    {
        return m_cells[(static_cast<unsigned>(y) & (Side - 1)) * Side + (static_cast<unsigned>(x) & (Side - 1))];
    }

    void Pack()
    {
        m_known = 0;
        for (int y = 0; y < Side; ++y)
            for (int x = 0; x < Side; ++x)
            {
                const int cx = m_x - Side / 2 + x;
                const int cy = m_y - Side / 2 + y;
                const Cell& cell = At(cx, cy);
                auto& texel = m_texels[y * Side + x];
                texel = cell.used && cell.x == cx && cell.y == cy ? cell.texel : FloorTexel{};
                m_known += texel.weight > 0;
            }
        ++m_version;
    }

    std::array<Cell, CellCount> m_cells{};
    std::array<FloorTexel, CellCount> m_texels{};
    int m_map = -1, m_x = 0, m_y = 0;
    float m_z = 0;
    double m_seconds = 0;
    unsigned m_cursor = 0, m_known = 0;
    uint64_t m_version = 0;
};

}
