#include "Engine/Source/ModernAssets/ModernM2Skin.h"
#include "Engine/Source/ModernAssets/NativeDiagnostic.h"

#include "Engine/Source/Model2/M2Data.h"
#include "Engine/Source/Model2/WxlBoneBudget.h"
#include "Engine/Source/Os/OsMemory.h"
#include "Engine/Source/Storm/H/storm.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace {
constexpr uint16_t kSourceShaderMin = 0x8000;
constexpr uint32_t kMaxEntries = 0x10000;
constexpr uint32_t kMaxIndices = 16u * 1024u * 1024u;

struct OwnedNearArrays {
    bool modern = false;
    std::vector<void*> allocations;
};

std::mutex s_mutex;
std::unordered_map<M2Data*, OwnedNearArrays> s_owned;

bool IsModern(M2Data* data) {
    std::lock_guard<std::mutex> lock(s_mutex);
    const auto it = s_owned.find(data);
    return it != s_owned.end() && it->second.modern;
}

bool Adopt(M2Data* data, void* const* allocations, size_t count) {
    if (!data) return false;
    size_t nonNull = 0;
    for (size_t i = 0; i < count; ++i) nonNull += allocations[i] != nullptr;
    if (!nonNull) return true;

    try {
        std::lock_guard<std::mutex> lock(s_mutex);
        // Ownership also covers native skins needing palette subdivision;
        // retaining modern=false prevents source shader decoding on them.
        auto& list = s_owned.try_emplace(data).first->second.allocations;
        list.reserve(list.size() + nonNull);
        for (size_t i = 0; i < count; ++i)
            if (allocations[i]) list.push_back(allocations[i]);
        return true;
    } catch (...) {
        return false;
    }
}

uint16_t LookupSingle(std::vector<uint16_t>& lookup, uint16_t value) {
    for (uint32_t i = 0; i < lookup.size(); ++i)
        if (lookup[i] == value) return static_cast<uint16_t>(i);
    if (lookup.size() >= kMaxEntries) return 0xFFFF;
    lookup.push_back(value);
    return static_cast<uint16_t>(lookup.size() - 1);
}

uint16_t LookupPair(std::vector<uint16_t>& lookup, uint16_t first, uint16_t second) {
    for (uint32_t i = 0; i + 1 < lookup.size(); ++i)
        if (lookup[i] == first && lookup[i + 1] == second) return static_cast<uint16_t>(i);
    if (lookup.size() > 1 && lookup.back() == first) {
        if (lookup.size() >= kMaxEntries) return 0xFFFF;
        lookup.push_back(second);
        return static_cast<uint16_t>(lookup.size() - 2);
    }
    if (lookup.size() > kMaxEntries - 2) return 0xFFFF;
    const uint16_t index = static_cast<uint16_t>(lookup.size());
    lookup.push_back(first);
    lookup.push_back(second);
    return index;
}

uint16_t BlendOverride(std::vector<uint16_t>& combos, uint16_t first, uint16_t second) {
    for (uint32_t i = 0; i + 1 < combos.size(); i += 2)
        if (combos[i] == first && combos[i + 1] == second) return static_cast<uint16_t>(i);
    if (combos.size() > kMaxEntries - 2) return 0xFFFF;
    const uint16_t index = static_cast<uint16_t>(combos.size());
    combos.push_back(first);
    combos.push_back(second);
    return index;
}

