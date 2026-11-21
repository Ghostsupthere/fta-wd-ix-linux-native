#pragma once

#include "Engine/Source/Model2/M2Data.h"

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace M2OpaqueBatchRun {

struct Run {
    uint32_t elements;
    uint32_t indexCount;
    uint32_t maxIndex;
};

inline bool SameBatch(const M2Batch& a, const M2Batch& b) {
    static_assert(sizeof(M2Batch) == 24 && offsetof(M2Batch, m_skinSectionIndex) == 4);
    constexpr std::size_t SectionOffset = offsetof(M2Batch, m_skinSectionIndex);
    constexpr std::size_t AfterSection = SectionOffset + sizeof(a.m_skinSectionIndex);
    return std::memcmp(&a, &b, SectionOffset) == 0 &&
        std::memcmp(reinterpret_cast<const char*>(&a) + AfterSection,
                    reinterpret_cast<const char*>(&b) + AfterSection, sizeof(a) - AfterSection) == 0;
}

// The caller supplies a draw-ready first section and an uploaded shared index buffer.
template <class Element>
Run Gather(const Element* elements, const uint32_t* order, uint32_t count, uint32_t indexCapacity) {
    const Element& first = elements[order[0]];
    const M2SkinSection& section = *first.m_skinSection;
    const uint32_t start = section.m_indexStart;
    uint32_t maxVertex = uint32_t{section.m_vertexStart} + section.m_vertexCount;
    const Run single{1, section.m_indexCount, maxVertex - 1};

    if (!section.m_vertexCount || maxVertex > 65536 || !section.m_indexCount || section.m_indexCount % 3 ||
        section.m_indexCount > 65535 || start > indexCapacity || section.m_indexCount > indexCapacity - start) {
        return single;
    }

    uint32_t consumed = 1;
    uint32_t end = start + section.m_indexCount;
    for (; consumed < std::min(count, 64u); ++consumed) {
        const Element& next = elements[order[consumed]];
        if (next.m_type || next.m_flags != first.m_flags || next.m_model != first.m_model ||
            next.m_alpha != first.m_alpha || next.m_effect != first.m_effect ||
            next.m_vertexPermute != first.m_vertexPermute || next.m_pixelPermute != first.m_pixelPermute ||
            next.m_shadowValue != first.m_shadowValue || next.m_priorityPlane != first.m_priorityPlane ||
            !next.m_batch || !next.m_skinSection || !SameBatch(*first.m_batch, *next.m_batch)) {
            break;
        }

        const M2SkinSection& part = *next.m_skinSection;
        const uint32_t partMax = uint32_t{part.m_vertexStart} + part.m_vertexCount;
        if (part.m_indexStart != end || !part.m_indexCount || part.m_indexCount % 3 ||
            part.m_indexCount > 65535u - (end - start) || part.m_indexCount > indexCapacity - end ||
            !part.m_vertexCount || part.m_vertexStart < section.m_vertexStart || partMax > 65536 ||
            part.m_boneCount != section.m_boneCount || part.m_boneComboIndex != section.m_boneComboIndex) {
            break;
        }

        end += part.m_indexCount;
        maxVertex = std::max(maxVertex, partMax);
    }
    return {consumed, end - start, maxVertex - 1};
}

} // namespace M2OpaqueBatchRun
