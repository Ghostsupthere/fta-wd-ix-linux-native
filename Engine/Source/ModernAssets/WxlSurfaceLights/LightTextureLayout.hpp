#pragma once
// The layout of the shared light texture (supplied wxl-living-azeroth ModelLights.cpp, FillGrid and
// Fill): one A32B32G32R32F texture, 1024 texels per row, bound on vertex sampler 0 for the M2/WMO
// vertex shaders and on pixel sampler 13 for the terrain. The patched shaders read it with texldl,
// so these offsets must match ModelLightShaderText.hpp / TerrainLightShaderText.hpp exactly.
//
//  row 0, the header (12 texels):
//   t0  (active/stock lights off, 0, 0, valid)      t1  camera position, .w the occlusion region's first row + 0.5
//   t2..t4 the scene view's columns (camera-relative world -> view, row-vector convention)
//   t5  debug-view weights     t6  (1/W, 1/H, lights per cell, 0)     t7  (1, 0, 0.5, 2/3)
//   t8  (0.7, 0.03, 1/ln2, 0.05)     t9  (0.8, 0.001, 0.25, 1/lights per cell)
//   t10 (scale of ours on baked-light WMO surfaces, 0, 0, 0)     t11 the grid (x, y of cell 0, 1/cell size, cells)
//  rows 1..: the cells' entries, row-major: cell c's k-th light at linear texel c * perCell + k, each
//   (u, v of the light's first texel, 1, 0); (0, 0, 0, 0) ends a cell's list.
//  then the light pool, 4 texels each, never split across rows: world position + 1/radius; colour +
//   falloff shape; spot direction + cos outer; 1/(cos inner - cos outer), occlusion tile, flicker dip,
//   1 = indoor.
//  then the baked occlusion tiles: light p's tile (kOccCells distances, 4 per texel) at linear texel
//   p x kOccTexels of this region; its 4th texel .y = p / kTilesPerRow, so the shaders take the row from
//   the whole part and the tile's u from the fraction (or -1: none).
#include "LightPool.hpp"

#include <cstring>
#include <vector>

namespace wxl::surfacelights::texture
{
    constexpr int kHeaderTexels = 12;
    constexpr int kTexelsPerLight = 4;
    constexpr int kWidth = 1024;
    constexpr int kPoolRows = (kMaxPool * kTexelsPerLight + kWidth - 1) / kWidth;
    static_assert(kWidth % kTexelsPerLight == 0, "a light's texels must share a row");
    constexpr int kOccTexels = kOccCells / 4;
    constexpr int kTilesPerRow = kWidth / kOccTexels;
    constexpr int kOccRows = (kMaxPool + kTilesPerRow - 1) / kTilesPerRow;
    static_assert(kWidth % kOccTexels == 0, "a tile must not split across rows");

    struct Geometry
    {
        int entryRows = 0;
        int height = 0;
        int PoolRow() const { return 1 + entryRows; }
        int OccRow() const { return 1 + entryRows + kPoolRows; }

        static Geometry For(const Grid& grid)
        {
            Geometry g;
            const int entries = grid.cells * grid.cells * grid.perCell;
            g.entryRows = (entries + kWidth - 1) / kWidth;
            g.height = 1 + g.entryRows + kPoolRows + kOccRows;
            return g;
        }
    };

    // `t` points at the first texel of the first locked row; `pitch` is the row pitch in floats.
    inline void ZeroRows(float* t, int pitch, int rows)
    {
        for (int row = 0; row < rows; ++row) std::memset(t + static_cast<size_t>(row) * pitch, 0, kWidth * 4 * sizeof(float));
    }

    // Rows 1..entryRows: where each cell's lights start.
    inline void WriteEntries(float* t, int pitch, const Grid& grid, const Geometry& geometry)
    {
        ZeroRows(t, pitch, geometry.entryRows);
        const size_t cellCount = static_cast<size_t>(grid.cells) * grid.cells;
        for (size_t c = 0; c < cellCount; ++c)
            for (int k = 0; k < grid.count[c]; ++k)
            {
                const size_t entry = c * grid.perCell + k;
                const unsigned light = grid.index[entry];
                float* o = t + (entry / kWidth) * pitch + (entry % kWidth) * 4;
                const unsigned texel = light * kTexelsPerLight;
                o[0] = (static_cast<float>(texel % kWidth) + 0.5f) / kWidth;
                o[1] = (static_cast<float>(geometry.PoolRow() + texel / kWidth) + 0.5f) / geometry.height;
                o[2] = 1.0f;
            }
    }

