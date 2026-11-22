#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <functional>

#ifdef NDEBUG
#error Assertions must be enabled
#endif

struct C3Vector {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float SquaredMag() const { return x * x + y * y + z * z; }
    C3Vector operator+(C3Vector v) const { return {x + v.x, y + v.y, z + v.z}; }
    C3Vector operator-(C3Vector v) const { return {x - v.x, y - v.y, z - v.z}; }
    C3Vector operator*(float scale) const { return {x * scale, y * scale, z * scale}; }
};

struct CAaBox {
    C3Vector b, t;
    void operator|=(const CAaBox& box) {
        b = {std::min(b.x, box.b.x), std::min(b.y, box.b.y), std::min(b.z, box.b.z)};
        t = {std::max(t.x, box.t.x), std::max(t.y, box.t.y), std::max(t.z, box.t.z)};
    }
    C3Vector Center() const { return {(b.x + t.x) * 0.5f, (b.y + t.y) * 0.5f, (b.z + t.z) * 0.5f}; }
};

struct CAaSphere { C3Vector c; float r = 0.0f; };
struct M2Bounds { CAaBox m_extent; float m_radius; };
struct ModelData { M2Bounds m_bounds; };
struct SharedModel { CAaBox m_boundingBox; ModelData* m_data; uint32_t m_flags = 0; };

struct CM2Model {
    SharedModel* m_shared = nullptr;
    CM2Model* m_attachList = nullptr;
    CM2Model* m_attachParent = nullptr;
    CM2Model* m_attachNext = nullptr;
    std::function<void()> onWait;
    int m_refCount = 1;
    bool failed = false;
    bool loaded = false;
    bool waitSucceeds = false;
    bool dropOwnerDuringWait = false;
    int waits = 0;
    int priorities = 0;
    int progressReads = 0;

    bool HasLoadFailed() const { return failed || !m_shared; }
    bool IsLoaded() const { return loaded; }
    bool IsLoaded(int, int) const { return !HasLoadFailed() && (loaded || (m_shared->m_flags & 3) == 3); }
    void Release() { assert(m_refCount > 0); --m_refCount; }
    bool WaitForLoad(const char*) {
        ++waits;
        assert(m_refCount > 1);
        if (dropOwnerDuringWait) {
            Release();
        }
        if (onWait) {
            onWait();
        }
        loaded = waitSucceeds;
        return waitSucceeds;
    }
    void LinkToAsyncHead(int) { assert(!HasLoadFailed()); ++priorities; }
    void ComputeLoadProgress(int64_t& loadedBytes, int64_t& totalBytes) {
        assert(!HasLoadFailed());
        ++progressReads;
        loadedBytes = 2;
        totalBytes = 4;
    }
    CAaBox GetBoundingBox();
    CAaSphere GetBoundingSphere();
    CAaBox GetMaxBoundingBoxPlusAttachments();
    CAaSphere GetMaxBoundingSpherePlusAttachments();
};

#include "shared_model_bounds.inc"

enum MapObjFlag {
    MAPOBJ_FLAG_UNPLACED = 8,
    MAPOBJ_FLAG_PREPARED = 128,
    MAPOBJ_FLAG_INTERIOR = 512,
};

struct CMapDoodadDef {
    CM2Model* m_model = nullptr;
    uint32_t m_staticFlags = 0;
    MapObjFlag m_flags{};
    struct DoodadLink {
        bool linked = true;
        void Unlink() { linked = false; }
    } m_doodadDefLink;
    CMapDoodadDef* next = nullptr;
    C3Vector m_position;
    float m_diffuseLightScale = 1.0f;
    CM2Model* replacement = nullptr;
    bool failDuringBounds = false;
    int updates = 0, extends = 0;

    void UpdateBounds() {
        ++updates;
        if (replacement) {
            m_model = replacement;
            replacement = nullptr;
        } else if (m_model && failDuringBounds) {
            m_model->failed = true;
        } else if (m_model && !m_model->HasLoadFailed()) {
            m_model->loaded = true;
        }
    }
    void ExtendChunkBounds() { ++extends; }
};

