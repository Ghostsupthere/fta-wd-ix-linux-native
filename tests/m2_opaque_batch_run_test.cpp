#include "Engine/Source/Model2/M2OpaqueBatchRun.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <numeric>
#include <vector>


struct Element {
    int32_t m_type = 0;
    uint32_t m_flags = 0;
    const void* m_model = nullptr;
    float m_alpha = 1.0f;
    const void* m_effect = nullptr;
    uint32_t m_vertexPermute = 0;
    uint32_t m_pixelPermute = 0;
    uint32_t m_shadowValue = 0;
    int32_t m_priorityPlane = 0;
    const M2Batch* m_batch = nullptr;
    const M2SkinSection* m_skinSection = nullptr;
};

int main() {
    constexpr uint32_t count = 128;
    constexpr uint32_t indexCapacity = 140000;
    std::array<M2Batch, count> batches{};
    std::array<M2SkinSection, count> sections{};
    std::array<Element, count> elements{};
    std::array<uint32_t, count> order;
    std::iota(order.begin(), order.end(), 0);

    // Two consecutive matching sections for each material, at offsets above the legacy 16-bit range.
    for (uint32_t i = 0; i < count; ++i) {
        batches[i].m_skinSectionIndex = i;
        batches[i].m_materialIndex = i / 2;
        sections[i].m_indexStart = 65538 + i * 6;
        sections[i].m_indexCount = 6;
        sections[i].m_vertexStart = i * 4;
        sections[i].m_vertexCount = 4;
        sections[i].m_boneCount = 4;
        sections[i].m_boneComboIndex = 12;
        elements[i].m_batch = &batches[i];
        elements[i].m_skinSection = &sections[i];
    }

    std::vector<uint32_t> baseline;
    std::vector<uint32_t> candidate;
    for (const auto& section : sections) {
        for (uint32_t i = 0; i < section.m_indexCount; ++i) baseline.push_back(section.m_indexStart + i);
    }
    uint32_t draws = 0;
    for (uint32_t i = 0; i < count;) {
        const auto run = M2OpaqueBatchRun::Gather(elements.data(), order.data() + i, count - i, indexCapacity);
        assert(run.elements == 2 && run.indexCount == 12);
        assert(run.maxIndex == sections[i + 1].m_vertexStart + sections[i + 1].m_vertexCount - 1u);
        for (uint32_t j = 0; j < run.indexCount; ++j) candidate.push_back(sections[i].m_indexStart + j);
        i += run.elements;
        ++draws;
    }
    assert(candidate == baseline && draws == 64);
    std::printf("Opaque paired fixture: %u -> %u submissions, %zu triangles in unchanged order\n",
                count, draws, candidate.size() / 3);

    auto rejects = [&] {
        return M2OpaqueBatchRun::Gather(elements.data(), order.data(), count, indexCapacity).elements == 1;
    };
    const auto originalElement = elements[1];
    elements[1].m_flags = 4;
    assert(rejects());
    elements[1] = originalElement;
    elements[1].m_model = &elements;
    assert(rejects());
    elements[1] = originalElement;
    elements[1].m_alpha = 0.75f;
    assert(rejects());
    elements[1] = originalElement;
    elements[1].m_shadowValue = 1;
    assert(rejects());
    elements[1] = originalElement;
    elements[1].m_vertexPermute = 1;
    assert(rejects());
    elements[1] = originalElement;
    batches[1].m_textureComboIndex = 13;
    assert(rejects());
    batches[1].m_textureComboIndex = 0;

    auto originalSection = sections[1];
    ++sections[1].m_indexStart;
    assert(rejects());
    sections[1] = originalSection;
    sections[1].m_vertexCount = 0;
    assert(rejects());
    sections[1] = originalSection;
    ++sections[1].m_boneComboIndex;
    assert(rejects());
    sections[1] = originalSection;
    sections[1].m_indexCount = 5;
    assert(rejects());
    sections[1] = originalSection;
    assert(M2OpaqueBatchRun::Gather(elements.data(), order.data(), count, sections[1].m_indexStart).elements == 1);

    for (auto& batch : batches) batch.m_materialIndex = 0;
    assert(M2OpaqueBatchRun::Gather(elements.data(), order.data(), count, indexCapacity).elements == 64);

    sections[0].m_indexCount = 65535;
    sections[1].m_indexStart = sections[0].m_indexStart + sections[0].m_indexCount;
    assert(rejects());
}