bool DecodeBatch(M2Batch batch, uint32_t materialCount, uint32_t weightCount, uint32_t transformCount,
                 std::vector<M2Batch>& out, std::vector<uint16_t>& lookup,
                 std::vector<uint16_t>& blendOverrides) {
    uint16_t shader = batch.m_shader;
    uint16_t textureCount = batch.m_textureCount;
    batch.m_flags &= 0x10;

    if (shader >= kSourceShaderMin) {
        const uint16_t sourceEffect = shader & 0x7FFF;
        if (sourceEffect <= 2) {
            const uint16_t combo = LookupPair(lookup, 0, 0xFFFF);
            if (combo == 0xFFFF) return false;
            batch.m_shader = static_cast<uint16_t>(kSourceShaderMin | (sourceEffect + 1));
            batch.m_textureCount = 2;
            batch.m_textureCoordComboIndex = combo;
            out.push_back(batch);
            return true;
        }

        const uint16_t low = shader & 0xFF;
        if (low == 0 || low == 3 || low == 9 || low == 17 || low == 24) {
            if (static_cast<uint32_t>(batch.m_materialIndex) + 1 >= materialCount) return false;
            const uint16_t blend = BlendOverride(blendOverrides, 1, 4);
            const uint16_t combo = LookupPair(lookup, 0, 0xFFFF);
            if (blend == 0xFFFF || combo == 0xFFFF || out.size() > kMaxEntries - 2) return false;
            M2Batch follower = batch;
            batch.m_shader = blend;
            batch.m_textureCount = 2;
            batch.m_textureCoordComboIndex = combo;
            follower.m_shader = blend;
            follower.m_textureCount = 1;
            follower.m_textureCoordComboIndex = combo;
            follower.m_materialIndex = static_cast<uint16_t>(follower.m_materialIndex + 1);
            follower.m_materialLayer = 1;
            out.push_back(batch);
            out.push_back(follower);
            return true;
        }

        if (low == 1 || low == 15) {
            if (static_cast<uint32_t>(batch.m_materialIndex) + 1 >= materialCount) return false;
            const uint16_t combo = LookupPair(lookup, 0, shader == 0x8001 ? 0xFFFF : 1);
            if (combo == 0xFFFF || combo == 0xFFFF - 1 || out.size() > kMaxEntries - 2) return false;
            M2Batch follower = batch;
            batch.m_shader = 0;
            batch.m_textureCount = 1;
            batch.m_textureCoordComboIndex = combo;
            follower.m_shader = 0;
            follower.m_textureCount = 1;
            follower.m_textureCoordComboIndex = static_cast<uint16_t>(combo + 1);
            follower.m_textureComboIndex = static_cast<uint16_t>(follower.m_textureComboIndex + 1);
            if (static_cast<uint32_t>(follower.m_textureWeightComboIndex) + 1 < weightCount)
                follower.m_textureWeightComboIndex = static_cast<uint16_t>(follower.m_textureWeightComboIndex + 1);
            if (static_cast<uint32_t>(follower.m_textureTransformComboIndex) + 1 < transformCount)
                follower.m_textureTransformComboIndex = static_cast<uint16_t>(follower.m_textureTransformComboIndex + 1);
            follower.m_materialIndex = static_cast<uint16_t>(follower.m_materialIndex + 1);
            follower.m_materialLayer = 1;
            out.push_back(batch);
            out.push_back(follower);
            return true;
        }

        if (low == 2) {
            if (static_cast<uint32_t>(batch.m_materialIndex) + 1 >= materialCount) return false;
            const uint16_t blend = BlendOverride(blendOverrides, 1, 3);
            const uint16_t combo = LookupPair(lookup, 0, 0xFFFF);
            if (blend == 0xFFFF || combo == 0xFFFF || out.size() > kMaxEntries - 2) return false;
            M2Batch follower = batch;
            batch.m_shader = blend;
            batch.m_textureCount = 2;
            batch.m_textureCoordComboIndex = combo;
            follower.m_shader = blend;
            follower.m_textureCount = 1;
            follower.m_textureCoordComboIndex = combo;
            follower.m_materialIndex = static_cast<uint16_t>(follower.m_materialIndex + 1);
            follower.m_materialLayer = 1;
            out.push_back(batch);
            out.push_back(follower);
            return true;
        }

        switch (low) {
            case 5: case 8: case 10: case 12: case 16: case 23:
                shader = 0;
                textureCount = 1;
                break;
            case 21:
                shader = 0x4011;
                textureCount = 2;
                break;
            default:
                shader = 0x0010;
                textureCount = 1;
                break;
        }
    }

    const uint16_t blend1 = (shader >> 4) & 0x7;
    const uint16_t blend2 = shader & 0x7;
    const bool twoTexture = textureCount > 1 && (shader & 0x4000) && blend1 && blend2;
    if (twoTexture) {
        const uint16_t blend = BlendOverride(blendOverrides, blend1, blend2);
        if (blend == 0xFFFF) return false;
        batch.m_shader = blend;
    } else {
        batch.m_shader = 0;
        textureCount = std::min<uint16_t>(textureCount, 1);
    }

    batch.m_textureCount = std::min<uint16_t>(textureCount, 2);
    const uint16_t first = (shader & 0x80) ? 0xFFFF : 0;
    uint16_t combo = 0;
    if (batch.m_textureCount <= 1) {
        combo = LookupSingle(lookup, first);
    } else {
        // Keep this exactly in step with WarcraftXL's DecodeBlendBits.  The
        // second texture uses coordinate 1 only for the packed two-texture
        // form; a second source texture without that bit must reuse 0.
        const uint16_t second = (shader & 0x80) ? ((shader & 0x8) ? 0xFFFF : 0)
                                                : ((shader & 0x8) ? 0xFFFF
                                                                  : ((shader & 0x4000) ? 1 : 0));
        combo = LookupPair(lookup, first, second);
    }
    if (combo == 0xFFFF) return false;
    batch.m_textureCoordComboIndex = combo;
    out.push_back(batch);
    return out.size() <= kMaxEntries;
}

