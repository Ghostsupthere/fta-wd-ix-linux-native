#ifndef ENGINE_SOURCE_MODEL2_WXLMODELMIRROR_H
#define ENGINE_SOURCE_MODEL2_WXLMODELMIRROR_H

#include <algorithm>
#include <cstdint>
#include <vector>
#include "Engine/Source/Model2/M2Data.h"

// Geometric Y-axis mirror of a loaded model, used for the weapon sidecar's offhand mirror
// (WXLWeaponModels.csv "NoAutoMirror"). The Windows module rewrote the model and skin bytes into a
// virtual file; the native client mirrors its own private cache entry in memory instead, between
// M2Init and the first use of the data. Scope follows the supplied VirtualPath.cpp
// (MirrorModelBytes / MirrorSkinWinding): vertices, bone pivots and translation/rotation keys,
// attachments, bounds, collision data and the skin's triangle winding; scale tracks are
// unsigned magnitudes and stay. Emitter orientation tracks (particle latitude/longitude) are not
// mirrored, as upstream does not.
namespace WxlModelMirror {
// CM2Scene::CreateModel flag: the returned model uses a mirrored copy of the shared data, cached
// separately from the unmirrored one.
constexpr uint32_t kCreateFlag = 0x20000000;
// CM2Shared::m_flags bit set on that mirrored copy.
constexpr uint32_t kSharedFlag = 0x00010000;

// Embedded animation key arrays can be shared by sequence aliases; each must be mirrored once.
using Seen = std::vector<const void*>;

namespace detail {
inline void NegateY(C3Vector& v) { v.y = -v.y; }

// A box's Y range [min,max] becomes [-max,-min].
inline void MirrorBox(CAaBox& box) {
    const float low = -box.t.y;
    const float high = -box.b.y;
    box.b.y = low;
    box.t.y = high;
}

inline void MirrorKey(C3Vector& key) { NegateY(key); }

// Mirroring m = diag(1,-1,1) turns a rotation R into m*R*m: x and z flip sign, y and w stay. The
// engine decodes a component as value * 0.000030518044 - 1 (value / 32767.5 - 1), so negating the
// component is 65535 - value.
inline void MirrorKey(M2CompQuat& key) {
    key.m_values[0] = static_cast<uint16_t>(65535u - key.m_values[0]);
    key.m_values[2] = static_cast<uint16_t>(65535u - key.m_values[2]);
}

template <class T>
void MirrorKeyArray(M2Array<T>& keys, Seen& seen) {
    if (!keys.m_count) return;
    T* data = keys.Data();
    if (std::find(seen.begin(), seen.end(), static_cast<const void*>(data)) != seen.end()) return;
    seen.push_back(data);
    for (uint32_t i = 0; i < keys.m_count; ++i) MirrorKey(data[i]);
}

// Tracks stored per sequence only have resolved key arrays for embedded sequences (flag 0x20);
// the others point into .anim data that is not resident. Global-sequence tracks always resolve.
template <class T>
void MirrorTrack(const M2Data& data, M2Track<T>& track, Seen& seen) {
    const bool perSequence = track.m_loopIndex == 0xFFFF;
    for (uint32_t i = 0; i < track.m_sequenceKeys.m_count; ++i) {
        if (perSequence && (i >= data.m_sequences.m_count || !(data.m_sequences[i].m_flags & 0x20))) continue;
        MirrorKeyArray(track.m_sequenceKeys[i], seen);
    }
}
} // namespace detail

// Mirrors model data that has been through M2Init. Every array is read through its resolved
// accessor, so this must run after M2Init and before the model is initialised for drawing.
inline void MirrorData(M2Data& data) {
    Seen seen;

    for (uint32_t i = 0; i < data.m_vertices.m_count; ++i) {
        M2Vertex& vertex = data.m_vertices[i];
        detail::NegateY(vertex.m_position);
        detail::NegateY(vertex.m_normal);
    }

    for (uint32_t i = 0; i < data.m_bones.m_count; ++i) {
        M2CompBone& bone = data.m_bones[i];
        detail::NegateY(bone.m_pivot);
        detail::MirrorTrack(data, bone.m_translationTrack, seen);
        detail::MirrorTrack(data, bone.m_rotationTrack, seen);
    }

    for (uint32_t i = 0; i < data.m_attachments.m_count; ++i) detail::NegateY(data.m_attachments[i].m_position);
    for (uint32_t i = 0; i < data.m_events.m_count; ++i) detail::NegateY(data.m_events[i].m_position);
    for (uint32_t i = 0; i < data.m_lights.m_count; ++i) detail::NegateY(data.m_lights[i].m_position);
    for (uint32_t i = 0; i < data.m_ribbons.m_count; ++i) detail::NegateY(data.m_ribbons[i].m_position);
    for (uint32_t i = 0; i < data.m_particles.m_count; ++i) {
        detail::NegateY(data.m_particles[i].m_position);
        detail::NegateY(data.m_particles[i].m_windVector);
    }

    detail::MirrorBox(data.m_bounds.m_extent);
    detail::MirrorBox(data.m_collisionBounds.m_extent);
    for (uint32_t i = 0; i < data.m_sequences.m_count; ++i) detail::MirrorBox(data.m_sequences[i].m_bounds.m_extent);
    for (uint32_t i = 0; i < data.m_collisionPositions.m_count; ++i) detail::NegateY(data.m_collisionPositions[i]);
    for (uint32_t i = 0; i < data.m_collisionFaceNormals.m_count; ++i) detail::NegateY(data.m_collisionFaceNormals[i]);
}

// Mirrors the keys of one animation sequence that has just been loaded from its .anim data (the
// sequence's flag already carries 0x20). Aliases of one sequence share a buffer: pass the same
// `seen` for all of them.
inline void MirrorLoadedSequence(M2Data& data, uint32_t sequenceIndex, Seen& seen) {
    if (sequenceIndex >= data.m_sequences.m_count) return;
    for (uint32_t i = 0; i < data.m_bones.m_count; ++i) {
        M2CompBone& bone = data.m_bones[i];
        if (bone.m_translationTrack.m_loopIndex == 0xFFFF && sequenceIndex < bone.m_translationTrack.m_sequenceKeys.m_count)
            detail::MirrorKeyArray(bone.m_translationTrack.m_sequenceKeys[sequenceIndex], seen);
        if (bone.m_rotationTrack.m_loopIndex == 0xFFFF && sequenceIndex < bone.m_rotationTrack.m_sequenceKeys.m_count)
            detail::MirrorKeyArray(bone.m_rotationTrack.m_sequenceKeys[sequenceIndex], seen);
    }
}

// Reverses every triangle's winding (a mirror flips facing) and mirrors section centres. Run on a
// loaded skin before it is initialised. Returns false, leaving the skin untouched, when the
// triangle list is not whole triangles.
inline bool MirrorSkin(M2SkinProfile& skin) {
    if (skin.m_indices.m_count % 3 != 0) return false;
    uint16_t* triangles = skin.m_indices.m_count ? skin.m_indices.Data() : nullptr;
    for (uint32_t i = 0; i + 2 < skin.m_indices.m_count; i += 3) std::swap(triangles[i + 1], triangles[i + 2]);
    for (uint32_t i = 0; i < skin.m_skinSections.m_count; ++i) {
        M2SkinSection& section = skin.m_skinSections[i];
        detail::NegateY(section.m_centerPosition);
        detail::NegateY(section.m_sortCenterPosition);
    }
    return true;
}
} // namespace WxlModelMirror

#endif
