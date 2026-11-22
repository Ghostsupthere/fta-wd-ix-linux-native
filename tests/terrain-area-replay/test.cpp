#include <cassert>
#include <cfenv>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>
#include "WoW/Source/WorldClient/TerrainAreaReplay.h"

struct CiRect { int32_t minX, minY, maxX, maxY; };
struct C3Vector { float x, y, z; };
struct C4Plane { C3Vector n; float d; };
constexpr int NUM_PLANES = 6;

struct CMapChunk {
    float m_distToCamera;
    int id;
    void CullAndBucketChunk();
    static inline int32_t farCornerIndex;
};

struct CMapArea {
    const void* m_chunkInfo = this;
    CMapChunk near{9.0f, 0};
    CMapChunk far{1024.0f, 1};
    bool mutateDuringUpdate = false;
};

static std::vector<int> terrainOrder;
static std::vector<int> terrainBuckets;
static std::vector<int> liquidOrder;
static int visits;
static int wetCullCalls;
static int replayHits;

struct CWorld {
    static inline CiRect gbChunkRect;
    static inline int32_t frameCnt;
};
struct SceneList {
    int bucket;
    void LinkToTail(CMapChunk* chunk) {
        terrainOrder.push_back(chunk->id);
        terrainBuckets.push_back(bucket);
    }
};
struct SortEntry { SceneList m_mapChunkList; };
struct SortTable { SortEntry m_table[64]; };
struct Frustum { C4Plane planes[NUM_PLANES]; };

struct CWorldScene {
    static inline CiRect s_frustumChunkRect;
    static inline Frustum frustumStack[2];
    static inline int frustumIndex;
    static inline C4Plane camPlane, camPlaneXY;
    static inline C3Vector s_activeWorldView, camTarget;
    static inline SortTable sortTable;
};

namespace Profiler {
enum Zone { MapPrepareAreaReplay, MapPrepareAreaTraversal };
Zone UiDetailZone(Zone child, Zone) { return child; }
struct Scope { explicit Scope(Zone) { ++replayHits; } };
}

struct CMap {
    static inline bool bPreload;
    static void UpdateArea(bool addToScene, CMapArea* area, CiRect*, int) {
        ++visits;
        if (area->mutateDuringUpdate) {
            TerrainAreaReplay::Invalidate();
        }
        if (!addToScene || !area->m_chunkInfo) {
            return;
        }
        area->near.m_distToCamera = 9.0f;
        area->far.m_distToCamera = 1024.0f;
        if (TerrainAreaReplay::recording) {
            TerrainAreaReplay::RecordTerrain(&area->near, 3);
            TerrainAreaReplay::RecordTerrain(&area->far, 64);
            TerrainAreaReplay::RecordWetChunk(&area->near);
        }
        CWorldScene::sortTable.m_table[3].m_mapChunkList.LinkToTail(&area->near);
        area->near.CullAndBucketChunk();
    }
};

void CMapChunk::CullAndBucketChunk() {
    ++wetCullCalls;
    liquidOrder.push_back(id);
}

#include "terrain_replay.inc"

int main(int argc, char** argv) {
    assert(argc == 2);
    const bool enabled = std::strcmp(argv[1], "1") == 0;
    CMapArea first;
    CMapArea second;
    for (int bucket = 0; bucket < 64; ++bucket) {
        CWorldScene::sortTable.m_table[bucket].m_mapChunkList.bucket = bucket;
    }
    second.near.id = 2;
    CiRect rect{16, 16, 31, 31};

    TerrainAreaReplay::Update(&first, rect, true);
    TerrainAreaReplay::Update(&second, rect, true);
    assert(visits == 2 && replayHits == 0);
    assert((terrainOrder == std::vector<int>{0, 2}));
    assert(liquidOrder == terrainOrder);
    terrainOrder.clear();
    liquidOrder.clear();

    ++CWorld::frameCnt;
    TerrainAreaReplay::Update(&first, rect, true);
    TerrainAreaReplay::Update(&second, rect, true);
    assert(visits == 4 && replayHits == 0);
    terrainOrder.clear();
    liquidOrder.clear();

    ++CWorld::frameCnt;
    first.near.m_distToCamera = first.far.m_distToCamera = -1.0f;
    TerrainAreaReplay::Update(&second, rect, true);
    TerrainAreaReplay::Update(&first, rect, true);
    assert(visits == (enabled ? 4 : 6) && replayHits == (enabled ? 2 : 0));
    assert((terrainOrder == std::vector<int>{2, 0}));
    assert(terrainBuckets == std::vector<int>(6, 3));
    assert(liquidOrder == terrainOrder && wetCullCalls == 6);
    assert(first.near.m_distToCamera == 9.0f && first.far.m_distToCamera == 1024.0f);

    auto expectMiss = [&] {
        const int before = visits;
        TerrainAreaReplay::Update(&first, rect, true);
        assert(visits == before + 1);
    };
    ++CWorldScene::frustumStack[0].planes[4].d;
    expectMiss();
    ++CWorld::gbChunkRect.maxX;
    expectMiss();
    ++CWorldScene::s_frustumChunkRect.maxX;
    expectMiss();
    ++CWorldScene::s_activeWorldView.z;
    expectMiss();
    ++CWorldScene::camTarget.y;
    expectMiss();
    ++CWorldScene::camPlane.d;
    expectMiss();
    ++CWorldScene::camPlaneXY.d;
    expectMiss();
    ++CMapChunk::farCornerIndex;
    expectMiss();
    ++CWorld::frameCnt;
    expectMiss();  // Record after the view remains stable for a frame.
    ++rect.maxX;
    expectMiss();
    TerrainAreaReplay::Invalidate();
    expectMiss();

    first.mutateDuringUpdate = true;
    TerrainAreaReplay::Invalidate();
    expectMiss();
    expectMiss();
    first.mutateDuringUpdate = false;
    expectMiss();

    CMap::bPreload = true;
    expectMiss();
    CMap::bPreload = false;
    CWorldScene::frustumIndex = 1;
    expectMiss();
    CWorldScene::frustumIndex = 0;
    const int before = visits;
    TerrainAreaReplay::Update(&first, rect, false);
    assert(visits == before + 1);
    first.m_chunkInfo = nullptr;
    expectMiss();

    first.m_chunkInfo = &first;
    const int rounding = std::fegetround();
    assert(std::fesetround(rounding == FE_UPWARD ? FE_DOWNWARD : FE_UPWARD) == 0);
    expectMiss();
    assert(std::fesetround(rounding) == 0);
}