template <typename T>
T* AllocateNear(const void* anchor, size_t count) {
    if (!count || count > std::numeric_limits<size_t>::max() / sizeof(T)) return nullptr;
    return static_cast<T*>(OsAllocNear(anchor, count * sizeof(T)));
}

template <typename T>
bool AlignArray(M2Data* owner, M2Array<T>& array, uint32_t maximum = kMaxEntries) {
    if (!array.m_count || reinterpret_cast<uintptr_t>(array.Data()) % alignof(T) == 0) return true;
    if (array.m_count > maximum) return false;
    T* aligned = AllocateNear<T>(&array, array.m_count);
    if (!aligned) return false;
    std::memcpy(aligned, array.Data(), array.m_count * sizeof(T));
    void* allocations[]{aligned};
    if (!Adopt(owner, allocations, 1)) { OsFreeNear(aligned); return false; }
    array.Set(aligned);
    return true;
}

// The native shader can pass at most 75 bones to one M2 draw. Modern and
// customized v264 skins can exceed that palette. Rebuild over-budget sections
// as small triangle runs with local palettes; this is the native-layout
// counterpart of WarcraftXL's BoneBudget.cpp. File index high words have
// already been decoded into the widened native runtime section fields.
bool RebuildBoneBudget(M2Data* data, M2SkinProfile* skin,
                       std::vector<M2Batch>& batches, const char* name) {
    constexpr uint16_t kBoneBudget = WxlShaderBoneBudget;
    if (!data || !skin || skin->m_skinSections.m_count == 0 ||
        skin->m_skinSections.m_count >= kMaxEntries ||
        skin->m_vertices.m_count >= kMaxEntries || skin->m_indices.m_count > kMaxIndices ||
        skin->m_bones.m_count < skin->m_vertices.m_count ||
        data->m_boneCombos.m_count >= kMaxEntries || !skin->m_skinSections.Data() ||
        !skin->m_vertices.Data() || !skin->m_indices.Data() || !skin->m_bones.Data() ||
        !data->m_boneCombos.Data()) {
        return false;
    }

    bool needsSplit = false;
    for (uint32_t i = 0; i < skin->m_skinSections.m_count; ++i) {
        if (skin->m_skinSections[i].m_boneCount > kBoneBudget) {
            needsSplit = true;
            break;
        }
    }
    if (!needsSplit) return false;

    struct Run { uint16_t first; uint16_t count; };
    std::vector<uint16_t> vertices;
    std::vector<ubyte4> bones;
    std::vector<uint16_t> indices;
    std::vector<M2SkinSection> sections;
    std::vector<uint16_t> combos(data->m_boneCombos.Data(),
                                 data->m_boneCombos.Data() + data->m_boneCombos.m_count);
    std::vector<Run> runs(skin->m_skinSections.m_count, { 0, 0 });
    vertices.reserve(skin->m_vertices.m_count);
    bones.reserve(skin->m_vertices.m_count);
    indices.reserve(skin->m_indices.m_count);
    sections.reserve(skin->m_skinSections.m_count);

    for (uint32_t sectionIndex = 0; sectionIndex < skin->m_skinSections.m_count; ++sectionIndex) {
        const M2SkinSection source = skin->m_skinSections[sectionIndex];
        if (source.m_vertexStart > skin->m_vertices.m_count ||
            source.m_vertexCount > skin->m_vertices.m_count - source.m_vertexStart ||
            source.m_indexStart > skin->m_indices.m_count ||
            source.m_indexCount > skin->m_indices.m_count - source.m_indexStart ||
            source.m_indexCount % 3 != 0 || source.m_boneInfluences > 4 ||
            source.m_boneComboIndex > data->m_boneCombos.m_count ||
            source.m_boneCount > data->m_boneCombos.m_count - source.m_boneComboIndex) {
            WxlNativeDiagnostic("modern-m2: invalid bone-budget section in %s", name ? name : "");
            return false;
        }
        const uint16_t runFirst = static_cast<uint16_t>(sections.size());
        uint16_t runCount = 0;
        if (source.m_indexCount == 0) {
            M2SkinSection empty = source;
            empty.m_vertexStart = 0;
            empty.m_vertexCount = 0;
            empty.m_indexStart = 0;
            empty.m_boneCount = std::max<uint16_t>(1, std::min<uint16_t>(source.m_boneCount, kBoneBudget));
            sections.push_back(empty);
            runs[sectionIndex] = { runFirst, 1 };
            continue;
        }

        std::vector<uint16_t> palette;
        const uint16_t influenceCount = std::max<uint16_t>(1, source.m_boneInfluences);
        uint32_t firstTriangle = 0;
        const uint32_t triangleCount = source.m_indexCount / 3;
        auto emit = [&](uint32_t from, uint32_t to) -> bool {
            if (from >= to || palette.empty() || palette.size() > kBoneBudget ||
                sections.size() >= kMaxEntries || combos.size() + palette.size() >= kMaxEntries) return false;
            std::sort(palette.begin(), palette.end());
            palette.erase(std::unique(palette.begin(), palette.end()), palette.end());
            if (palette.empty() || palette.size() > kBoneBudget || vertices.size() >= kMaxEntries ||
                indices.size() >= kMaxIndices) return false;
            const uint16_t comboStart = static_cast<uint16_t>(combos.size());
            combos.insert(combos.end(), palette.begin(), palette.end());
            const uint16_t vertexStart = static_cast<uint16_t>(vertices.size());
            const uint32_t indexStart = static_cast<uint32_t>(indices.size());
            std::unordered_map<uint16_t, uint16_t> localVertices;
            for (uint32_t triangle = from; triangle < to; ++triangle) {
                for (uint32_t corner = 0; corner < 3; ++corner) {
                    const uint16_t oldVertex = skin->m_indices[source.m_indexStart + triangle * 3 + corner];
                    if (oldVertex >= skin->m_vertices.m_count) return false;
                    const auto existing = localVertices.find(oldVertex);
                    uint16_t local = 0;
                    if (existing == localVertices.end()) {
                        if (vertices.size() >= kMaxEntries) return false;
                        if (skin->m_vertices[oldVertex] >= data->m_vertices.m_count) return false;
                        local = static_cast<uint16_t>(vertices.size());
                        localVertices.emplace(oldVertex, local);
                        vertices.push_back(skin->m_vertices[oldVertex]);
                        ubyte4 remapped{};
                        const ubyte4& influences = skin->m_bones[oldVertex];
                        for (uint32_t weight = 0; weight < influenceCount; ++weight) {
                            if (influences[weight] >= source.m_boneCount) return false;
                            const uint16_t global = data->m_boneCombos[source.m_boneComboIndex + influences[weight]];
                            const auto found = std::lower_bound(palette.begin(), palette.end(), global);
                            if (found == palette.end() || *found != global) return false;
                            remapped.b[weight] = static_cast<uint8_t>(found - palette.begin());
                        }
                        bones.push_back(remapped);
                    } else {
                        local = existing->second;
                    }
                    if (indices.size() >= kMaxIndices) return false;
                    indices.push_back(local);
                }
            }
            const size_t vertexCount = vertices.size() - vertexStart;
            const size_t indexCount = indices.size() - indexStart;
            if (!vertexCount || vertexCount >= kMaxEntries || !indexCount || indexCount > kMaxIndices) return false;
            M2SkinSection rebuilt = source;
            rebuilt.m_vertexStart = vertexStart;
            rebuilt.m_vertexCount = static_cast<uint16_t>(vertexCount);
            rebuilt.m_indexStart = indexStart;
            rebuilt.m_indexCount = static_cast<uint32_t>(indexCount);
            rebuilt.m_boneCount = static_cast<uint16_t>(palette.size());
            rebuilt.m_boneComboIndex = comboStart;
            rebuilt.m_boneInfluences = std::max<uint16_t>(1, rebuilt.m_boneInfluences);
            sections.push_back(rebuilt);
            ++runCount;
            return true;
        };

        for (uint32_t triangle = 0; triangle < triangleCount; ++triangle) {
            std::vector<uint16_t> triangleBones;
            for (uint32_t corner = 0; corner < 3; ++corner) {
                const uint16_t oldVertex = skin->m_indices[source.m_indexStart + triangle * 3 + corner];
                if (oldVertex >= skin->m_vertices.m_count) return false;
                const ubyte4& influences = skin->m_bones[oldVertex];
                for (uint32_t weight = 0; weight < influenceCount; ++weight) {
                    if (influences[weight] >= source.m_boneCount) return false;
                    triangleBones.push_back(data->m_boneCombos[source.m_boneComboIndex + influences[weight]]);
                }
            }
            std::sort(triangleBones.begin(), triangleBones.end());
            triangleBones.erase(std::unique(triangleBones.begin(), triangleBones.end()), triangleBones.end());
            size_t combined = palette.size();
            for (uint16_t bone : triangleBones)
                if (std::find(palette.begin(), palette.end(), bone) == palette.end()) ++combined;
            if (combined > kBoneBudget && triangle > firstTriangle) {
                if (!emit(firstTriangle, triangle)) return false;
                palette.clear();
                firstTriangle = triangle;
            }
            palette.insert(palette.end(), triangleBones.begin(), triangleBones.end());
            std::sort(palette.begin(), palette.end());
            palette.erase(std::unique(palette.begin(), palette.end()), palette.end());
        }
        if (!emit(firstTriangle, triangleCount) || !runCount) return false;
        runs[sectionIndex] = { runFirst, runCount };
    }

    std::vector<M2Batch> expanded;
    expanded.reserve(batches.size());
    for (const M2Batch& batch : batches) {
        if (batch.m_skinSectionIndex >= runs.size()) return false;
        const Run run = runs[batch.m_skinSectionIndex];
        if (!run.count || run.first + run.count > sections.size() ||
            expanded.size() + run.count >= kMaxEntries) return false;
        for (uint16_t i = 0; i < run.count; ++i) {
            M2Batch copy = batch;
            copy.m_skinSectionIndex = static_cast<uint16_t>(run.first + i);
            expanded.push_back(copy);
        }
    }
    if (expanded.empty()) return false;

    uint16_t* vertexBuffer = AllocateNear<uint16_t>(skin, vertices.size());
    ubyte4* boneBuffer = AllocateNear<ubyte4>(skin, bones.size());
    uint16_t* indexBuffer = AllocateNear<uint16_t>(skin, indices.size());
    M2SkinSection* sectionBuffer = AllocateNear<M2SkinSection>(skin, sections.size());
    uint16_t* comboBuffer = AllocateNear<uint16_t>(data, combos.size());
    if (!vertexBuffer || !boneBuffer || !indexBuffer || !sectionBuffer || !comboBuffer) {
        OsFreeNear(vertexBuffer); OsFreeNear(boneBuffer); OsFreeNear(indexBuffer);
        OsFreeNear(sectionBuffer); OsFreeNear(comboBuffer);
        return false;
    }
    std::memcpy(vertexBuffer, vertices.data(), vertices.size() * sizeof(*vertexBuffer));
    std::memcpy(boneBuffer, bones.data(), bones.size() * sizeof(*boneBuffer));
    std::memcpy(indexBuffer, indices.data(), indices.size() * sizeof(*indexBuffer));
    std::memcpy(sectionBuffer, sections.data(), sections.size() * sizeof(*sectionBuffer));
    std::memcpy(comboBuffer, combos.data(), combos.size() * sizeof(*comboBuffer));
    void* allocations[] = { vertexBuffer, boneBuffer, indexBuffer, sectionBuffer, comboBuffer };
    if (!Adopt(data, allocations, sizeof(allocations) / sizeof(allocations[0]))) {
        OsFreeNear(vertexBuffer); OsFreeNear(boneBuffer); OsFreeNear(indexBuffer);
        OsFreeNear(sectionBuffer); OsFreeNear(comboBuffer);
        return false;
    }
    skin->m_vertices.Set(vertexBuffer);
    skin->m_vertices.m_count = static_cast<uint32_t>(vertices.size());
    skin->m_bones.Set(boneBuffer);
    skin->m_bones.m_count = static_cast<uint32_t>(bones.size());
    skin->m_indices.Set(indexBuffer);
    skin->m_indices.m_count = static_cast<uint32_t>(indices.size());
    skin->m_skinSections.Set(sectionBuffer);
    skin->m_skinSections.m_count = static_cast<uint32_t>(sections.size());
    skin->m_boneCountMax = kBoneBudget;
    data->m_boneCombos.Set(comboBuffer);
    data->m_boneCombos.m_count = static_cast<uint32_t>(combos.size());
    batches.swap(expanded);
    WxlNativeDiagnostic("modern-m2: split bone palettes in %s (%u sections)", name ? name : "",
             static_cast<uint32_t>(sections.size()));
    return true;
}
}  // namespace

