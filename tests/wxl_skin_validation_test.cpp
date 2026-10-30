#include "Engine/Source/ModernAssets/ModernM2Skin.h"
#include "Engine/Source/Model2/M2Data.h"
#include <cassert>
#include <initializer_list>
#include <algorithm>
#include <cstddef>
#include <vector>
#include <fstream>
#include <iterator>
#include <cstdio>
#include <cstring>
#include <array>
#include "Engine/Source/Model2/WxlBoneBudget.h"
#include "Engine/Source/Model2/WxlGeometryBudget.h"

// Capture the production index upload without requiring a graphics context.
struct CGxPool {};
struct CGxBuf {
    bool m_hasData = false, m_valid = true;
    std::vector<uint16_t> indices;
};
constexpr int GxPoolTarget_Index = 0, GxPoolUsage_Dynamic = 0, GxPoolHintBit_Unk1 = 1;
struct CaptureGx {
    CGxPool pool;
    CGxBuf buffer;
    CGxBuf* bound = nullptr;
    CGxPool* PoolCreate(int, int, uint32_t, int, const char*) { return &pool; }
    CGxBuf* BufCreate(CGxPool*, uint32_t size, uint32_t count, int) {
        assert(size == sizeof(uint16_t)); buffer.indices.resize(count); return &buffer;
    }
    void* BufLock(CGxBuf* buffer) { return buffer->indices.data(); }
    void BufUnlock(CGxBuf*, int) {}
    void PrimIndexPtr(CGxBuf* buffer) { bound = buffer; }
};
static CaptureGx* g_theGxDevicePtr;
struct CM2Cache { uint32_t m_flags = 8; };
struct CM2Shared {
    CGxPool* m_indexPool = nullptr;
    CGxBuf* m_indexBuf = nullptr;
    uint32_t m_instances = 1;
    M2SkinProfile* m_skinData;
    CM2Cache* m_cache;
    M2Data* m_data;
    M2SkinSection* m_skinSections;
    const char* m_fileNameWithoutPath = "fixture";
    int32_t SetIndices();
};
#include "Engine/Source/Model2/WxlSkinIndexUpload.inl"

static uint32_t allocations = 0;
static uint8_t* arena = nullptr;
static size_t available = 0;
static std::vector<void*> outstanding;
void* OsAllocNear(const void*, size_t size) {
    ++allocations;
    size = (size + 15) & ~size_t(15);
    if (!arena || size > available) return nullptr;
    void* result = arena;
    arena += size; available -= size;
    outstanding.push_back(result);
    return result;
}
void OsFreeNear(void* pointer) {
    if (!pointer) return;
    auto found = std::find(outstanding.begin(), outstanding.end(), pointer);
    assert(found != outstanding.end());
    outstanding.erase(found);
}

struct Fixture {
    M2Data data{};
    M2SkinProfile skin{};
    M2Vertex vertices[81]{};
    M2CompBone bones[81]{};
    uint16_t vertexLookup[81]{0, 1, 2};
    uint16_t indices[81]{0, 1, 2};
    ubyte4 influences[81]{};
    M2SkinSection sections[1]{};
    M2Batch batches[1]{};
    M2Material materials[1]{};
    uint16_t combos[81]{0};
    alignas(std::max_align_t) uint8_t storage[32768]{};

    Fixture() {
        data.m_vertices.Set(vertices); data.m_vertices.m_count = 3;
        data.m_bones.Set(bones); data.m_bones.m_count = 1;
        data.m_boneCombos.Set(combos); data.m_boneCombos.m_count = 1;
        data.m_materials.Set(materials); data.m_materials.m_count = 1;
        skin.m_vertices.Set(vertexLookup); skin.m_vertices.m_count = 3;
        skin.m_indices.Set(indices); skin.m_indices.m_count = 3;
        skin.m_bones.Set(influences); skin.m_bones.m_count = 3;
        skin.m_skinSections.Set(sections); skin.m_skinSections.m_count = 1;
        skin.m_batches.Set(batches); skin.m_batches.m_count = 1;
        sections[0].m_skinSectionId = 401;
        sections[0].m_vertexCount = 3;
        sections[0].m_indexCount = 3;
        sections[0].m_boneCount = 1;
        sections[0].m_boneInfluences = 1;
    }
    bool Valid() const { return ValidateNativeM2Skin(&data, &skin); }
    void OverBudget() {
        data.m_vertices.m_count = data.m_bones.m_count = data.m_boneCombos.m_count = 81;
        skin.m_vertices.m_count = skin.m_indices.m_count = skin.m_bones.m_count = 81;
        sections[0].m_vertexCount = sections[0].m_indexCount = sections[0].m_boneCount = 81;
        batches[0].m_textureCount = 1;
        batches[0].m_shader = 0;
        for (uint32_t i = 0; i < 81; ++i) {
            vertexLookup[i] = indices[i] = combos[i] = i;
            influences[i].b[0] = i;
            vertices[i].m_weights.b[0] = 255;
        }
    }
};