    // The pool rows; returns how many lights were written (at most kMaxPool).
    inline unsigned WritePool(float* t, int pitch, const std::vector<ActiveLight>& pool)
    {
        ZeroRows(t, pitch, kPoolRows);
        unsigned count = 0;
        for (const ActiveLight& l : pool)
        {
            if (count >= static_cast<unsigned>(kMaxPool)) break;
            const unsigned texel = count * kTexelsPerLight;
            float* o = t + (texel / kWidth) * pitch + (texel % kWidth) * 4;
            o[0] = l.pos[0]; o[1] = l.pos[1]; o[2] = l.pos[2];
            o[3] = l.radius > 0.0f ? 1.0f / l.radius : 0.0f;
            o[4] = l.color[0]; o[5] = l.color[1]; o[6] = l.color[2]; o[7] = l.falloff;
            o[8] = l.spotDir[0]; o[9] = l.spotDir[1]; o[10] = l.spotDir[2]; o[11] = l.cosOuter;
            o[12] = l.spotScale; o[14] = l.dip;
            o[13] = l.occlusion ? static_cast<float>(count) / kTilesPerRow : -1.0f;
            o[15] = l.indoor ? 1.0f : 0.0f;
            ++count;
        }
        return count;
    }

    // The occlusion tiles of the first `count` lights of the pool.
    inline void WriteOcclusion(float* t, int pitch, const std::vector<ActiveLight>& pool, unsigned count)
    {
        ZeroRows(t, pitch, kOccRows);
        for (unsigned p = 0; p < count && p < pool.size(); ++p)
        {
            const float* occ = pool[p].occlusion;
            if (!occ) continue;
            const unsigned first = p * kOccTexels;
            float* o = t + (first / kWidth) * pitch + (first % kWidth) * 4;
            std::memcpy(o, occ, kOccCells * sizeof(float));
        }
    }

    struct HeaderInput
    {
        bool active = false;
        bool stockOff = true;
        float camera[3] = {};
        float view[16] = {};       // camera-relative world -> view, row-vector convention
        int debugView = 0;
        const Grid* grid = nullptr;
        const Geometry* geometry = nullptr;
        float bakedAdd = 1.0f;
    };

    // Row 0. Inactive = all zero: the patched shaders then keep the client's own lighting.
    inline void WriteHeader(float* t, const HeaderInput& in)
    {
        std::memset(t, 0, kWidth * 4 * sizeof(float));
        if (!in.active || !in.grid || !in.geometry) return;
        const Grid& grid = *in.grid;
        t[0] = in.stockOff ? 1.0f : 0.0f;
        t[3] = 1.0f;
        t[4] = in.camera[0]; t[5] = in.camera[1]; t[6] = in.camera[2];
        t[7] = in.geometry->OccRow() + 0.5f;
        for (int col = 0; col < 3; ++col)
            for (int row = 0; row < 4; ++row) t[(2 + col) * 4 + row] = in.view[row * 4 + col];
        if (in.debugView >= 1 && in.debugView <= 3) { t[5 * 4 + (in.debugView - 1)] = 1.0f; t[5 * 4 + 3] = 1.0f; }
        if (in.debugView == 4) t[2] = 1.0f;
        if (in.debugView == 5) t[1] = 1.0f;
        const float perCell = static_cast<float>(grid.perCell);
        const float k[16] = {
            1.0f / kWidth, 1.0f / in.geometry->height, perCell, 0.0f,
            1.0f, 0.0f, 0.5f, 2.0f / 3.0f,
            0.7f, 0.03f, 1.44269502f, 0.05f,
            0.8f, 0.001f, 0.25f, grid.perCell > 0 ? 1.0f / perCell : 0.0f};
        std::memcpy(t + 6 * 4, k, sizeof(k));
        t[10 * 4] = in.bakedAdd;
        const float g[4] = {grid.originX, grid.originY, 1.0f / grid.cellSize, static_cast<float>(grid.cells)};
        std::memcpy(t + 11 * 4, g, sizeof(g));
    }

    // The pixel-shader grid constants (terrain): c206 = (corner x, y, 1/cell size, cells),
    // c212 = (1/height, lights per cell, cells - 1, occlusion region row + 0.5). Zero lights per cell
    // while the grid isn't filled, so the terrain loop ends at once.
    inline void GridConstants(const Grid& grid, const Geometry& geometry, bool on, float c206[4], float c212[4])
    {
        const bool live = on && grid.cells > 0;
        c206[0] = grid.originX; c206[1] = grid.originY;
        c206[2] = grid.cellSize > 0.0f ? 1.0f / grid.cellSize : 0.0f;
        c206[3] = live ? static_cast<float>(grid.cells) : 0.0f;
        c212[0] = geometry.height > 0 ? 1.0f / geometry.height : 0.0f;
        c212[1] = live ? static_cast<float>(grid.perCell) : 0.0f;
        c212[2] = live ? static_cast<float>(grid.cells - 1) : 0.0f;
        c212[3] = geometry.OccRow() + 0.5f;
    }
}