bool RelocateNativeM2SkinSections(M2Data* data, M2SkinProfile* skin, uint32_t skinSize) {
    if (!data || !skin || skinSize < sizeof(M2SkinProfile) || skin->m_indices.m_count > kMaxIndices) return false;
    auto& array = skin->m_skinSections;
    const uint32_t count = array.m_count, offset = array.m_offset;
    if (count > kMaxEntries || offset > skinSize ||
        uint64_t(count) * sizeof(M2SkinSectionFile) > skinSize - offset) return false;
    if (!count) { array.Set(nullptr); return true; }
    auto* sections = AllocateNear<M2SkinSection>(skin, count);
    if (!sections) return false;
    for (uint32_t i = 0; i < count; ++i) {
        M2SkinSectionFile source;
        std::memcpy(&source, reinterpret_cast<const uint8_t*>(skin) + offset + i * sizeof(source), sizeof(source));
        auto& target = sections[i];
        target.m_skinSectionId = source.m_skinSectionId & 0xFFFFu;
        target.m_vertexStart = source.m_vertexStart;
        target.m_vertexCount = source.m_vertexCount;
        target.m_indexStart = (source.m_skinSectionId & 0xFFFF0000u) | source.m_indexStart;
        target.m_indexCount = source.m_indexCount;
        target.m_boneCount = source.m_boneCount;
        target.m_boneComboIndex = source.m_boneComboIndex;
        target.m_boneInfluences = source.m_boneInfluences;
        target.m_centerBoneIndex = source.m_centerBoneIndex;
        target.m_centerPosition = source.m_centerPosition;
        target.m_sortCenterPosition = source.m_sortCenterPosition;
        target.m_sortRadius = source.m_sortRadius;
        if (target.m_indexStart > skin->m_indices.m_count ||
            target.m_indexCount > skin->m_indices.m_count - target.m_indexStart) {
            OsFreeNear(sections);
            return false;
        }
    }
    void* allocations[]{sections};
    if (!Adopt(data, allocations, 1)) { OsFreeNear(sections); return false; }
    array.Set(sections);
    return true;
}

