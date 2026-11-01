#include "Engine/Source/Model2/WxlModelMirror.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <new>

using namespace WxlModelMirror;

// The engine's vector and box constructors live in libraries this test does not link.
C2Vector::C2Vector() {}
static CAaBox Box(const C3Vector& low, const C3Vector& high) {
    CAaBox box;
    box.b = low;
    box.t = high;
    return box;
}

// One block holds the model and every array it points to, so the self-relative 32-bit offsets the
// engine stores are always representable.
struct Arena {
    uint8_t* base = static_cast<uint8_t*>(std::aligned_alloc(16, 1 << 16));
    size_t used = 0;
    Arena() { std::memset(base, 0, 1 << 16); }
    ~Arena() { std::free(base); }
    template <class T> T* take(uint32_t count) {
        used = (used + 15) & ~size_t(15);
        T* result = new (base + used) T[count ? count : 1]();
        used += sizeof(T) * (count ? count : 1);
        assert(used < (1 << 16));
        return result;
    }
};

static float DecodeComponent(uint16_t v) { return v * 0.000030518044f - 1.0f; }

int main() {
    Arena a;
    M2Data& data = *a.take<M2Data>(1);

    // Three sequences: 0 embedded, 1 not resident (its inner key arrays hold garbage offsets), 2 aliases 0.
    M2Sequence* seq = a.take<M2Sequence>(3);
    seq[0].m_flags = 0x20; seq[1].m_flags = 0; seq[2].m_flags = 0x20;
    seq[0].m_bounds.m_extent = Box(C3Vector(-1, 2, -3), C3Vector(1, 5, 3));
    data.m_sequences.m_count = 3; data.m_sequences.Set(seq);

    M2Vertex* verts = a.take<M2Vertex>(2);
    verts[0].m_position = C3Vector(1, 2, 3); verts[0].m_normal = C3Vector(0, 1, 0);
    verts[1].m_position = C3Vector(-4, -5, 6); verts[1].m_normal = C3Vector(0.6f, -0.8f, 0);
    data.m_vertices.m_count = 2; data.m_vertices.Set(verts);

    // Bone 0: per-sequence tracks. Sequence 0 and 2 share one translation key array (aliases).
    // Bone 1: a global-sequence track (loop index != 0xFFFF) with one resolved array.
    M2CompBone* bones = a.take<M2CompBone>(2);
    C3Vector* sharedKeys = a.take<C3Vector>(2);
    sharedKeys[0] = C3Vector(1, 10, 2); sharedKeys[1] = C3Vector(3, -20, 4);
    M2Array<C3Vector>* trans = a.take<M2Array<C3Vector>>(3);
    trans[0].m_count = 2; trans[0].Set(sharedKeys);
    trans[2].m_count = 2; trans[2].Set(sharedKeys);
    trans[1].m_count = 5; trans[1].m_offset = 0x7fff0000;  // not resident: must never be dereferenced
    bones[0].m_translationTrack.m_loopIndex = 0xFFFF;
    bones[0].m_translationTrack.m_sequenceKeys.m_count = 3;
    bones[0].m_translationTrack.m_sequenceKeys.Set(trans);
    M2CompQuat* quats = a.take<M2CompQuat>(1);
    // A 90 degree turn about Z: (0, 0, s, s) as encoded components.
    auto encode = [](float f) { return static_cast<uint16_t>((f + 1.0f) / 0.000030518044f + 0.5f); };
    const float s = 0.70710678f;
    quats[0].m_values[0] = encode(0.1f); quats[0].m_values[1] = encode(0.2f);
    quats[0].m_values[2] = encode(s); quats[0].m_values[3] = encode(s);
    M2Array<M2CompQuat>* rots = a.take<M2Array<M2CompQuat>>(3);
    rots[0].m_count = 1; rots[0].Set(quats);
    rots[1].m_count = 1; rots[1].m_offset = 0x7fff0000;
    rots[2].m_count = 0;
    bones[0].m_rotationTrack.m_loopIndex = 0xFFFF;
    bones[0].m_rotationTrack.m_sequenceKeys.m_count = 3;
    bones[0].m_rotationTrack.m_sequenceKeys.Set(rots);
    bones[0].m_pivot = C3Vector(0, 7, 1);

    C3Vector* globalKeys = a.take<C3Vector>(1);
    globalKeys[0] = C3Vector(0, 3, 0);
    M2Array<C3Vector>* globalTrack = a.take<M2Array<C3Vector>>(1);
    globalTrack[0].m_count = 1; globalTrack[0].Set(globalKeys);
    bones[1].m_translationTrack.m_loopIndex = 0;
    bones[1].m_translationTrack.m_sequenceKeys.m_count = 1;
    bones[1].m_translationTrack.m_sequenceKeys.Set(globalTrack);
    data.m_bones.m_count = 2; data.m_bones.Set(bones);

    M2Attachment* attachments = a.take<M2Attachment>(1);
    attachments[0].m_position = C3Vector(0.5f, 0.25f, 0.0f);
    data.m_attachments.m_count = 1; data.m_attachments.Set(attachments);
    data.m_bounds.m_extent = Box(C3Vector(-1, -2, -3), C3Vector(4, 6, 8));
    data.m_collisionBounds.m_extent = Box(C3Vector(0, 1, 0), C3Vector(1, 2, 1));

    MirrorData(data);

    assert(verts[0].m_position.x == 1 && verts[0].m_position.y == -2 && verts[0].m_position.z == 3);
    assert(verts[0].m_normal.y == -1 && verts[1].m_normal.y == 0.8f && verts[1].m_position.y == 5);
    assert(bones[0].m_pivot.y == -7);
    // The shared array behind sequences 0 and 2 flips exactly once.
    assert(sharedKeys[0].y == -10 && sharedKeys[1].y == 20 && sharedKeys[0].x == 1 && sharedKeys[1].z == 4);
    assert(trans[1].m_offset == 0x7fff0000 && rots[1].m_offset == 0x7fff0000);
    assert(globalKeys[0].y == -3);
    // x and z flip, y and w stay.
    assert(std::abs(DecodeComponent(quats[0].m_values[0]) + 0.1f) < 1e-3f);
    assert(std::abs(DecodeComponent(quats[0].m_values[1]) - 0.2f) < 1e-3f);
    assert(std::abs(DecodeComponent(quats[0].m_values[2]) + s) < 1e-3f);
    assert(std::abs(DecodeComponent(quats[0].m_values[3]) - s) < 1e-3f);
    assert(attachments[0].m_position.y == -0.25f && attachments[0].m_position.x == 0.5f);
    assert(data.m_bounds.m_extent.b.y == -6 && data.m_bounds.m_extent.t.y == 2);
    assert(data.m_collisionBounds.m_extent.b.y == -2 && data.m_collisionBounds.m_extent.t.y == -1);
    assert(seq[0].m_bounds.m_extent.b.y == -5 && seq[0].m_bounds.m_extent.t.y == -2);

    // Mirroring is an involution (the quaternion round trip is exact: 65535 - (65535 - v)).
    const uint16_t q0 = quats[0].m_values[0], q2 = quats[0].m_values[2];
    MirrorData(data);
    assert(verts[0].m_position.y == 2 && sharedKeys[0].y == 10 && sharedKeys[1].y == -20 && globalKeys[0].y == 3);
    assert(quats[0].m_values[0] == static_cast<uint16_t>(65535 - q0) && quats[0].m_values[2] == static_cast<uint16_t>(65535 - q2));
    MirrorData(data);

    // A sequence loaded later from .anim data: its key array now resolves. Aliases share it, so the
    // same Seen list must keep the second alias from flipping it back.
    seq[1].m_flags = 0x20;
    C3Vector* lateKeys = a.take<C3Vector>(1);
    lateKeys[0] = C3Vector(0, 6, 0);
    trans[1].m_count = 1; trans[1].Set(lateKeys);
    M2CompQuat* lateQuats = a.take<M2CompQuat>(1);
    lateQuats[0].m_values[0] = 40000; lateQuats[0].m_values[1] = 20000; lateQuats[0].m_values[2] = 10000; lateQuats[0].m_values[3] = 30000;
    rots[1].m_count = 1; rots[1].Set(lateQuats);
    Seen seen;
    MirrorLoadedSequence(data, 1, seen);
    MirrorLoadedSequence(data, 1, seen);
    assert(lateKeys[0].y == -6);
    assert(lateQuats[0].m_values[0] == 25535 && lateQuats[0].m_values[1] == 20000 && lateQuats[0].m_values[2] == 55535 && lateQuats[0].m_values[3] == 30000);
    // Out of range and global-sequence tracks are ignored.
    MirrorLoadedSequence(data, 9, seen);
    assert(globalKeys[0].y == -3);

    // Skin: triangles reverse winding, section centres flip, partial triangles are rejected untouched.
    Arena b;
    M2SkinProfile& skin = *b.take<M2SkinProfile>(1);
    uint16_t* tris = b.take<uint16_t>(6);
    for (uint16_t i = 0; i < 6; ++i) tris[i] = i;
    skin.m_indices.m_count = 6; skin.m_indices.Set(tris);
    M2SkinSection* sections = b.take<M2SkinSection>(1);
    sections[0].m_centerPosition = C3Vector(1, 2, 3);
    sections[0].m_sortCenterPosition = C3Vector(4, -5, 6);
    skin.m_skinSections.m_count = 1; skin.m_skinSections.Set(sections);
    assert(MirrorSkin(skin));
    const uint16_t expected[6] = {0, 2, 1, 3, 5, 4};
    assert(std::memcmp(tris, expected, sizeof(expected)) == 0);
    assert(sections[0].m_centerPosition.y == -2 && sections[0].m_sortCenterPosition.y == 5);
    assert(MirrorSkin(skin) && tris[1] == 1 && tris[2] == 2);

    skin.m_indices.m_count = 5;
    const uint16_t before[6] = {0, 1, 2, 3, 4, 5};
    assert(!MirrorSkin(skin) && std::memcmp(tris, before, sizeof(before)) == 0 && sections[0].m_centerPosition.y == 2);

    // Empty skin and empty model are fine.
    skin.m_indices.m_count = 0; skin.m_skinSections.m_count = 0;
    assert(MirrorSkin(skin));
    Arena c;
    MirrorData(*c.take<M2Data>(1));
    return 0;
}
