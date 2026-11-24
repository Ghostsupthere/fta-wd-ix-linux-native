#include "WoW/Source/WorldClient/StaticCullReuse.h"

#include <array>
#include <cassert>
#include <limits>

static bool StockHidden(const StaticCullReuse::BoxQuery& query, const float* depth) {
    if (!query.testsColumns) return false;
    for (int32_t column = query.first; column <= query.last; ++column) {
        if (query.height > depth[column]) return false;
    }
    return true;
}

int main() {
    using namespace StaticCullReuse;
    std::array<float, 384> depth{};
    BoxQuery query{1.0f, 4, 9, -1, true};
    depth.fill(2.0f);
    assert(query.Hidden(depth.data()));
    depth[7] = 0.0f;
    assert(!query.Hidden(depth.data()) && query.visibleColumn == 7);
    depth[7] = 1.0f;
    depth[8] = 0.0f;
    assert(!query.Hidden(depth.data()) && query.visibleColumn == 8);
    depth[8] = 2.0f;
    assert(query.Hidden(depth.data()) && query.visibleColumn == -1);
    depth[7] = std::numeric_limits<float>::quiet_NaN();
    assert(query.Hidden(depth.data())); // retain the stock greater-than comparison
    for (unsigned seed = 1; seed < 32; ++seed) {
        for (unsigned column = 0; column < depth.size(); ++column) {
            depth[column] = (column * seed + seed) % 17;
        }
        assert(query.Hidden(depth.data()) == StockHidden(query, depth.data()));
    }
    BoxQuery noProjection;
    assert(!noProjection.Hidden(depth.data()));

    using View = std::array<uint32_t, 3>;
    Cache<View, 8> cache;
    bool hit = true;
    const Bounds bounds{0, 0, 0, 1, 2, 3};
    View view{0, 10, 11};
    cache.Begin(10, view);
    auto& first = cache.Lookup(0x1000, bounds, hit);
    assert(!hit);
    first.inFrustum = true;
    first.projected = true;
    first.query = query;
    cache.Begin(11, view);
    const auto& next = cache.Lookup(0x1000, bounds, hit);
    assert(hit && next.projected && next.inFrustum);
    auto replaced = bounds;
    replaced[3] += 0.25f;
    cache.Lookup(0x1000, replaced, hit);
    assert(!hit); // a moved or reloaded object at the same address
    cache.Begin(12, {1, 10, 11});
    cache.Lookup(0x1000, replaced, hit);
    assert(!hit); // map change
    view = {1, 10, 12};
    cache.Begin(13, view);
    cache.Lookup(0x1000, replaced, hit);
    assert(!hit); // camera/projection change
    cache.Begin(15, view);
    cache.Lookup(0x1000, replaced, hit);
    assert(!hit); // gap in world render frames
}