bool ValidateNativeM2Skin(const M2Data* data, const M2SkinProfile* skin) {
    if (!data || !skin || skin->m_bones.m_count < skin->m_vertices.m_count) return false;
    for (uint32_t i = 0; i < skin->m_skinSections.m_count; ++i) {
        const M2SkinSection& section = skin->m_skinSections[i];
        // File level/high-word metadata must already have been separated
        // from the runtime geoset ID by RelocateNativeM2SkinSections.
        if ((section.m_skinSectionId >> 16) || !section.m_boneCount || section.m_boneInfluences > 4 ||
            section.m_vertexStart > skin->m_vertices.m_count ||
            section.m_vertexCount > skin->m_vertices.m_count - section.m_vertexStart ||
            section.m_indexStart > skin->m_indices.m_count ||
            section.m_indexCount > skin->m_indices.m_count - section.m_indexStart ||
            section.m_indexCount % 3 || section.m_boneComboIndex > data->m_boneCombos.m_count ||
            section.m_boneCount > data->m_boneCombos.m_count - section.m_boneComboIndex) return false;
        const uint32_t end = section.m_vertexStart + section.m_vertexCount;
        for (uint32_t vertex = section.m_vertexStart; vertex < end; ++vertex) {
            if (skin->m_vertices[vertex] >= data->m_vertices.m_count) return false;
            for (uint32_t weight = 0; weight < section.m_boneInfluences; ++weight) {
                const uint8_t local = skin->m_bones[vertex].b[weight];
                if (local >= section.m_boneCount ||
                    data->m_boneCombos[section.m_boneComboIndex + local] >= data->m_bones.m_count) return false;
            }
        }
        for (uint32_t index = section.m_indexStart; index < section.m_indexStart + section.m_indexCount; ++index) {
            if (skin->m_indices[index] < section.m_vertexStart || skin->m_indices[index] >= end) return false;
        }
    }
    for (uint32_t i = 0; i < skin->m_batches.m_count; ++i) {
        if (skin->m_batches[i].m_skinSectionIndex >= skin->m_skinSections.m_count ||
            skin->m_batches[i].m_materialIndex >= data->m_materials.m_count) return false;
    }
    return true;
}

