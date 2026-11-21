#include <algorithm>
#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstdint>

#ifdef NDEBUG
#error Assertions must be enabled
#endif

struct C3Vector { float x = 0.0f, y = 0.0f, z = 0.0f; };

struct C44Matrix {
    float a0 = 1.0f, a1 = 0.0f, a2 = 0.0f;
    float tx = 0.0f;
    C44Matrix operator*(const C44Matrix& rhs) const { return {a0 * rhs.a0, 0, 0, tx + rhs.tx}; }
};

static C3Vector operator*(C3Vector v, const C44Matrix& matrix) {
    return {v.x * matrix.a0 + matrix.tx, v.y * matrix.a0, v.z * matrix.a0};
}

struct CAaBox {
    C3Vector b, t;
    bool IsInverted() const { return b.x > t.x && b.y > t.y && b.z > t.z; }
    bool IsValid() const { return t.x > b.x && t.y > b.y && t.z > b.z; }
    C3Vector Center() const { return {(b.x + t.x) * 0.5f, (b.y + t.y) * 0.5f, (b.z + t.z) * 0.5f}; }
    void operator|=(const CAaBox& other) {
        b = {std::min(b.x, other.b.x), std::min(b.y, other.b.y), std::min(b.z, other.b.z)};
        t = {std::max(t.x, other.t.x), std::max(t.y, other.t.y), std::max(t.z, other.t.z)};
    }
    static CAaBox Transform(const CAaBox& local, const C44Matrix& world) {
        return {local.b * world, local.t * world};
    }
};

static CAaBox operator|(CAaBox a, const CAaBox& b) { a |= b; return a; }

struct CAaSphere {
    C3Vector c;
    float r = 0.0f;
    static CAaSphere BoundingAccurate(const CAaSphere*, uint32_t) {
        assert(false && "Unexpected emitter sphere aggregation");
        return {};
    }
};

struct M2Bounds { CAaBox m_extent; float m_radius; };
struct M2Sequence { M2Bounds m_bounds; };
struct ModelBone { struct { uint16_t m_sequenceIndex = 0xFFFF; } m_sequence; };
struct ArraySize { uint32_t m_count = 0; };

template <class T> struct M2Array {
    uint32_t m_count;
    T* data;
    const T& operator[](uint32_t i) const {
        assert(i < m_count);
        return data[i];
    }
};

struct M2Data {
    M2Bounds m_bounds;
    M2Array<M2Sequence> m_sequences;
    M2Array<uint16_t> m_boneIndicesById;
    ArraySize m_bones, m_particles, m_ribbons;
};

struct SharedModel { M2Data* m_data; };
struct ModelScene { C44Matrix m_viewInv; };
struct CParticleEmitter2 { uint32_t m_flags = 0; CAaBox WorldBounds() const { return {}; } };
struct Ribbon { CAaBox m_worldBounds; };
static bool fnotequal(float a, float b) { return a != b; }

struct CM2Model {
    uint32_t m_fFlags = 0x8001;
    SharedModel* m_shared = nullptr;
    ModelScene* m_scene = nullptr;
    ModelBone* m_bones = nullptr;
    CM2Model* m_attachParent = nullptr;
    C44Matrix m_matrixF4;
    C44Matrix m_worldTransform;
    CParticleEmitter2** m_particleEmitters = nullptr;
    Ribbon** m_ribbonEmitters = nullptr;

    CAaBox GetCurrentBoundingBox();
    CAaBox GetSplitBodyBoundingBox(uint32_t boneId);
    CAaSphere GetCurrentBoundingSphere();
};

#include "sequence_bounds.inc"

int main() {
    M2Sequence sequences[]{{{{{-1, -1, -1}, {1, 1, 1}}, 2}},
                           {{{{-2, -2, -2}, {6, 2, 2}}, 5}}};
    uint16_t boneMap[]{0, 1, 20, 0xFFFF};
    M2Data data{{{{-3, -3, -3}, {3, 3, 3}}, 7}, {2, sequences}, {4, boneMap}, {2}, {}, {}};
    SharedModel shared{&data};
    CM2Model model;
    model.m_shared = &shared;
    model.m_worldTransform = {2, 0, 0, 10};

    assert(model.GetCurrentBoundingBox().t.x == 16 && model.GetSplitBodyBoundingBox(1).t.x == 16);
    assert(model.GetCurrentBoundingSphere().r == 14 && model.GetCurrentBoundingSphere().c.x == 10);
    ModelBone bones[2];
    model.m_bones = bones;
    assert(model.GetCurrentBoundingBox().b.x == 4 && model.GetCurrentBoundingSphere().r == 14);

    bones[0].m_sequence.m_sequenceIndex = 2;
    bones[1].m_sequence.m_sequenceIndex = 1;
    assert(model.GetCurrentBoundingBox().t.x == 16);
    assert(model.GetSplitBodyBoundingBox(1).b.x == 4 && model.GetSplitBodyBoundingBox(1).t.x == 22);

    bones[0].m_sequence.m_sequenceIndex = 0;
    assert(model.GetCurrentBoundingBox().t.x == 12 && model.GetCurrentBoundingSphere().r == 4);
    assert(model.GetSplitBodyBoundingBox(1).t.x == 22);
    assert(model.GetSplitBodyBoundingBox(2).t.x == 12 && model.GetSplitBodyBoundingBox(3).t.x == 12);
    assert(model.GetSplitBodyBoundingBox(0xFFFFFFFF).t.x == 12);
    bones[1].m_sequence.m_sequenceIndex = 0xFFFF;
    assert(model.GetSplitBodyBoundingBox(1).t.x == 12);

    data.m_sequences.m_count = 0;
    assert(model.GetCurrentBoundingBox().t.x == 16 && model.GetCurrentBoundingSphere().r == 14);
    data.m_sequences.m_count = 2;
    data.m_bones.m_count = 0;
    assert(model.GetSplitBodyBoundingBox(1).t.x == 16);

    model.m_fFlags = 1;
    assert(model.GetCurrentBoundingBox().IsInverted() && model.GetSplitBodyBoundingBox(1).IsInverted());
    assert(model.GetCurrentBoundingSphere().r == 0);
}