struct PreparationList {
    CMapDoodadDef* head = nullptr;
    CMapDoodadDef* Head() const { return head; }
    CMapDoodadDef* Next(CMapDoodadDef* doodad) const { return doodad->next; }
};

struct CMapBaseObjLink {
    void* m_owner;
    CMapBaseObjLink* next = nullptr;
};
template <typename T> struct TSGetExplicitLink {};
template <typename T, typename Hook> struct TSList {
    T* head = nullptr;
    T* Head() const { return head; }
    T* Next(T* link) const { return link->next; }
};
using ProgressList = TSList<CMapBaseObjLink, TSGetExplicitLink<CMapBaseObjLink>>;

struct CMapChunk { CAaBox m_bbox; ProgressList m_doodadDefLinkList; };
struct World { static inline C3Vector s_currentWorldPos{}; };
namespace SFile { int IsStreamingMode() { return 1; } }

// The extracted preparation function runs outside a profiled world update.
namespace Profiler {
enum Id { MapPrepareDoodads, WorldUpdateMapPreparation, Count };
Id UiDetailZone(Id, Id) { return Count; }
struct Scope { explicit Scope(Id) {} };
}

struct CMap {
    static inline PreparationList doodadDefList;
    static inline ProgressList s_unk_ADFBF8;
    static constexpr bool bDungeon = false;
    static bool QueryShadow(C3Vector*) { return true; }
    static void PrepareMapDoodadDefs();
    static uint32_t ComputeDoodadLoadProgress(CMapChunk*, float*, uint32_t);
};

#include "doodad_preparation.inc"
#include "doodad_progress.inc"
    *progress += static_cast<float>(total - remaining) * scale;
    return remaining;
}

static void TestAttachmentBounds() {
    ModelData data{{{{-2, -2, -2}, {2, 4, 2}}, 10}};
    SharedModel shared{{{1, 2, 3}, {5, 8, 3}}, &data, 3};
    CM2Model parent;
    parent.m_shared = &shared;
    parent.loaded = true;
    CM2Model child;
    child.m_shared = &shared;
    child.failed = true;
    child.m_attachParent = &parent;
    parent.m_attachList = &child;

    assert(parent.GetMaxBoundingBoxPlusAttachments().t.y == 10);
    const CAaSphere withCachedChild = parent.GetMaxBoundingSpherePlusAttachments();
    assert(withCachedChild.c.y == 1 && std::fabs(withCachedChild.r - (10.0f + std::sqrt(13.0f))) < 0.001f);
    assert(child.waits == 0 && child.m_refCount == 1 && parent.m_refCount == 1);

    parent.failed = true;
    assert(parent.GetMaxBoundingBoxPlusAttachments().t.y == 8);
    assert(parent.GetMaxBoundingSpherePlusAttachments().c.y == 5 && parent.waits == 0);
    parent.failed = false;

    CM2Model detaching;
    detaching.m_shared = &shared;
    detaching.waitSucceeds = true;
    auto attachThenDetachOnWait = [&] {
        detaching.loaded = false;
        detaching.m_refCount = 1;
        detaching.m_attachNext = &child;
        detaching.m_attachParent = &parent;
        parent.m_attachList = &detaching;
        detaching.onWait = [&] {
            parent.m_attachList = &child;
            detaching.m_attachParent = nullptr;
            detaching.m_attachNext = nullptr;
            detaching.Release();
        };
    };

    attachThenDetachOnWait();
    assert(parent.GetMaxBoundingBoxPlusAttachments().t.x == 6);
    assert(detaching.waits == 1 && detaching.m_refCount == 0);
    attachThenDetachOnWait();
    assert(parent.GetMaxBoundingSpherePlusAttachments().r == withCachedChild.r);
    assert(detaching.waits == 2 && detaching.m_refCount == 0);
}