bool StoreNativeM2TextureCombos(M2Data* data, const uint16_t* combos, uint32_t comboCount,
                               const uint16_t* transforms, uint32_t transformCount) {
    if (!data || comboCount > kMaxEntries || transformCount > kMaxEntries ||
        (comboCount && !combos) || (transformCount && !transforms)) return false;
    // Never relocate the model allocation: its external relative arrays, live
    // users and s_owned key all depend on this address. Grow only the tables.
    auto* newCombos = comboCount > data->m_textureCombos.m_count ?
        AllocateNear<uint16_t>(&data->m_textureCombos, comboCount) : nullptr;
    auto* newTransforms = transformCount > data->m_textureTransformCombos.m_count ?
        AllocateNear<uint16_t>(&data->m_textureTransformCombos, transformCount) : nullptr;
    if ((comboCount > data->m_textureCombos.m_count && !newCombos) ||
        (transformCount > data->m_textureTransformCombos.m_count && !newTransforms)) {
        OsFreeNear(newCombos); OsFreeNear(newTransforms); return false;
    }
    void* allocations[]{newCombos, newTransforms};
    if (!Adopt(data, allocations, 2)) {
        OsFreeNear(newCombos); OsFreeNear(newTransforms); return false;
    }
    if (newCombos) data->m_textureCombos.Set(newCombos);
    if (newTransforms) data->m_textureTransformCombos.Set(newTransforms);
    if (comboCount) std::memcpy(data->m_textureCombos.Data(), combos, comboCount * sizeof(uint16_t));
    if (transformCount) std::memcpy(data->m_textureTransformCombos.Data(), transforms, transformCount * sizeof(uint16_t));
    data->m_textureCombos.m_count = comboCount;
    data->m_textureTransformCombos.m_count = transformCount;
    return true;
}

