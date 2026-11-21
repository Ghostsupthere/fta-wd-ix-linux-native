#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#ifdef NDEBUG
#error Assertions must be enabled
#endif

struct C3Vector { float x = 0, y = 0, z = 0; };
struct Camera { void* m_camera; };
struct Attachment { C3Vector m_position; };
struct Vertex {
    struct { uint8_t b[4]{}; } m_indices, m_weights;
};

template <class T>
struct Array {
    uint32_t m_count = 0;
    T* data = nullptr;
    const T& operator[](uint32_t index) const {
        assert(index < m_count);
        return data[index];
    }
};

struct M2Data {
    Array<Camera> m_cameras;
    Array<uint16_t> m_attachmentIndicesById;
    Array<Attachment> m_attachments;
    Array<Vertex> m_vertices;
    Array<int> m_bones;
};

struct Shared { M2Data* m_data; };
struct CM2Model {
    Shared* m_shared = nullptr;
    Camera* m_cameras = nullptr;
    const void* m_boneMatrices = nullptr;
    int m_refCount = 1;
    int waits = 0;
    bool loaded = false;
    bool result = false;
    bool ownerDropsReference = false;

    bool IsLoaded() const { return loaded; }
    bool HasLoadFailed() const { return !m_shared; }
    void Release() { assert(m_refCount > 0); --m_refCount; }
    bool WaitForLoad(const char*) {
        assert(m_refCount > 1);
        ++waits;
        if (ownerDropsReference) {
            Release();
        }
        return result;
    }
    void* GetCameraByIndex(uint32_t index);
    C3Vector GetAttachmentPivot(uint32_t id);
};

#include "query_guards.inc"

struct M2SkinProfile { Array<uint16_t> m_vertices; };
struct M2SkinSection {
    uint32_t m_vertexStart = 0;
    uint32_t m_vertexCount = 1;
    uint32_t m_boneInfluences = 1;
};

#include "hit_validation.inc"

struct CiRect { int32_t minX, minY, maxX, maxY; };
struct CMath { static int32_t fint_(float value) { return static_cast<int32_t>(std::nearbyint(value)); } };
constexpr float SMOLTILE_SIZE = 4.1666665f;
constexpr float OO_COORD_TO_SUBCHUNK = 0.24f;

struct RayScratch {
    std::vector<uint32_t> storage;
    uint32_t* m_data = nullptr;
    uint32_t Count() const { return static_cast<uint32_t>(storage.size()); }
    void SetCount(uint32_t count) {
        storage.resize(count);
        m_data = storage.data();
    }
};

struct CMap {
    static inline RayScratch scCollideList;
    static inline uint32_t cCount = 0;

    static void VectorIntersectSX(CiRect&);
    static void VectorIntersectSY(CiRect&);
    static void VectorIntersectDX(C3Vector&, C3Vector&, CiRect&);
    static void VectorIntersectDY(C3Vector&, C3Vector&, CiRect&);
};

#include "terrain_ray.inc"

int main() {
    M2Data data;
    Shared shared{&data};
    Camera camera{&data};
    uint16_t attachmentMap[]{0, 9};
    Attachment point{{3, 0, 0}};
    data.m_cameras = {1, &camera};
    data.m_attachmentIndicesById = {2, attachmentMap};
    data.m_attachments = {1, &point};
    CM2Model model;
    assert(!model.GetCameraByIndex(0) && model.waits == 0);
    model.m_shared = &shared;
    model.m_cameras = &camera;
    assert(!model.GetCameraByIndex(0) && model.waits == 1 && model.m_refCount == 1);
    model.loaded = true;
    assert(model.GetCameraByIndex(0) == &data && !model.GetCameraByIndex(1));
    assert(model.GetAttachmentPivot(0).x == 3 && model.GetAttachmentPivot(1).x == 0);
    assert(model.GetAttachmentPivot(7).x == 0 && model.waits == 1);
    model.loaded = false;
    model.result = true;
    model.ownerDropsReference = true;
    assert(model.GetCameraByIndex(0) == &data && model.waits == 2 && model.m_refCount == 0);

    Vertex vertex;
    uint16_t vertexIndex = 0;
    M2SkinProfile skin{{1, &vertexIndex}};
    M2SkinSection section;
    data.m_vertices = {1, &vertex};
    data.m_bones.m_count = 1;
    model.m_boneMatrices = &data;
    assert(HitTestVertexReferences(model, skin, section));
    section.m_boneInfluences = 2;
    assert(!HitTestVertexReferences(model, skin, section));
    vertex.m_weights.b[0] = 255;
    assert(HitTestVertexReferences(model, skin, section));
    vertex.m_weights.b[1] = 1;
    vertex.m_indices.b[1] = 1;
    assert(!HitTestVertexReferences(model, skin, section));
    vertex.m_weights.b[1] = 0;
    vertexIndex = 1;
    assert(!HitTestVertexReferences(model, skin, section));
    assert(!HitTestRange(UINT32_MAX, 2, UINT32_MAX));

    CiRect straight{0, 6, 3000, 6};
    CMap::VectorIntersectSX(straight);
    assert(CMap::cCount == 6002 && CMap::scCollideList.m_data[6000] == 3000);
    CMap::cCount = 0;
    CiRect reverse{2, 2500, 2, 0};
    CMap::VectorIntersectSY(reverse);
    assert(CMap::cCount == 5002 && CMap::scCollideList.m_data[5001] == 0);
    CMap::cCount = 0;
    CiRect edge{INT32_MAX, 0, INT32_MAX, 0};
    CMap::VectorIntersectSX(edge);
    assert(CMap::cCount == 2);
    CiRect oversized{INT32_MIN, 0, INT32_MAX, 0};
    CMap::VectorIntersectSX(oversized);
    assert(CMap::cCount == 2);
    CMap::cCount = 0;
    C3Vector start{}, end{8000, 4000, 0};
    CiRect diagonal{0, 0, 1920, 960};
    CMap::VectorIntersectDX(start, end, diagonal);
    assert(CMap::cCount > 0x7FC && CMap::cCount <= CMap::scCollideList.Count());
    CMap::cCount = 0;
    end = {4000, 8000, 0};
    CiRect steep{0, 0, 960, 1920};
    CMap::VectorIntersectDY(start, end, steep);
    assert(CMap::cCount > 0x7FC && CMap::cCount <= CMap::scCollideList.Count());
}
