#pragma once

#include "Engine/Source/Model2/M2HeapSort.h"

#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

namespace M2TransparentSort {

struct Key {
    static constexpr auto Invalid = std::numeric_limits<uint64_t>::max();
    uint64_t first;
    uint64_t second;
};

inline uint32_t DescendingDepth(uint32_t bits) {
    if (!(bits & 0x7FFFFFFFu)) {
        bits = 0;
    }
    return bits & 0x80000000u ? bits : ~(bits ^ 0x80000000u);
}

inline Key MakeKey(float sortDepth, uint32_t flags, int32_t priorityPlane, float depth) {
    static_assert(sizeof(float) == sizeof(uint32_t) && std::numeric_limits<float>::is_iec559);
    uint32_t primaryDepth;
    uint32_t secondaryDepth;
    std::memcpy(&primaryDepth, &sortDepth, sizeof(primaryDepth));
    std::memcpy(&secondaryDepth, &depth, sizeof(secondaryDepth));
    if ((primaryDepth & 0x7FFFFFFFu) > 0x7F800000u || (secondaryDepth & 0x7FFFFFFFu) > 0x7F800000u) {
        return {Key::Invalid, 0};
    }

    // Preserve depth, split, signed priority and final depth in comparator order.
    const uint32_t priority = static_cast<uint32_t>(priorityPlane) ^ 0x80000000u;
    const uint32_t split = (flags & 1u) ^ 1u;
    return {
        (uint64_t{DescendingDepth(primaryDepth)} << 32) | (uint64_t{split} << 31) | (priority >> 1),
        (uint64_t{priority & 1u} << 32) | DescendingDepth(secondaryDepth),
    };
}

template <class Element, class Compare>
bool TrySort(uint32_t* order, uint32_t count, const Element* elements, uint32_t elementCount,
             std::vector<Key>& keys, Compare fallback) {
    constexpr uint32_t MinimumPass = 64;
    constexpr uint32_t MaxElements = 1u << 18;
    if (count < MinimumPass || elementCount > MaxElements) {
        return false;
    }

    try {
        keys.resize(elementCount);
    } catch (const std::bad_alloc&) {
        return false;
    }

    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t index = order[i];
        const auto& element = elements[index];
        keys[index] = MakeKey(element.m_sortDepth, element.m_flags, element.m_priorityPlane, element.m_depth);
    }

    M2HeapSortWith(order, count, [&](uint32_t a, uint32_t b) -> int32_t {
        const Key& left = keys[a];
        const Key& right = keys[b];
        if (left.first != Key::Invalid && right.first != Key::Invalid) {
            if (left.first != right.first) {
                return left.first < right.first ? -1 : 1;
            }
            if (left.second != right.second) {
                return left.second < right.second ? -1 : 1;
            }
        }
        return fallback(a, b);
    });
    return true;
}

} // namespace M2TransparentSort