int main(int argc, char** argv) {
    static_assert(WxlBoneInstances(44)==1 && WxlBoneInstances(25)==3 && WxlBoneInstances(162)==0);
    Fixture fixture;
    uint32_t optimizedIndices = 0;
    assert(WxlAccumulateOptimizedIndices(optimizedIndices, 45000));
    assert(!WxlAccumulateOptimizedIndices(optimizedIndices, 45000)); // Repeated material window.
    assert(optimizedIndices == 45000);
    assert(WxlAccumulateOptimizedIndices(optimizedIndices, 20535));
    assert(!WxlAccumulateOptimizedIndices(optimizedIndices, 1));
    auto otherBatch = fixture.batches[0];
    auto otherSection = fixture.sections[0];
    assert(WxlBatchesMatch(fixture.batches[0], otherBatch, fixture.sections[0], otherSection));
    for (auto field : {&M2Batch::m_shader, &M2Batch::m_textureCount, &M2Batch::m_textureCoordComboIndex,
                       &M2Batch::m_materialLayer}) {
        otherBatch = fixture.batches[0]; ++(otherBatch.*field);
        assert(!WxlBatchesMatch(fixture.batches[0], otherBatch, fixture.sections[0], otherSection));
    }
    otherBatch = fixture.batches[0]; otherBatch.m_flags ^= 1;
    assert(!WxlBatchesMatch(fixture.batches[0], otherBatch, fixture.sections[0], otherSection));
    otherBatch = fixture.batches[0]; otherBatch.m_priorityPlane = 1;
    assert(!WxlBatchesMatch(fixture.batches[0], otherBatch, fixture.sections[0], otherSection));
    otherBatch = fixture.batches[0]; otherSection.m_boneInfluences = 4;
    assert(!WxlBatchesMatch(fixture.batches[0], otherBatch, fixture.sections[0], otherSection));
    otherSection = fixture.sections[0]; ++otherSection.m_boneCount;
    assert(!WxlBatchesMatch(fixture.batches[0], otherBatch, fixture.sections[0], otherSection));
    {
        struct FileSections {
            M2SkinProfile skin{};
            M2SkinSectionFile section{};
            alignas(16) uint8_t storage[512]{};
        } file;
        const uint32_t fileSize = offsetof(FileSections, storage);
        file.skin.m_skinSections.m_count = 1;
        file.skin.m_skinSections.m_offset = offsetof(FileSections, section);
        file.skin.m_indices.m_count = 70000;
        file.section.m_skinSectionId = (1u << 16) | 202;
        file.section.m_indexStart = 1000;
        file.section.m_indexCount = 3;
        arena = file.storage; available = sizeof(file.storage);
        assert(!RelocateNativeM2SkinSections(&fixture.data, &file.skin, fileSize - 1));
        assert(RelocateNativeM2SkinSections(&fixture.data, &file.skin, fileSize));
        assert(file.skin.m_skinSections[0].m_skinSectionId == 202);
        assert(file.skin.m_skinSections[0].m_indexStart == 66536);
        assert(reinterpret_cast<uintptr_t>(file.skin.m_skinSections.Data()) % alignof(M2SkinSection) == 0);
        ReleaseModernM2Data(&fixture.data);
        assert(outstanding.empty());
        file.skin.m_skinSections.m_offset = offsetof(FileSections, section);
        file.section.m_skinSectionId = (2u << 16) | 202;
        assert(!RelocateNativeM2SkinSections(&fixture.data, &file.skin, fileSize));
        assert(outstanding.empty()); // Rejected decoded range retires its allocation.
        arena = nullptr; available = allocations = 0;
    }
    assert(fixture.Valid());
    fixture.sections[0].m_boneCount = 0;
    assert(!fixture.Valid());
    assert(RebuildModernM2Skin(&fixture.data, &fixture.skin, "classic.m2"));
    RegisterModernM2Data(&fixture.data);
    assert(!RebuildModernM2Skin(&fixture.data, &fixture.skin, "modern.m2"));
    assert(allocations == 0); // Rejection precedes render-array allocation.
    ReleaseModernM2Data(&fixture.data);
    fixture.sections[0].m_boneCount = 1;
    fixture.sections[0].m_vertexCount = 4; assert(!fixture.Valid()); fixture.sections[0].m_vertexCount = 3;
    fixture.indices[2] = 3; assert(!fixture.Valid()); fixture.indices[2] = 2;
    fixture.vertexLookup[1] = 3; assert(!fixture.Valid()); fixture.vertexLookup[1] = 1;
    fixture.influences[0].b[0] = 1; assert(!fixture.Valid()); fixture.influences[0].b[0] = 0;
    fixture.combos[0] = 1; assert(!fixture.Valid()); fixture.combos[0] = 0;
    fixture.batches[0].m_materialIndex = 1; assert(!fixture.Valid()); fixture.batches[0].m_materialIndex = 0;
    fixture.sections[0].m_skinSectionId |= 0x10000; assert(!fixture.Valid()); fixture.sections[0].m_skinSectionId = 401;
    fixture.skin.m_bones.m_count = 2; assert(!fixture.Valid()); fixture.skin.m_bones.m_count = 3;
    fixture.sections[0].m_indexCount = 2; assert(!fixture.Valid()); fixture.sections[0].m_indexCount = 3;
    fixture.sections[0].m_boneInfluences = 5; assert(!fixture.Valid()); fixture.sections[0].m_boneInfluences = 1;
    assert(fixture.Valid());
    fixture.sections[0].m_vertexStart = 1; fixture.sections[0].m_vertexCount = 2;
    assert(!fixture.Valid()); // Index zero is valid globally but outside this palette's section.
    fixture.sections[0].m_vertexStart = 0; fixture.sections[0].m_vertexCount = 3;
    RegisterModernM2Data(&fixture.data);
    for (uint16_t shader : {uint16_t(0x8003), uint16_t(0x800F), uint16_t(0x8102)}) {
        fixture.batches[0].m_shader = shader;
        assert(!RebuildModernM2Skin(&fixture.data, &fixture.skin, "missing-follow-material.m2"));
    }
    assert(allocations == 0);
    ReleaseModernM2Data(&fixture.data);

    fixture.OverBudget();
    assert(fixture.Valid());
    RegisterModernM2Data(&fixture.data);
    assert(!RebuildModernM2Skin(&fixture.data, &fixture.skin, "allocation-failure.m2"));
    assert(fixture.skin.m_skinSections.Data() == fixture.sections);
    ReleaseModernM2Data(&fixture.data);

    arena = fixture.storage; available = sizeof(fixture.storage);
    RegisterModernM2Data(&fixture.data);
    assert(RebuildModernM2Skin(&fixture.data, &fixture.skin, "split-palette.m2"));
    assert(fixture.Valid());
    assert(fixture.skin.m_skinSections.m_count == 2 && fixture.skin.m_batches.m_count == 2);
    assert(fixture.skin.m_skinSections[0].m_boneCount == 75);
    assert(fixture.skin.m_skinSections[1].m_boneCount == 6);
    for (uint32_t i = 0; i < 2; ++i) {
        const auto& section = fixture.skin.m_skinSections[i];
        assert(section.m_skinSectionId == 401 && fixture.skin.m_batches[i].m_skinSectionIndex == i);
        for (uint32_t vertex = section.m_vertexStart; vertex < section.m_vertexStart + section.m_vertexCount; ++vertex) {
            const auto local = fixture.skin.m_bones[vertex].b[0];
            assert(fixture.data.m_boneCombos[section.m_boneComboIndex + local] == fixture.skin.m_vertices[vertex]);
        }
    }
    for (uint32_t i = 0; i < 81; ++i)
        assert(fixture.skin.m_vertices[fixture.skin.m_indices[i]] == i);
    ReleaseModernM2Data(&fixture.data);
    assert(outstanding.empty());
    if (argc == 3) {
        auto read = [](const char* name) {
            std::ifstream stream(name, std::ios::binary); assert(stream);
            return std::vector<uint8_t>(std::istreambuf_iterator<char>(stream), {});
        };
        const auto modelBytes=read(argv[1]), skinBytes=read(argv[2]);
        const size_t skinAt=(modelBytes.size()+15)&~size_t(15);
        const size_t workAt=(skinAt+skinBytes.size()+15)&~size_t(15);
        std::vector<uint8_t> storage(workAt+4*1024*1024);
        std::memcpy(storage.data(),modelBytes.data(),modelBytes.size());
        std::memcpy(storage.data()+skinAt,skinBytes.data(),skinBytes.size());
        auto* data=reinterpret_cast<M2Data*>(storage.data());
        auto* skin=reinterpret_cast<M2SkinProfile*>(storage.data()+skinAt);
        assert(data->m_version==264);
        data->m_bones.Relocate(data); data->m_vertices.Relocate(data);
        data->m_boneCombos.Relocate(data); data->m_materials.Relocate(data);
        skin->m_vertices.Relocate(skin); skin->m_indices.Relocate(skin); skin->m_bones.Relocate(skin);
        skin->m_batches.Relocate(skin);
        arena=storage.data()+workAt; available=storage.size()-workAt;
        assert(RelocateNativeM2SkinSections(data,skin,skinBytes.size()));
        auto mappedCorners = [&]() {
            std::vector<std::array<uint32_t, 6>> result;
            for (uint32_t s = 0; s < skin->m_skinSections.m_count; ++s) {
                M2SkinSection section;
                // Source tables may be unaligned until production normalization.
                std::memcpy(&section, reinterpret_cast<const uint8_t*>(skin->m_skinSections.Data()) + s * sizeof(section), sizeof(section));
                for (uint32_t i = section.m_indexStart; i < section.m_indexStart + section.m_indexCount; ++i) {
                    const uint16_t vertex = skin->m_indices[i];
                    std::array<uint32_t, 6> mapped{section.m_skinSectionId, skin->m_vertices[vertex], ~0u, ~0u, ~0u, ~0u};
                    for (uint32_t w = 0; w < section.m_boneInfluences; ++w) {
                        uint16_t bone;
                        const size_t combo = section.m_boneComboIndex + skin->m_bones[vertex].b[w];
                        std::memcpy(&bone, reinterpret_cast<const uint8_t*>(data->m_boneCombos.Data()) + combo * sizeof(bone), sizeof(bone));
                        mapped[w + 2] = bone;
                    }
                    result.push_back(mapped);
                }
            }
            return result;
        };
        const auto originalCorners = mappedCorners();
        const std::vector<M2Batch> nativeBatches(skin->m_batches.Data(),skin->m_batches.Data()+skin->m_batches.m_count);
        assert(RebuildModernM2Skin(data,skin,argv[1])); // Intentionally unregistered/native.
        assert(ValidateNativeM2Skin(data,skin));
        assert(mappedCorners() == originalCorners); // Every corner keeps every bone influence and section ID.
        for(uint32_t i=0;i<skin->m_skinSections.m_count;++i) assert(skin->m_skinSections[i].m_boneCount<=75);
        for(uint32_t i=0;i<skin->m_batches.m_count;++i) {
            const auto& batch=skin->m_batches[i];
            assert(std::any_of(nativeBatches.begin(),nativeBatches.end(),[&](const auto& source){
                auto expected = source;
                expected.m_skinSectionIndex = batch.m_skinSectionIndex;
                return std::memcmp(&expected, &batch, sizeof(batch)) == 0;
            }));
        }
        std::vector<M2SkinSection> drawSections(skin->m_skinSections.Data(),
            skin->m_skinSections.Data() + skin->m_skinSections.m_count);
        std::vector<uint16_t> uploaded;
        for (const auto& section : drawSections)
            uploaded.insert(uploaded.end(), skin->m_indices.Data() + section.m_indexStart,
                            skin->m_indices.Data() + section.m_indexStart + section.m_indexCount);
        CaptureGx capture; g_theGxDevicePtr = &capture;
        CM2Cache cache;
        CM2Shared shared;
        shared.m_skinData = skin; shared.m_cache = &cache; shared.m_data = data;
        shared.m_skinSections = drawSections.data();
        assert(shared.SetIndices() && capture.bound == &capture.buffer);
        assert(uploaded.size() <= capture.buffer.indices.size());
        assert(std::equal(uploaded.begin(), uploaded.end(), capture.buffer.indices.begin()));
        uint32_t drawStart = 0;
        for (const auto& section : drawSections) {
            assert(section.m_indexStart == drawStart); // Includes offsets above 65535.
            assert(std::equal(capture.buffer.indices.begin() + drawStart,
                              capture.buffer.indices.begin() + drawStart + section.m_indexCount,
                              uploaded.begin() + drawStart));
            drawStart += section.m_indexCount;
        }
        std::printf("native skin: %u sections, %u triangles; palette limits/geometry/bone mappings/native batches preserved\n",
                    skin->m_skinSections.m_count,static_cast<uint32_t>(originalCorners.size()/3));
        ReleaseModernM2Data(data); assert(outstanding.empty());
    }
}