int main() {
    TestAttachmentBounds();
    ModelData data{{{{-2, -2, -2}, {2, 4, 2}}, 10}};
    SharedModel shared{{{1, 2, 3}, {5, 8, 3}}, &data, 1};
    CM2Model model;
    model.m_shared = &shared;
    assert(model.GetBoundingBox().t.y == 4 && model.waits == 0);
    CAaSphere live = model.GetBoundingSphere();
    assert(live.r == 10 && live.c.y == 1 && model.waits == 0);

    model.failed = true;
    assert(model.GetBoundingBox().t.y == 8 && model.waits == 0);
    CAaSphere cached = model.GetBoundingSphere();
    assert(cached.c.x == 3 && cached.c.y == 5 && cached.c.z == 3);
    assert(std::fabs(cached.r - std::sqrt(13.0f)) < 0.001f);

    CM2Model missing;
    assert(missing.GetBoundingBox().t.x == 0 && missing.GetBoundingSphere().r == 0 && missing.waits == 0);

    shared.m_flags = 0;
    model.failed = false;
    assert(model.GetBoundingBox().t.y == 8 && model.waits == 1 && model.m_refCount == 1);
    model.waitSucceeds = true;
    model.dropOwnerDuringWait = true;
    assert(model.GetBoundingSphere().r == 10 && model.waits == 2 && model.m_refCount == 0);

    SharedModel cold{{}, nullptr, 0};
    SharedModel warmed{{}, &data, 3};
    std::array<CM2Model, 5> models;
    models[0].m_shared = &cold;
    models[0].failed = true;
    models[1].m_shared = &cold;
    models[2].m_shared = &warmed;
    models[3].m_shared = &warmed;
    models[4].m_shared = &warmed;

    std::array<CMapDoodadDef, 6> doodads;
    for (size_t i = 0; i < models.size(); ++i) {
        doodads[i].m_model = &models[i];
        doodads[i].next = &doodads[i + 1];
    }
    doodads[0].m_staticFlags = 0x10000;
    doodads[0].m_flags = MAPOBJ_FLAG_PREPARED;
    doodads[1].m_staticFlags = 0x10000;
    doodads[2].replacement = &models[1];
    doodads[3].failDuringBounds = true;
    CMap::doodadDefList.head = &doodads[0];
    CMap::PrepareMapDoodadDefs();
    assert(!doodads[0].m_doodadDefLink.linked && !(doodads[0].m_flags & MAPOBJ_FLAG_PREPARED));
    assert(doodads[0].updates == 1 && doodads[0].extends == 1 && !doodads[0].m_staticFlags);
    assert(doodads[1].m_doodadDefLink.linked && models[1].priorities == 1 && doodads[1].updates == 0);
    assert(doodads[2].m_doodadDefLink.linked && !(doodads[2].m_flags & MAPOBJ_FLAG_PREPARED));
    assert(!doodads[3].m_doodadDefLink.linked && !(doodads[3].m_flags & MAPOBJ_FLAG_PREPARED));
    assert(!doodads[4].m_doodadDefLink.linked && (doodads[4].m_flags & MAPOBJ_FLAG_PREPARED));
    assert(doodads[4].m_diffuseLightScale == 0.5f && doodads[4].extends == 1);
    assert(!doodads[5].m_doodadDefLink.linked && (doodads[5].m_flags & MAPOBJ_FLAG_PREPARED));

    CMapBaseObjLink pending{&doodads[1]}, absent{&doodads[5], &pending}, failed{&doodads[3], &absent};
    CMapChunk chunk;
    chunk.m_doodadDefLinkList.head = &failed;
    float progress = 0.0f;
    assert(CMap::ComputeDoodadLoadProgress(&chunk, &progress, 2) == 1);
    assert(models[1].progressReads == 1 && models[3].progressReads == 0 && progress == 0.75f);

    cold.m_flags = 3;
    CMap::doodadDefList.head = &doodads[2];
    doodads[2].next = nullptr;
    CMap::PrepareMapDoodadDefs();
    assert(!doodads[2].m_doodadDefLink.linked && (doodads[2].m_flags & MAPOBJ_FLAG_PREPARED));
}
