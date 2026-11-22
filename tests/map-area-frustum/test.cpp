#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

struct CiRect { int32_t minX, minY, maxX, maxY; };
struct Index { int32_t x, y; };
struct CWorld { static inline CiRect gbChunkRect; };

static std::vector<int> operations;
static int frustumTests;

enum Operation { Prepare, AddTerrain, BucketLiquid, FreeLink, Purge };

struct CMapChunk {
    Index m_aIndex;
    int Id() const { return 16 * m_aIndex.y + m_aIndex.x; }
    void Record(Operation operation) const { operations.push_back(256 * operation + Id()); }
    void CullAndBucketChunk() { Record(BucketLiquid); }
    struct { CMapChunk* owner; CMapChunk* Head() { return owner; } } m_parentLinkList;
};

struct CWorldScene {
    static inline CiRect frustum;
    static bool InsideFrustumRect(CiRect* rect) {
        ++frustumTests;
        return rect->minX <= frustum.maxX && rect->minY <= frustum.maxY
            && rect->maxX >= frustum.minX && rect->maxY >= frustum.minY;
    }
    static void AddMapChunk(CMapChunk* chunk) { chunk->Record(AddTerrain); }
};

struct CMap {
    static void FreeBaseObjLink(CMapChunk* chunk) { chunk->Record(FreeLink); }
};

struct CMapArea {
    const void* m_chunkInfo = this;
    CMapChunk* m_mapChunks[256] = {};
    CMapChunk chunks[256] = {};

    CMapChunk* Spawn(int x, int y) {
        const int index = x + 16 * y;
        auto& chunk = chunks[index];
        chunk.m_aIndex = {x, y};
        chunk.m_parentLinkList.owner = &chunk;
        m_mapChunks[index] = &chunk;
        return &chunk;
    }

    void PrepareChunk(int x, int y) { Spawn(x, y)->Record(Prepare); }
    static void PurgeChunk(CMapChunk* chunk) { chunk->Record(Purge); }
    void Update(int32_t addToScene, CiRect* chunkRect);
};

#include "terrain_update.inc"

static void Compare(CiRect block, CiRect stream, CiRect frustum, bool addToScene, bool once, int tests) {
    CWorld::gbChunkRect = stream;
    CWorldScene::frustum = frustum;
    CMapArea baseline;
    CMapArea candidate;
    for (int y = block.minY; y <= block.maxY; ++y) {
        for (int x = block.minX; x <= block.maxX; ++x) {
            if ((x + y) % 2 == 0) {
                baseline.Spawn(x & 15, y & 15);
                candidate.Spawn(x & 15, y & 15);
            }
        }
    }

    operations.clear();
    frustumTests = 0;
    UpdateAreaChunks<false>(baseline, addToScene, &block);
    const auto expected = operations;
    assert(frustumTests == tests);

    operations.clear();
    frustumTests = 0;
    candidate.Update(addToScene, &block);
    assert(operations == expected);
    assert(frustumTests == (once && addToScene ? 1 : tests));
    for (int i = 0; i < 256; ++i) {
        assert(bool(candidate.m_mapChunks[i]) == bool(baseline.m_mapChunks[i]));
    }
}

int main(int argc, char** argv) {
    assert(argc == 2);
    const bool once = std::strcmp(argv[1], "1") == 0;
    const CiRect block{16, 32, 19, 35};
    const CiRect partialStream{17, 30, 20, 34};
    const CiRect offscreen{20, 36, 29, 41};
    Compare(block, block, block, true, once, 16);
    Compare(block, partialStream, {19, 35, 20, 36}, true, once, 9);
    Compare(block, partialStream, offscreen, true, once, 9);
    Compare(block, partialStream, block, false, once, 0);
    Compare({1020, 1020, 1023, 1023}, {1010, 1010, 1023, 1023}, block, true, once, 16);

    CMapArea unavailable;
    unavailable.m_chunkInfo = nullptr;
    CiRect rect = block;
    operations.clear();
    frustumTests = 0;
    unavailable.Update(1, &rect);
    assert(operations.empty() && frustumTests == 0);
}
