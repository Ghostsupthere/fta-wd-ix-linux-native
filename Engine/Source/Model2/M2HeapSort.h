#pragma once

#include <cstdint>

// Keep the legacy heap traversal so comparator ties produce the same draw order.
template <class Compare>
void M2HeapSortWith(uint32_t* array, uint32_t count, Compare compare) {
    if (count <= 1) {
        return;
    }

    for (uint32_t i = count >> 1; i;) {
        i--;

        uint32_t value = array[i];
        uint32_t parent = i;

        for (uint32_t child = 2 * i + 1; child < count; child = 2 * child + 1) {
            uint32_t childValue = array[child];

            if (child + 1 < count && compare(array[child + 1], array[child]) > 0) {
                child++;
                childValue = array[child];
            }

            if (compare(childValue, value) <= 0) {
                break;
            }

            array[parent] = array[child];
            parent = child;
        }

        array[parent] = value;
    }

    for (uint32_t i = count - 1; i; i--) {
        uint32_t value = array[i];
        array[i] = array[0];

        uint32_t parent = 0;

        for (uint32_t child = 1; child < i; child = 2 * child + 1) {
            uint32_t childValue = array[child];

            if (child + 1 < i && compare(array[child + 1], array[child]) > 0) {
                child++;
                childValue = array[child];
            }

            if (compare(childValue, value) <= 0) {
                break;
            }

            array[parent] = array[child];
            parent = child;
        }

        array[parent] = value;
    }
}
