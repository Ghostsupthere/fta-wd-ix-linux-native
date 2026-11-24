#include "RelinkCandidates.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include "Engine/Source/Tempest/C3Ray.h"

namespace RelinkCandidates {
namespace {
constexpr float CoordinateLimit = 16384.0f;
constexpr float MinDirection = 0.999999f;
constexpr float MaxDirection = 1.000001f;
constexpr float Tolerance = 0.0020000001f;
constexpr float Upper = Tolerance + 1.0f;
constexpr uint32_t MaxFaces = 8192;
constexpr size_t CacheBuckets = 64;

struct Interval {
    double low, high;
};

// Enclose each float operation, including subnormal values and an unrounded
// intermediate if the compiler contracts a multiply and add.
Interval Rounded(double low, double high) {
    const double margin = std::max(std::abs(low), std::abs(high)) * 0x1p-22 + 0x1p-120;
    return {low - margin, high + margin};
}

Interval Add(Interval a, Interval b) { return Rounded(a.low + b.low, a.high + b.high); }
Interval Sub(Interval a, Interval b) { return Rounded(a.low - b.high, a.high - b.low); }

Interval Mul(Interval a, Interval b) {
    const auto [low, high] = std::minmax({a.low * b.low, a.low * b.high, a.high * b.low, a.high * b.high});
    return Rounded(low, high);
}

Interval Exact(float value) { return {value, value}; }

bool InBounds(const C3Vector& p) {
    return std::abs(p.x) <= CoordinateLimit && std::abs(p.y) <= CoordinateLimit && std::abs(p.z) <= CoordinateLimit;
}

bool Candidate(Cell cell, const C3Vector& a, const C3Vector& b, const C3Vector& c) {
    if (!InBounds(a) || !InBounds(b) || !InBounds(c)) return true;

    const auto e1x = Sub(Exact(b.x), Exact(a.x));
    const auto e1y = Sub(Exact(b.y), Exact(a.y));
    const auto e2x = Sub(Exact(c.x), Exact(a.x));
    const auto e2y = Sub(Exact(c.y), Exact(a.y));
    const Interval z = cell.down ? Interval{-MaxDirection, -MinDirection} : Interval{MinDirection, MaxDirection};
    const auto px = Mul({-z.high, -z.low}, e2y);
    const auto py = Mul(z, e2x);
    const auto det = Add(Mul(py, e1y), Mul(px, e1x));

    if (det.low > -0.000001 && det.high < 0.000001) return false;
    if (det.low < 0.000001 && det.high > -0.000001) return true;
    const auto inverse = Rounded(1.0 / det.high, 1.0 / det.low);

    // The stock ray/triangle calculation reduces to these horizontal terms
    // when direction.x and direction.y are zero. Keep the stock t calculation.
    const auto sx = Sub({double(cell.x), double(cell.x) + 1}, Exact(a.x));
    const auto sy = Sub({double(cell.y), double(cell.y) + 1}, Exact(a.y));
    const auto u = Mul(Add(Mul(py, sy), Mul(sx, px)), inverse);
    if (u.high < -Tolerance || u.low > Upper) return false;

    const auto qz = Sub(Mul(e1y, sx), Mul(e1x, sy));
    const auto v = Mul(Mul(z, qz), inverse);
    return v.high >= -Tolerance && Add(v, u).low <= Upper;
}

struct Key {
    uint64_t geometry;
    uint32_t faceStart, count;
    Cell cell;
    bool operator==(const Key& other) const {
        return geometry == other.geometry && faceStart == other.faceStart && count == other.count && cell == other.cell;
    }
};

struct Entry {
    Key key{};
    std::array<uint8_t, MaxFaces> mask{};
};
std::array<std::array<Entry, 2>, CacheBuckets> cache;
std::array<uint8_t, CacheBuckets> replaceWay{};
std::chrono::steady_clock::time_point nextBuild;

size_t Bucket(Key key) {
    uint64_t hash = key.geometry * 0x9e3779b97f4a7c15ull + key.faceStart;
    hash = (hash ^ uint32_t(key.cell.x)) * 0xbf58476d1ce4e5b9ull;
    hash = (hash ^ uint32_t(key.cell.y)) * 0x94d049bb133111ebull;
    return ((hash >> 32) ^ hash ^ uint64_t(key.cell.down)) % CacheBuckets;
}
}

Mode Setting() {
    static const Mode mode = [] {
        const char* value = std::getenv("WXL_WMO_RELINK_CANDIDATES");
        if (!value) return Mode::Off;
        if (value[0] == '1' && value[1] == '\0') return Mode::Filter;
        if (value[0] == '2' && value[1] == '\0') return Mode::Verify;
        return Mode::Off;
    }();
    return mode;
}

bool CellForRay(const C3Ray& ray, Cell& cell) {
    const float z = std::abs(ray.dir.z);
    if (ray.dir.x != 0 || ray.dir.y != 0 || !(z >= MinDirection && z <= MaxDirection) || !InBounds(ray.origin))
        return false;
    cell = {static_cast<int32_t>(std::floor(ray.origin.x)), static_cast<int32_t>(std::floor(ray.origin.y)), ray.dir.z < 0};
    return true;
}

void BuildMask(Cell cell, const uint16_t* faces, uint32_t count, GeometryView mesh, uint8_t* mask) {
    for (uint32_t i = 0; i < count; ++i) {
        const size_t index = size_t(faces[i]) * 3;
        mask[i] = true;
        if (index + 2 >= mesh.indexCount) continue;
        const auto a = mesh.indices[index], b = mesh.indices[index + 1], c = mesh.indices[index + 2];
        if (a >= mesh.vertexCount || b >= mesh.vertexCount || c >= mesh.vertexCount) continue;
        mask[i] = Candidate(cell, mesh.vertices[a], mesh.vertices[b], mesh.vertices[c]);
    }
}

const uint8_t* GetMask(uint64_t geometry, uint32_t faceStart, Cell cell,
                       const uint16_t* faces, uint32_t count, GeometryView mesh, Lookup* lookup) {
    if (lookup) *lookup = Lookup::Unavailable;
    if (!geometry || !faces || count > MaxFaces || count <= 300 || !mesh.indices || !mesh.vertices ||
        !mesh.indexCount || !mesh.vertexCount)
        return nullptr;
    const Key key{geometry, faceStart, count, cell};
    const size_t bucket = Bucket(key);
    for (const auto& entry : cache[bucket]) {
        if (entry.key == key) {
            if (lookup) *lookup = Lookup::Hit;
            return entry.mask.data();
        }
    }

    const auto now = std::chrono::steady_clock::now();
    if (now < nextBuild) {
        if (lookup) *lookup = Lookup::Deferred;
        return nullptr;
    }
    auto& entry = cache[bucket][replaceWay[bucket]];
    replaceWay[bucket] ^= 1;
    BuildMask(cell, faces, count, mesh, entry.mask.data());
    entry.key = key;
    nextBuild = std::chrono::steady_clock::now() + std::chrono::milliseconds(4);
    if (lookup) *lookup = Lookup::Built;
    return entry.mask.data();
}
} // namespace RelinkCandidates