void RegisterModernM2Data(M2Data* data) {
    if (!data) return;
    std::lock_guard<std::mutex> lock(s_mutex);
    s_owned.try_emplace(data).first->second.modern = true;
}

void ReleaseModernM2Data(M2Data* data) {
    if (!data) return;
    std::vector<void*> allocations;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        const auto it = s_owned.find(data);
        if (it == s_owned.end()) return;
        allocations.swap(it->second.allocations);
        s_owned.erase(it);
    }
    for (void* allocation : allocations) OsFreeNear(allocation);
}

bool RebuildModernM2Skin(M2Data* data, M2SkinProfile* skin, const char* name) {
    if (!data || !skin) return false;
    // Installed v264 files may place typed arrays at unaligned file offsets.
    // Normalize before even scanning sections, including skins needing no split.
    if (!AlignArray(data, skin->m_vertices) || !AlignArray(data, skin->m_indices, kMaxIndices) ||
        !AlignArray(data, skin->m_bones) || !AlignArray(data, skin->m_skinSections) ||
        !AlignArray(data, skin->m_batches) || !AlignArray(data, data->m_boneCombos)) return false;
    if (!IsModern(data)) {
        bool needsSplit = false;
        for (uint32_t i = 0; i < skin->m_skinSections.m_count; ++i)
            needsSplit |= skin->m_skinSections[i].m_boneCount > WxlShaderBoneBudget;
        if (!needsSplit) return true;
        if (!ValidateNativeM2Skin(data, skin)) {
            WxlNativeDiagnostic("native-m2: rejected unsafe over-budget skin in %s", name ? name : "");
            return false;
        }
        // Native v264 base characters can also retain a full 141/162-bone
        // palette. Split geometry only: their shader batches are already native.
        std::vector<M2Batch> batches(skin->m_batches.Data(), skin->m_batches.Data() + skin->m_batches.m_count);
        if (!RebuildBoneBudget(data, skin, batches, name)) {
            WxlNativeDiagnostic("native-m2: required bone palette repair failed for %s", name ? name : "");
            return false;
        }
        M2Batch* buffer = AllocateNear<M2Batch>(skin, batches.size());
        if (!buffer) return false;
        std::memcpy(buffer, batches.data(), batches.size() * sizeof(M2Batch));
        void* allocations[]{buffer};
        if (!Adopt(data, allocations, 1)) { OsFreeNear(buffer); return false; }
        skin->m_batches.Set(buffer);
        skin->m_batches.m_count = static_cast<uint32_t>(batches.size());
        return ValidateNativeM2Skin(data, skin);
    }
    if (!ValidateNativeM2Skin(data, skin)) {
        WxlNativeDiagnostic("modern-m2: rejected unsafe skin references in %s", name ? name : "");
        return false;
    }
    if (!skin->m_batches.m_count) return true;
    if (!skin->m_batches.Data() ||
        skin->m_batches.m_count > kMaxEntries || data->m_materials.m_count > kMaxEntries ||
        data->m_textureWeightCombos.m_count > kMaxEntries ||
        data->m_textureTransformCombos.m_count > kMaxEntries) {
        return false;
    }

    std::vector<M2Batch> batches;
    std::vector<uint16_t> lookup;
    std::vector<uint16_t> blendOverrides;
    batches.reserve(skin->m_batches.m_count);
    for (uint32_t i = 0; i < skin->m_batches.m_count; ++i) {
        if (!DecodeBatch(skin->m_batches[i], data->m_materials.m_count, data->m_textureWeightCombos.m_count,
                         data->m_textureTransformCombos.m_count, batches, lookup, blendOverrides)) {
            WxlNativeDiagnostic("modern-m2: skin batch rebuild rejected %s", name ? name : "");
            return false;
        }
    }
    if (batches.empty() || batches.size() > kMaxEntries || lookup.empty()) return false;

    bool needsSplit = false;
    for (uint32_t i = 0; i < skin->m_skinSections.m_count; ++i)
        needsSplit |= skin->m_skinSections[i].m_boneCount > WxlShaderBoneBudget;
    if (needsSplit && !RebuildBoneBudget(data, skin, batches, name)) {
        WxlNativeDiagnostic("modern-m2: required bone palette split failed for %s", name ? name : "");
        return false;
    }
    if (batches.empty() || batches.size() > kMaxEntries) return false;

    M2Batch* batchBuffer = AllocateNear<M2Batch>(skin, batches.size());
    uint16_t* lookupBuffer = AllocateNear<uint16_t>(data, lookup.size());
    uint16_t* blendBuffer = blendOverrides.empty() ? nullptr
        : AllocateNear<uint16_t>(data, blendOverrides.size());
    if (!batchBuffer || !lookupBuffer || (!blendOverrides.empty() && !blendBuffer)) {
        OsFreeNear(batchBuffer);
        OsFreeNear(lookupBuffer);
        OsFreeNear(blendBuffer);
        return false;
    }
    std::memcpy(batchBuffer, batches.data(), batches.size() * sizeof(M2Batch));
    std::memcpy(lookupBuffer, lookup.data(), lookup.size() * sizeof(uint16_t));
    if (blendBuffer)
        std::memcpy(blendBuffer, blendOverrides.data(), blendOverrides.size() * sizeof(uint16_t));

    void* allocations[] = { batchBuffer, lookupBuffer, blendBuffer };
    if (!Adopt(data, allocations, sizeof(allocations) / sizeof(allocations[0]))) {
        OsFreeNear(batchBuffer);
        OsFreeNear(lookupBuffer);
        OsFreeNear(blendBuffer);
        return false;
    }

    skin->m_batches.Set(batchBuffer);
    skin->m_batches.m_count = static_cast<uint32_t>(batches.size());
    data->m_textureCoordCombos.Set(lookupBuffer);
    data->m_textureCoordCombos.m_count = static_cast<uint32_t>(lookup.size());
    if (blendBuffer) {
        data->m_textureCombinerCombos.Set(blendBuffer);
        data->m_textureCombinerCombos.m_count = static_cast<uint32_t>(blendOverrides.size());
        data->m_flags |= 0x8;
    }
    for (uint32_t i = 0; i < data->m_materials.m_count; ++i) {
        M2Material& material = data->m_materials[i];
        if (material.m_blendMode > 6) {
            material.m_blendMode = static_cast<M2BLEND>(4);
            material.m_flags |= 0x5;
        }
        material.m_flags &= 0x1F;
    }
    if (!ValidateNativeM2Skin(data, skin)) {
        WxlNativeDiagnostic("modern-m2: rejected unsafe rebuilt skin in %s", name ? name : "");
        return false;
    }
    WxlNativeDiagnostic("modern-m2: rebuilt skin material contract %s (%u batches)",
             name ? name : "", static_cast<uint32_t>(batches.size()));
    return true;
}
