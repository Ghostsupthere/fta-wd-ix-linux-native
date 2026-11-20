#include <cassert>
#include <cstdint>
#include <functional>

#ifdef NDEBUG
#error Assertions must be enabled
#endif

struct C3Vector {
    float x, y, z;
};

struct C44Matrix {
    C3Vector position{};
    C44Matrix() = default;
    C44Matrix(C3Vector, C3Vector, C3Vector, C3Vector position) : position(position) {}
};

struct CParticleEmitter2 {
    uint32_t m_flags = 1;
    float width = 0.0f;
    float height = 0.0f;
    void SetWidth(float value) { width = value; }
    void SetHeight(float value) { height = value; }
};

struct ModelData {
    struct { uint32_t m_count; } m_particles;
};

struct SharedModel {
    ModelData* m_data;
};

struct CM2Model {
    SharedModel* m_shared = nullptr;
    CParticleEmitter2** m_particleEmitters = nullptr;
    CM2Model* m_attachParent = nullptr;
    uint32_t m_fFlags = 0;
    float m_fadeAlpha = 0.0f;
    C44Matrix transform;
    bool waitSucceeds = false;
    int waits = 0;
    int animations = 0;
    std::function<void()> onWait;

    bool WaitForLoad(const char*) {
        ++waits;
        if (onWait) {
            onWait();
        }
        return waitSucceeds;
    }

    void SetAnimating(int) { ++animations; }

    void SetWorldTransform(const C44Matrix& value) {
        m_fFlags |= 0x8000;
        transform = value;
    }
};

struct CBarrier_Contact {
    struct { C3Vector n{0.0f, 1.0f, 0.0f}; } m_plane;
    C3Vector m_point{1.0f, 2.0f, 3.0f};
    float m_strength = 0.5f;
};

struct CBarrier {
    static inline CM2Model* s_sparkles[4]{};
    struct {
        CBarrier_Contact m_data[4];
        int32_t Count() const { return 4; }
    } m_contacts;
    void RenderSparkles();
};

void CBarrier::RenderSparkles() {
#include "barrier_sparkles.inc"
}

struct CMapBaseObjLink {
    void* m_owner;
    CMapBaseObjLink* next = nullptr;
};

struct LinkList {
    CMapBaseObjLink* head;
    CMapBaseObjLink* Head() const { return head; }
    CMapBaseObjLink* Next(CMapBaseObjLink* link) const { return link->next; }
};

struct CMapDoodadDef {
    uint16_t m_doodadSet;
    CM2Model* m_model;
};

struct CMapObjDefGroup {
    LinkList m_doodadDefLinkList;
};

struct CMapObjDef {
    LinkList m_mapObjDefGroupLinkList;
    static void SetDoodadEmittersIgnoresDistance(CMapObjDef*, int32_t, uint16_t);
};

#include "doodad_emitters.inc"

int main() {
    ModelData data{{2}};
    SharedModel shared{&data};
    CParticleEmitter2 first, second;
    CParticleEmitter2* particles[]{&first, &second};
    CM2Model ready;
    ready.waitSucceeds = true;
    ready.m_shared = &shared;
    ready.m_particleEmitters = particles;
    CM2Model failed;
    CM2Model replaced;
    replaced.waitSucceeds = true;

    CBarrier barrier;
    barrier.s_sparkles[0] = &failed;
    barrier.s_sparkles[1] = &replaced;
    barrier.s_sparkles[2] = &ready;
    replaced.onWait = [&] { barrier.s_sparkles[1] = nullptr; };
    barrier.RenderSparkles();
    assert(failed.waits == 1 && replaced.waits == 1 && ready.waits == 1);
    assert(!failed.animations && !replaced.animations && ready.animations == 1);
    assert(ready.m_fFlags == (0x8 | 0x10000 | 0x8000) && ready.m_fadeAlpha == 0.5f);
    assert(ready.transform.position.z == 3.0f);
    assert(first.width == 100.0f && second.width == 100.0f);
    assert(first.height == 100.0f && second.height == 100.0f);

    constexpr uint16_t selectedSet = 4;
    CMapDoodadDef bad{selectedSet, &failed}, swapped{selectedSet, &replaced},
        matching{selectedSet, &ready}, other{5, &ready}, empty{0, nullptr};
    CMapBaseObjLink stopLink{&empty};
    CMapBaseObjLink matchingLink{&matching, &stopLink};
    CMapBaseObjLink otherLink{&other, &matchingLink};
    CMapBaseObjLink swappedLink{&swapped, &otherLink};
    CMapBaseObjLink badLink{&bad, &swappedLink};
    CMapObjDefGroup group{{&badLink}};
    CMapBaseObjLink groupLink{&group};
    CMapObjDef object{{&groupLink}};
    replaced.onWait = [&] { swapped.m_model = &ready; };
    CMapObjDef::SetDoodadEmittersIgnoresDistance(&object, 1, selectedSet);
    assert(failed.waits == 2 && replaced.waits == 2 && ready.waits == 2);
    assert(first.m_flags == 0x400001 && second.m_flags == 0x400001);

    swapped.m_model = &replaced;
    replaced.onWait = [&] { swapped.m_doodadSet = other.m_doodadSet; };
    CMapObjDef::SetDoodadEmittersIgnoresDistance(&object, 0, selectedSet);
    assert(failed.waits == 3 && replaced.waits == 3 && ready.waits == 3);
    assert(first.m_flags == 1 && second.m_flags == 1);
}
