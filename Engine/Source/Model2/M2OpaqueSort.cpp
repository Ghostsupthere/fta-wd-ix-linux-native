#include "Engine/Source/Model2/M2OpaqueSort.h"

#if defined(WOWDECOMP_LINUX)

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <tuple>
#include <vector>

#include "Engine/Source/Model2/M2Cache.h"
#include "Engine/Source/Model2/M2Data.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Model2/M2Scene.h"
#include "Engine/Source/Model2/M2Shared.h"
#include "Engine/Source/ShaderEffect/ShaderEffect.h"

namespace {

constexpr uint32_t kTypes = 8;       // element types 0..7; anything else sorts with the stock sort
constexpr uint32_t kMaxTextures = 4;  // a batch with more textures sorts with the stock sort

struct Key {
    uint64_t layer;
    uint64_t hasEffect;
    uintptr_t vertexShader, pixelShader;
    uintptr_t shared;
    uint64_t optimized;
    uintptr_t model;
    uint64_t boneCombo;
    int64_t blend;
    uint64_t materialFlags;
    uintptr_t texture[kMaxTextures];
    uint64_t textureCount;
    uintptr_t batch;
    uint32_t order;  // position in the pass: final tie-break, keeps the result deterministic
    uint32_t index;  // element index to write back
};

bool Less(const Key& a, const Key& b) {
    if (a.layer != b.layer) return a.layer < b.layer;
    if (a.hasEffect != b.hasEffect) return a.hasEffect < b.hasEffect;
    if (a.vertexShader != b.vertexShader) return a.vertexShader < b.vertexShader;
    if (a.pixelShader != b.pixelShader) return a.pixelShader < b.pixelShader;
    if (a.shared != b.shared) return a.shared < b.shared;
    if (a.optimized != b.optimized) return a.optimized < b.optimized;
    if (a.model != b.model) return a.model < b.model;
    if (a.boneCombo != b.boneCombo) return a.boneCombo < b.boneCombo;
    if (a.blend != b.blend) return a.blend < b.blend;
    if (a.materialFlags != b.materialFlags) return a.materialFlags < b.materialFlags;
    for (uint32_t i = 0; i < kMaxTextures; i++) {
        if (a.texture[i] != b.texture[i]) return a.texture[i] < b.texture[i];
    }
    if (a.textureCount != b.textureCount) return a.textureCount < b.textureCount;
    if (a.batch != b.batch) return a.batch < b.batch;
    return a.order < b.order;
}

// Gathers the key of a type 0 or 1 element the way CM2Scene::SortOpaqueGeoBatches reads it. False when the
// element has more textures than the key holds.
bool BuildKey(const M2Element* e, uint32_t index, uint32_t order, Key& key) {
    const M2Data* data = e->m_model->m_shared->m_data;
    const M2Batch* batch = e->m_batch;
    if (batch->m_textureCount > kMaxTextures) return false;

    std::memset(&key, 0, sizeof(key));

    if (e->m_type == 0) {
        key.layer = batch->m_materialLayer;
        if (e->m_effect) {
            key.hasEffect = 1;
            key.vertexShader = reinterpret_cast<uintptr_t>(e->m_effect->m_vertexShaders[e->m_vertexPermute]);
            key.pixelShader = reinterpret_cast<uintptr_t>(e->m_effect->m_pixelShaders[e->m_pixelPermute]);
        }
        key.shared = reinterpret_cast<uintptr_t>(e->m_model->m_shared);
        key.optimized = e->m_flags & 4;
        key.model = reinterpret_cast<uintptr_t>(e->m_model);
        key.boneCombo = e->m_skinSection->m_boneComboIndex;
    }

    const M2Material* material = &data->m_materials[batch->m_materialIndex];
    key.blend = static_cast<int64_t>(material->m_blendMode);
    key.materialFlags = material->m_flags & 0x1F;

    for (uint32_t i = 0; i < batch->m_textureCount; i++) {
        const uint32_t combo = data->m_textureCombos[batch->m_textureComboIndex + i];
        key.texture[i] = combo >= data->m_textures.m_count ? 0 : reinterpret_cast<uintptr_t>(e->m_model->m_textures[combo]);
    }

    key.textureCount = batch->m_textureCount;
    key.batch = reinterpret_cast<uintptr_t>(batch);
    key.order = order;
    key.index = index;
    return true;
}

bool EnvFlag(const char* name, bool fallback) {
    const char* value = std::getenv(name);
    if (!value || !*value) return fallback;
    return std::strcmp(value, "0") != 0;
}

void Verify(CM2Scene* scene, const uint32_t* before, const uint32_t* after, uint32_t count) {
    static uint32_t calls, violations, mismatches;
    calls++;

    uint64_t sumBefore = 0, sumAfter = 0, xorBefore = 0, xorAfter = 0;
    for (uint32_t i = 0; i < count; i++) {
        sumBefore += before[i]; xorBefore ^= (uint64_t(before[i]) * 0x9E3779B97F4A7C15ull);
        sumAfter += after[i]; xorAfter ^= (uint64_t(after[i]) * 0x9E3779B97F4A7C15ull);
    }
    if (sumBefore != sumAfter || xorBefore != xorAfter) {
        if (++mismatches <= 5) std::fprintf(stderr, "WarcraftXL m2 sort verify: result is not a permutation of the input (%u elements)\n", count);
    }

    uint32_t bad = 0;
    for (uint32_t i = 1; i < count; i++) {
        if (CM2Scene::SortOpaque(after[i - 1], after[i], scene) > 0) bad++;
    }
    if (bad) {
        violations++;
        if (violations <= 5) std::fprintf(stderr, "WarcraftXL m2 sort verify: %u adjacent pairs out of order under the stock comparator (%u elements)\n", bad, count);
    }
    if (calls % 600 == 0) {
        std::fprintf(stderr, "WarcraftXL m2 sort verify: %u sorts checked, %u with out-of-order pairs, %u not permutations\n", calls, violations, mismatches);
    }
}

}  // namespace

