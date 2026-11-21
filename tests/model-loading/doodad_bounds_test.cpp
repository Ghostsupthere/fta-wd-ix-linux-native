#include <cassert>
#include <cmath>
#include <cstdint>
#include <functional>

#ifdef NDEBUG
#error Assertions must be enabled
#endif

struct C44Matrix;

struct C3Vector {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    float SquaredMag() const { return x * x + y * y + z * z; }
    C3Vector operator+(C3Vector v) const { return {x + v.x, y + v.y, z + v.z}; }
    C3Vector operator-(C3Vector v) const { return {x - v.x, y - v.y, z - v.z}; }
    C3Vector operator*(float scale) const { return {x * scale, y * scale, z * scale}; }
    C3Vector& operator*=(const C44Matrix& matrix);
};

struct C44Matrix {
    float d0 = 0.0f, d1 = 0.0f, d2 = 0.0f;
    C44Matrix operator*(C44Matrix m) const { return {d0 + m.d0, d1 + m.d1, d2 + m.d2}; }
};

static C3Vector operator*(C3Vector v, const C44Matrix& m) {
    return v + C3Vector{m.d0, m.d1, m.d2};
}

C3Vector& C3Vector::operator*=(const C44Matrix& matrix) {
    return *this = *this * matrix;
}

struct CAaBox {
    C3Vector b, t;
    C3Vector Center() const { return (b + t) * 0.5f; }
};

struct CAaSphere {
    C3Vector c;
    float r = 0.0f;
};

struct CWorldMath {
    static void TransformAABox(const C44Matrix& m, const CAaBox& box, CAaBox& result) {
        result = {box.b * m, box.t * m};
    }
};

struct M2Data {
    struct { CAaBox m_extent; } m_collisionBounds;
};

struct SharedModel {
    M2Data* m_data = nullptr;
    CAaBox m_boundingBox;
};

struct CFacet {};
template <typename T> struct TSGrowableArray { int count = 0; };

struct CM2Model {
    SharedModel* m_shared = nullptr;
    uint32_t m_fFlags = 0x40;
    bool sharedReady = false;
    bool ready = false;
    bool failed = false;
    bool waitSucceeds = false;
    int waits = 0;
    C44Matrix transform;
    std::function<void()> onWait;

    bool HasLoadFailed() const { return failed || !m_shared; }
    bool IsLoaded() const { return ready; }
    bool IsLoaded(int, int) const { return sharedReady && !HasLoadFailed(); }
    bool WaitForLoad(const char*) {
        ++waits;
        if (onWait) {
            onWait();
        }
        ready = waitSucceeds;
        return ready;
    }
    CAaBox GetBoundingBox() const {
        assert(ready && !failed);
        return {{-3, -3, -3}, {3, 3, 3}};
    }
    CAaSphere GetBoundingSphere() const {
        assert(ready && !failed);
        return {{}, 4};
    }
    void SetWorldTransform(const C44Matrix& matrix) { transform = matrix; }
    void GetCollisionFacets(const CAaBox&, const C44Matrix&, TSGrowableArray<CFacet>&);
};

#include "collision_facets_gate.inc"
    assert(this->m_shared && this->m_shared->m_data);
    ++facets.count;
}

enum MapObjFlag { MAPOBJ_FLAG_UNPLACED = 8 };
constexpr uint32_t Enable_NoAnimation = 0x8000;
struct CWorld { static inline uint32_t enables = 0; };
struct CWorldView { static inline float s_fadeSize[4]{1, 4, 8, 16}; };

namespace SI2 {
void Set3DPosition(int* updates, const C3Vector*) { ++*updates; }
}

struct CMapDoodadDef {
    CM2Model* m_model = nullptr;
    CAaBox m_bbox, m_bboxDoodadDef;
    CAaSphere m_sphere;
    C3Vector m_position, m_vec2;
    C44Matrix m_mat{10, 0, 0}, m_identity;
    float m_scale = 2.0f;
    uint8_t m_fadeLevel = 0;
    MapObjFlag m_flags{};
    int m_soundKitObject = 0;
    void UpdateBounds();
    void UpdateMatrixOnly(const C44Matrix&);
};

#include "doodad_bounds.inc"

int main() {
    M2Data data{{{{-2, -2, -2}, {2, 2, 2}}}};
    SharedModel shared{&data, {{-1, -1, -1}, {1, 1, 1}}};
    CM2Model ready;
    ready.m_shared = &shared;
    ready.sharedReady = true;
    ready.waitSucceeds = true;
    CMapDoodadDef doodad;

    doodad.UpdateBounds();
    assert(doodad.m_bbox.b.x == 10 && doodad.m_bboxDoodadDef.t.x == 10 && doodad.m_sphere.r == 0);

    doodad.m_model = &ready;
    doodad.UpdateBounds();
    assert(doodad.m_bbox.b.x == 7 && doodad.m_bboxDoodadDef.t.x == 12);
    assert(doodad.m_sphere.r == 8 && doodad.m_vec2.x == 10 && ready.m_fFlags == 0);
    doodad.UpdateMatrixOnly({20, 0, 0});
    assert(doodad.m_bbox.b.x == 17 && doodad.m_bboxDoodadDef.t.x == 22);
    assert(ready.transform.d0 == 20 && doodad.m_soundKitObject == 1);

    SharedModel cached{nullptr, {{-5, 0, 0}, {5, 0, 0}}};
    CM2Model failed;
    failed.m_shared = &cached;
    failed.failed = true;
    doodad.m_model = &failed;
    doodad.UpdateBounds();
    assert(doodad.m_bbox.b.x == 15 && doodad.m_bboxDoodadDef.t.x == 25);
    assert(doodad.m_sphere.r == 10 && failed.m_fFlags == 0x40);
    const int failedWaits = failed.waits;
    doodad.UpdateMatrixOnly({30, 0, 0});
    assert(failed.waits == failedWaits && doodad.m_bboxDoodadDef.t.x == 35);
    assert(doodad.m_bbox.b.x == 30 && doodad.m_soundKitObject == 2);

    CM2Model loading;
    loading.m_shared = &shared;
    loading.sharedReady = true;
    loading.waitSucceeds = true;
    loading.onWait = [&] { doodad.m_model = &failed; };
    doodad.m_model = &loading;
    doodad.UpdateMatrixOnly({40, 0, 0});
    assert(doodad.m_bboxDoodadDef.t.x == 45 && loading.transform.d0 == 0);
    assert(failed.transform.d0 == 40 && doodad.m_soundKitObject == 3);

    doodad.m_model = &loading;
    loading.onWait = [&] { doodad.m_model = nullptr; };
    doodad.UpdateBounds();
    assert(doodad.m_bbox.b.x == 40 && doodad.m_bboxDoodadDef.t.x == 40);

    TSGrowableArray<CFacet> facets;
    failed.GetCollisionFacets({}, {}, facets);
    assert(failed.waits == failedWaits && facets.count == 0);
    CM2Model failedWait;
    failedWait.m_shared = &cached;
    failedWait.GetCollisionFacets({}, {}, facets);
    assert(failedWait.waits == 1 && facets.count == 0);
    ready.GetCollisionFacets({}, {}, facets);
    assert(facets.count == 1);
}
