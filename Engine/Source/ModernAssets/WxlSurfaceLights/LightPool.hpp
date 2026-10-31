#pragma once
// The platform-independent core of the supplied wxl-living-azeroth light list (env/Lights.cpp):
// flame flicker, merging of nearby torches, the in-range pool with range fade, the clustered cell
// grid and the baked-occlusion cache. Everything that reads the engine (scene models, the client's
// own CM2Light values, attachments, ray casts, indoor tests) stays outside and reaches this code
// as plain candidates and callbacks, so the logic runs under the sanitizer tests.
// Adapted from Morfium's Living Azeroth (GPL-3.0-or-later).
#include "DoodadLightTables.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace wxl::surfacelights
{
    constexpr int kMaxPool = 512;        // lights in range, in all
    constexpr int kMaxPerCellCap = 32;   // the shaders' loop bound
    constexpr int kMaxCells = 64;        // per side
    constexpr int kOccSide = 8;          // directions per side of a light's octahedral tile
    constexpr int kOccCells = kOccSide * kOccSide;

    struct Settings
    {
        int enabled = 0;
        float radius = 16.0f;        // yd: the light is exactly 0 here
        float brightness = 1.0f;
        float range = 256.0f;        // yd from the camera, fading over the last third
        float cellSize = 16.0f;
        int maxPerCell = 16;
        float mergeDistance = 1.0f;
        float flicker = 0.15f;
        float flickerSpeed = 1.0f;
        int models = 1;
        int modelStockOff = 1;
        float bakedAdd = 1.0f;
        int indoorSkipsTerrain = 1;
        int bakeAll = 0;
        int bakeRaysPerFrame = 2000;
        int bakeEnabled = 1;
    };

    // One light as the renderers get it.
    struct ActiveLight
    {
        float pos[3] = {};
        float color[3] = {};
        float radius = 0.0f;
        float falloff = 1.0f;
        float spotDir[3] = {0.0f, 0.0f, -1.0f};
        float cosOuter = -2.0f;   // point lights: -2 (always inside)
        float spotScale = 1.0f;
        float dip = 0.0f;
        bool indoor = false;
        const void* model = nullptr;
        const float* occlusion = nullptr;
    };

    // A light this frame, before merging and choosing.
    struct Candidate
    {
        float pos[3] = {};
        float color[3] = {};
        float weight = 1.0f;
        float radius = -1.0f, falloff = -1.0f;
        float spotDir[3] = {0.0f, 0.0f, -1.0f};
        float cosOuter = -2.0f, spotScale = 1.0f;
        int flickerMode = kFlickerDefault;
        float flickerSpeed = -1.0f, flickerAmount = -1.0f;
        float scale = 1.0f;
        const void* model = nullptr;
        bool bake = false;
        bool moving = false;
    };

    struct Grid
    {
        float originX = 0.0f, originY = 0.0f, cellSize = 16.0f;
        int cells = 0, perCell = 0;
        std::vector<uint16_t> index;
        std::vector<uint8_t> count;
        unsigned generation = 0;
    };

    struct GridStats { unsigned busiest = 0, full = 0, dropped = 0; };

    namespace detail
    {
        inline uint32_t Hash(uint32_t x) { x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16; return x; }
        inline float Unit(uint32_t x) { return static_cast<float>(Hash(x) >> 8) * (1.0f / 16777216.0f); }
    }

    // Flame flicker, 0..1 (how much it dips); each light its own phase from its position.
    inline float Flicker(int mode, const float pos[3], double time)
    {
        const double phase = std::fmod(std::fabs(pos[0] * 12.9898 + pos[1] * 78.233 + pos[2] * 37.719), 1000.0);
        const double t = time + phase;
        if (mode == kFlickerOff) return 0.0f;
        if (mode == kFlickerNoise || mode == kFlickerSteps)
        {
            const double u = t * 6.0;
            const uint32_t seed = static_cast<uint32_t>(phase * 1000.0);
            const uint32_t k = static_cast<uint32_t>(static_cast<int64_t>(std::floor(u)));
            const float a = detail::Unit(k * 2654435761u + seed), b = detail::Unit((k + 1) * 2654435761u + seed);
            if (mode == kFlickerSteps) return a;
            float f = static_cast<float>(u - std::floor(u));
            f = f * f * (3.0f - 2.0f * f);
            return a + (b - a) * f;
        }
        const double s = 0.5 * std::sin(t * 7.13) + 0.3 * std::sin(t * 13.71 + 1.3) + 0.2 * std::sin(t * 23.17 + 2.1);
        return static_cast<float>(0.5 + 0.5 * s);
    }

    // The distance at which 1 / (a + b d + c d^2) drops to `share` of its value at 1 yd (-1 if never).
    inline float Reach(const float att[3], float share)
    {
        const float a = att[0], b = att[1], c = att[2];
        const float atOne = a + b + c;
        if (atOne <= 0.0f || share <= 0.0f) return -1.0f;
        const float k = atOne / share;
        if (c > 1e-6f)
        {
            const float disc = b * b - 4.0f * c * (a - k);
            return disc < 0.0f ? -1.0f : (-b + std::sqrt(disc)) / (2.0f * c);
        }
        return b > 1e-6f ? (k - a) / b : -1.0f;
    }

    // Matrix helpers (row-vector convention, 16 floats).
    inline void MulPoint(const float v[3], const float m[16], float out[3])
    {
        for (int c = 0; c < 3; ++c) out[c] = v[0] * m[c] + v[1] * m[4 + c] + v[2] * m[8 + c] + m[12 + c];
    }
    inline void MulDirection(const float v[3], const float m[16], float out[3])
    {
        for (int c = 0; c < 3; ++c) out[c] = v[0] * m[c] + v[1] * m[4 + c] + v[2] * m[8 + c];
        const float len = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
        if (len > 1e-6f) for (int c = 0; c < 3; ++c) out[c] /= len;
        else { out[0] = 0.0f; out[1] = 0.0f; out[2] = -1.0f; }
    }
    inline float MatrixScale(const float m[16]) { return std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]); }

    // A table light (DoodadLightAssignment) as a source, fixed at the scan. Returns the properties'
    // colour x intensity, the spot cone and the flicker settings; the caller places it.
    struct TableLight
    {
        float color[3] = {};
        float radius = -1.0f, falloff = -1.0f;
        float spotDir[3] = {0.0f, 0.0f, -1.0f};
        float cosOuter = -2.0f, spotScale = 1.0f;
        int flickerMode = kFlickerDefault;
        float flickerSpeed = -1.0f, flickerAmount = -1.0f;
        bool bake = false;
    };

    inline TableLight MakeTableLight(const Assignment& a, const Properties& p, const float world[16])
    {
        TableLight t;
        t.bake = (p.flags & kBakeOcclusion) != 0;
        t.color[0] = ((p.color >> 16) & 0xFF) / 255.0f * p.intensity;
        t.color[1] = ((p.color >> 8) & 0xFF) / 255.0f * p.intensity;
        t.color[2] = (p.color & 0xFF) / 255.0f * p.intensity;
        t.radius = p.radius;
        t.falloff = p.falloff;
        if (p.type == kSpot)
        {
            MulDirection(a.direction, world, t.spotDir);
            constexpr float kRad = 3.14159265f / 180.0f;
            const float inner = std::cos(std::min(p.innerAngle, p.outerAngle) * kRad);
            t.cosOuter = std::cos(p.outerAngle * kRad);
            t.spotScale = inner - t.cosOuter > 1e-4f ? 1.0f / (inner - t.cosOuter) : 1e4f;
        }
        t.flickerMode = p.flickerMode;
        t.flickerSpeed = p.flickerSpeed;
        t.flickerAmount = p.flickerAmount;
        return t;
    }

    // Merges point lights closer than `mergeDistance` (torch groups): summed colour, position
    // weighted by brightness, the larger radius. Spot lights never merge. Merged-away candidates get
    // weight 0. Neighbours come from a grid of merge-distance cells. Returns how many were merged.
    inline unsigned MergeCandidates(std::vector<Candidate>& cands, float mergeDistance)
    {
        const float md = mergeDistance, md2 = md * md;
        const float inv = md > 1e-3f ? 1.0f / md : 0.0f;
        if (inv <= 0.0f) return 0;
        std::vector<std::pair<uint64_t, uint32_t>> cells;
        std::vector<float> original(cands.size() * 3);
        const auto cellOf = [&](const float p[3], int out[3]) { for (int k = 0; k < 3; ++k) out[k] = static_cast<int>(std::floor(p[k] * inv)); };
        const auto keyOf = [](int x, int y, int z)
        {
            return (static_cast<uint64_t>(x + 1048576) & 0x1FFFFF) | ((static_cast<uint64_t>(y + 1048576) & 0x1FFFFF) << 21) |
                   ((static_cast<uint64_t>(z + 1048576) & 0x1FFFFF) << 42);
        };
        for (size_t k = 0; k < cands.size(); ++k)
        {
            std::memcpy(&original[k * 3], cands[k].pos, 3 * sizeof(float));
            if (cands[k].cosOuter > -1.5f) continue;
            int c[3];
            cellOf(cands[k].pos, c);
            cells.push_back({keyOf(c[0], c[1], c[2]), static_cast<uint32_t>(k)});
        }
        std::sort(cells.begin(), cells.end());
        unsigned merged = 0;
        for (size_t a = 0; a < cands.size(); ++a)
        {
            if (cands[a].weight <= 0.0f || cands[a].cosOuter > -1.5f) continue;
            float wa = cands[a].color[0] + cands[a].color[1] + cands[a].color[2];
            int ca[3];
            cellOf(&original[a * 3], ca);
            for (int nz = -1; nz <= 1; ++nz)
            for (int ny = -1; ny <= 1; ++ny)
            for (int nx = -1; nx <= 1; ++nx)
            {
                const uint64_t key = keyOf(ca[0] + nx, ca[1] + ny, ca[2] + nz);
                auto it = std::lower_bound(cells.begin(), cells.end(), std::make_pair(key, 0u));
                for (; it != cells.end() && it->first == key; ++it)
                {
                    const size_t b = it->second;
                    if (b <= a || cands[b].weight <= 0.0f) continue;
                    const float* pa = &original[a * 3];
                    const float* pb = &original[b * 3];
                    const float dx = pa[0] - pb[0], dy = pa[1] - pb[1], dz = pa[2] - pb[2];
                    if (dx * dx + dy * dy + dz * dz > md2) continue;
                    const float wb = cands[b].color[0] + cands[b].color[1] + cands[b].color[2];
                    const float share = wa + wb > 0.0f ? wb / (wa + wb) : 0.5f;
                    for (int k = 0; k < 3; ++k)
                    {
                        cands[a].pos[k] += (cands[b].pos[k] - cands[a].pos[k]) * share;
                        cands[a].color[k] += cands[b].color[k];
                    }
                    cands[a].radius = std::max(cands[a].radius, cands[b].radius);
                    cands[a].scale = std::max(cands[a].scale, cands[b].scale);
                    cands[a].bake = cands[a].bake || cands[b].bake;
                    cands[a].moving = cands[a].moving || cands[b].moving;
                    wa += wb;
                    cands[b].weight = 0.0f;
                    ++merged;
                }
            }
        }
        return merged;
    }

    struct ChosenPool
    {
        std::vector<ActiveLight> active;
        std::vector<float> baseLuminance;   // per light: brightness before fade and flicker
        std::vector<char> wantBake;
        unsigned poolDropped = 0;
        unsigned indoor = 0;
    };

    // Every candidate still in play, fading out over the last third of the range, with flicker.
    // They stay in the candidates' order (stable between scans, so the grid doesn't reshuffle every
    // frame); only past kMaxPool do the farthest from the eye drop out. `indoor(pos)` answers whether
    // a light sits inside an indoor WMO group (called only when the setting asks for it).
    inline ChosenPool ChooseActive(const std::vector<Candidate>& cands, const Settings& settings, const float eye[3],
                                   double time, const std::function<bool(const float*)>& indoor)
    {
        ChosenPool out;
        const float range = settings.range;
        std::vector<std::pair<float, size_t>> order;
        for (size_t k = 0; k < cands.size(); ++k)
            if (cands[k].weight > 0.0f)
            {
                const float dx = cands[k].pos[0] - eye[0], dy = cands[k].pos[1] - eye[1], dz = cands[k].pos[2] - eye[2];
                order.push_back({dx * dx + dy * dy + dz * dz, k});
            }
        if (order.size() > static_cast<size_t>(kMaxPool))
        {
            out.poolDropped = static_cast<unsigned>(order.size() - kMaxPool);
            std::nth_element(order.begin(), order.begin() + kMaxPool, order.end());
            order.resize(kMaxPool);
            std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
        }
        const float fadeStart = range * (2.0f / 3.0f);
        for (const auto& entry : order)
        {
            const Candidate& c = cands[entry.second];
            const float d = std::sqrt(entry.first);
            const float fade = d <= fadeStart ? 1.0f : std::max(0.0f, 1.0f - (d - fadeStart) / (range - fadeStart));
            const int mode = c.flickerMode == kFlickerDefault ? kFlickerSmooth : c.flickerMode;
            const float amount = std::clamp(c.flickerAmount >= 0.0f ? c.flickerAmount * (settings.flicker / 0.15f) : settings.flicker, 0.0f, 1.0f);
            const float speed = c.flickerSpeed >= 0.0f ? c.flickerSpeed : 1.0f;
            const float flicker = 1.0f - amount * Flicker(mode, c.pos, time * speed * settings.flickerSpeed);
            ActiveLight l;
            std::memcpy(l.pos, c.pos, sizeof(l.pos));
            for (int k = 0; k < 3; ++k) l.color[k] = c.color[k] * settings.brightness * fade * flicker;
            l.radius = std::clamp((c.radius > 0.0f ? c.radius * settings.radius / 16.0f : settings.radius) * c.scale, 0.01f, 128.0f);
            l.falloff = c.falloff > 0.0f ? c.falloff : 1.0f;
            std::memcpy(l.spotDir, c.spotDir, sizeof(l.spotDir));
            l.cosOuter = c.cosOuter;
            l.spotScale = c.spotScale;
            l.dip = 1.0f - flicker;
            l.model = c.model;
            l.indoor = settings.indoorSkipsTerrain && indoor && indoor(l.pos);
            out.indoor += l.indoor ? 1 : 0;
            out.active.push_back(l);
            out.baseLuminance.push_back((c.color[0] + c.color[1] + c.color[2]) * settings.brightness);
            out.wantBake.push_back(!c.moving && (c.bake || settings.bakeAll) ? 1 : 0);
        }
        return out;
    }

    // The clustered list: a grid of cells around the camera, each listing the lights that reach it.
    // The grid snaps to whole cells around the eye, so a cell only changes when lights do. A light
    // goes into every cell its radius reaches (circle vs square). A cell holding more than perCell
    // keeps the strongest at its centre: base brightness x (1 - d / reach)^2.
    class GridBuilder
    {
    public:
        // Rebuilds `grid` when anything that decides membership or ranking changed. Returns whether
        // `grid` was touched (false = unchanged, flicker and view rotation don't matter).
        bool Build(const std::vector<ActiveLight>& active, const std::vector<float>& baseLuminance, const float eye[3],
                   const Settings& settings, Grid& grid, GridStats& stats)
        {
            Grid next;
            next.cellSize = std::max(4.0f, settings.cellSize);
            next.perCell = std::max(1, std::min(settings.maxPerCell, kMaxPerCellCap));
            const int half = std::min(static_cast<int>(std::ceil(settings.range / next.cellSize)), kMaxCells / 2);
            next.cells = std::max(1, half * 2);
            next.originX = (std::floor(eye[0] / next.cellSize) - half) * next.cellSize;
            next.originY = (std::floor(eye[1] / next.cellSize) - half) * next.cellSize;
            bool unchanged = next.cells == grid.cells && next.perCell == grid.perCell && next.cellSize == grid.cellSize &&
                             next.originX == grid.originX && next.originY == grid.originY && inputs_.size() == active.size();
            if (unchanged)
                for (size_t i = 0; i < active.size(); ++i)
                {
                    const auto& old = inputs_[i];
                    const auto& light = active[i];
                    if (old.x != light.pos[0] || old.y != light.pos[1] || old.radius != light.radius ||
                        old.luminance != baseLuminance[i]) { unchanged = false; break; }
                }
            if (unchanged)
            {
                stats = last_;
                return false;
            }
            inputs_.clear();
            inputs_.reserve(active.size());
            for (size_t i = 0; i < active.size(); ++i)
                inputs_.push_back({active[i].pos[0], active[i].pos[1], active[i].radius, baseLuminance[i]});
            const size_t cellCount = static_cast<size_t>(next.cells) * next.cells;
            next.count.assign(cellCount, 0);
            next.index.assign(cellCount * next.perCell, 0);

            entries_.clear();
            const float cs = next.cellSize, halfDiag = cs * 0.70710678f;
            for (size_t i = 0; i < active.size(); ++i)
            {
                const ActiveLight& l = active[i];
                const float r = l.radius;
                if (r <= 0.0f) continue;
                const float lx = l.pos[0] - next.originX, ly = l.pos[1] - next.originY;
                const int x0 = std::max(0, static_cast<int>(std::floor((lx - r) / cs)));
                const int y0 = std::max(0, static_cast<int>(std::floor((ly - r) / cs)));
                const int x1 = std::min(next.cells - 1, static_cast<int>(std::floor((lx + r) / cs)));
                const int y1 = std::min(next.cells - 1, static_cast<int>(std::floor((ly + r) / cs)));
                for (int y = y0; y <= y1; ++y)
                    for (int x = x0; x <= x1; ++x)
                    {
                        const float nx = std::max(x * cs, std::min(lx, (x + 1) * cs)) - lx;
                        const float ny = std::max(y * cs, std::min(ly, (y + 1) * cs)) - ly;
                        if (nx * nx + ny * ny > r * r) continue;
                        const float cx = (x + 0.5f) * cs - lx, cy = (y + 0.5f) * cs - ly;
                        const float t = 1.0f - std::min(std::sqrt(cx * cx + cy * cy) / (r + halfDiag), 1.0f);
                        entries_.push_back({static_cast<uint32_t>(y * next.cells + x), baseLuminance[i] * t * t, static_cast<uint16_t>(i)});
                    }
            }
            std::sort(entries_.begin(), entries_.end(), [](const CellEntry& a, const CellEntry& b)
                      { return a.cell != b.cell ? a.cell < b.cell : (a.score != b.score ? a.score > b.score : a.light < b.light); });

            GridStats s;
            for (size_t e = 0; e < entries_.size();)
            {
                const uint32_t c = entries_[e].cell;
                unsigned n = 0;
                for (; e < entries_.size() && entries_[e].cell == c; ++e, ++n)
                    if (n < static_cast<unsigned>(next.perCell)) next.index[c * next.perCell + n] = entries_[e].light;
                s.busiest = std::max(s.busiest, n);
                if (n > static_cast<unsigned>(next.perCell)) { ++s.full; s.dropped += n - next.perCell; }
                next.count[c] = static_cast<uint8_t>(std::min(n, static_cast<unsigned>(next.perCell)));
            }
            last_ = stats = s;
            const bool same = next.cells == grid.cells && next.perCell == grid.perCell && next.originX == grid.originX &&
                              next.originY == grid.originY && next.cellSize == grid.cellSize &&
                              next.count == grid.count && next.index == grid.index;
            next.generation = same ? grid.generation : grid.generation + 1;
            grid = std::move(next);
            return true;
        }

        void Reset() { inputs_.clear(); last_ = {}; }

    private:
        struct CellEntry { uint32_t cell; float score; uint16_t light; };
        struct Input { float x, y, radius, luminance; };
        std::vector<Input> inputs_;
        std::vector<CellEntry> entries_;
        GridStats last_;
    };

    // Octahedral map: (u, v) in [-1, 1] -> unit direction. The shaders encode the other way:
    // n = d / (|x| + |y| + |z|), lower half folded with (1 - |n.yx|) * sign(n.xy).
    inline void OctDecode(float u, float v, float out[3])
    {
        float x = u, y = v, z = 1.0f - std::fabs(u) - std::fabs(v);
        const float t = std::max(-z, 0.0f);
        x += x >= 0.0f ? -t : t;
        y += y >= 0.0f ? -t : t;
        const float len = std::sqrt(x * x + y * y + z * z);
        out[0] = x / len; out[1] = y / len; out[2] = z / len;
    }

    // Baked occlusion (supplied Lights.cpp "baked occlusion"): per light (by position + radius), for
    // each of kOccCells directions, how far it reaches before terrain or a WMO. Each cell keeps the
    // FARTHEST of 5 rays (its centre and 4 points near its corners). A hit in the first kEmbedded
    // yards counts as the light sitting inside that surface; the ray carries on past it. Baked under a
    // per-frame ray budget, nearest lights first; only lights that stayed in place for kSettleFrames
    // are queued (moving ones never finish).
    class OcclusionCache
    {
    public:
        // Casts one segment a -> b; returns the hit fraction in [0, 1], or a negative value for no hit.
        using Trace = std::function<float(const float* a, const float* b)>;

        static constexpr float kEmbedded = 0.3f;
        static constexpr unsigned kSettleFrames = 15;
        static constexpr int kSubRays = 5;
        static constexpr size_t kMaxEntries = 4096;

        struct Stats { unsigned wanted = 0, done = 0, rays = 0, cached = 0; };

        // Fills active[i].occlusion (null until its tile is complete) and spends the ray budget.
        Stats Update(std::vector<ActiveLight>& active, const std::vector<char>& wantBake, const float eye[3],
                     const Settings& settings, const Trace& trace)
        {
            ++frame_;
            Stats stats;
            std::vector<std::pair<float, Entry*>> work;
            for (size_t i = 0; i < active.size(); ++i)
            {
                ActiveLight& l = active[i];
                l.occlusion = nullptr;
                if (!settings.bakeEnabled || i >= wantBake.size() || !wantBake[i] || l.radius <= 0.0f) continue;
                ++stats.wanted;
                const uint64_t key = Key(l.pos, l.radius);
                auto it = entries_.find(key);
                if (it == entries_.end())
                {
                    if (entries_.size() >= kMaxEntries) continue;
                    it = entries_.emplace(key, Entry{}).first;
                    std::memcpy(it->second.pos, l.pos, sizeof(it->second.pos));
                    it->second.reach = l.radius + 0.5f;
                }
                Entry& e = it->second;
                e.settled = e.lastFrame + 1 == frame_ ? e.settled + 1 : 1;
                e.lastFrame = frame_;
                if (e.cellsDone == kOccCells) { l.occlusion = e.dist; ++stats.done; continue; }
                if (e.settled < kSettleFrames) continue;
                const float dx = l.pos[0] - eye[0], dy = l.pos[1] - eye[1], dz = l.pos[2] - eye[2];
                work.push_back({dx * dx + dy * dy + dz * dz, &e});
            }
            std::sort(work.begin(), work.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            const unsigned budget = static_cast<unsigned>(std::max(0, settings.bakeRaysPerFrame));
            for (auto& w : work)
            {
                Entry& e = *w.second;
                while (e.cellsDone < kOccCells && stats.rays + kSubRays * 3 <= budget) BakeCell(e, e.cellsDone++, stats.rays, trace);
                if (stats.rays + kSubRays * 3 > budget) break;
            }
            // Forget lights not seen for a while (left the area, or moved: a new position is a new entry).
            if (frame_ % 300 == 0)
                for (auto it = entries_.begin(); it != entries_.end();)
                    it = it->second.lastFrame + 600 < frame_ ? entries_.erase(it) : std::next(it);
            stats.cached = static_cast<unsigned>(entries_.size());
            return stats;
        }

        void Clear() { entries_.clear(); }
        size_t Size() const { return entries_.size(); }

    private:
        struct Entry
        {
            float dist[kOccCells] = {};
            int cellsDone = 0;
            unsigned settled = 0;
            uint64_t lastFrame = 0;
            float pos[3] = {};
            float reach = 0.0f;
        };

        static uint64_t Key(const float p[3], float radius)
        {
            const auto q = [](float v) { return static_cast<uint64_t>((static_cast<int64_t>(std::floor(v * 4.0f)) + 131072) & 0x3FFFF); };
            const uint64_t r = static_cast<uint64_t>(std::min(1023.0f, std::max(0.0f, radius * 8.0f)));
            return q(p[0]) | (q(p[1]) << 18) | (q(p[2]) << 36) | (r << 54);
        }

        static float CastRay(const float from[3], const float dir[3], float reach, unsigned& rays, const Trace& trace)
        {
            float start = 0.0f;
            for (int tries = 0; tries < 3; ++tries)
            {
                const float a[3] = {from[0] + dir[0] * start, from[1] + dir[1] * start, from[2] + dir[2] * start};
                const float b[3] = {from[0] + dir[0] * reach, from[1] + dir[1] * reach, from[2] + dir[2] * reach};
                ++rays;
                const float t = trace(a, b);
                if (!(t >= 0.0f && t <= 1.0f)) return reach;
                const float d = start + (reach - start) * t;
                if (d > kEmbedded) return d;
                start = d + 0.05f;
                if (start >= reach) return reach;
            }
            return reach;
        }

        static void BakeCell(Entry& e, int cell, unsigned& rays, const Trace& trace)
        {
            const int ix = cell % kOccSide, iy = cell / kOccSide;
            static const float kSub[kSubRays][2] = {{0.5f, 0.5f}, {0.15f, 0.15f}, {0.85f, 0.15f}, {0.15f, 0.85f}, {0.85f, 0.85f}};
            float best = 0.0f;
            for (const auto& s : kSub)
            {
                float dir[3];
                OctDecode((ix + s[0]) / kOccSide * 2.0f - 1.0f, (iy + s[1]) / kOccSide * 2.0f - 1.0f, dir);
                best = std::max(best, CastRay(e.pos, dir, e.reach, rays, trace));
            }
            e.dist[cell] = best;
        }

        std::unordered_map<uint64_t, Entry> entries_;
        uint64_t frame_ = 0;
    };
}