void M2SortOpaquePass(CM2Scene* scene) {
    TSGrowableArray<uint32_t>& pass = scene->m_passElements[0];
    const uint32_t count = pass.m_count;
    if (count <= 1) return;

    static const bool enabled = EnvFlag("WXL_M2_KEYED_SORT", true);
    static const bool verify = EnvFlag("WXL_M2_SORT_VERIFY", false);

    auto stock = [&]() { M2HeapSort(&CM2Scene::SortOpaque, pass.m_data, count, scene); };

    if (!enabled) {
        stock();
        return;
    }

    thread_local std::vector<Key> geo[2];
    thread_local std::vector<uint32_t> other[kTypes];
    thread_local std::vector<uint32_t> before;

    geo[0].clear(); geo[1].clear();
    for (auto& list : other) list.clear();
    if (verify) before.assign(pass.m_data, pass.m_data + count);

    for (uint32_t i = 0; i < count; i++) {
        const uint32_t index = pass.m_data[i];
        const M2Element* element = &scene->m_elements.m_data[index];
        const int32_t type = element->m_type;

        if (type < 0 || type >= static_cast<int32_t>(kTypes)) {
            stock();
            return;
        }

        if (type <= 1) {
            Key key;
            if (!BuildKey(element, index, i, key)) {
                stock();
                return;
            }
            geo[type].push_back(key);
        } else {
            other[type].push_back(index);
        }
    }

    // The stock comparator orders by type first, so the buckets concatenate in type order.
    uint32_t out = 0;
    for (uint32_t type = 0; type < kTypes; type++) {
        if (type <= 1) {
            std::vector<Key>& keys = geo[type];
            std::sort(keys.begin(), keys.end(), Less);
            for (const Key& key : keys) pass.m_data[out++] = key.index;
        } else {
            std::vector<uint32_t>& bucket = other[type];
            if (bucket.size() > 1) M2HeapSort(&CM2Scene::SortOpaque, bucket.data(), static_cast<uint32_t>(bucket.size()), scene);
            for (uint32_t index : bucket) pass.m_data[out++] = index;
        }
    }

    if (verify) Verify(scene, before.data(), pass.m_data, count);
}

#endif  // WOWDECOMP_LINUX
