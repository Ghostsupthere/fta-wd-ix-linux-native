#pragma once

#include <cstdint>

#if defined(WOWDECOMP_LINUX) && defined(__SSE2__)
#include <xmmintrin.h>
#endif

namespace GxShaderConstantUpdate {

inline void Scalar(float* destination, const float* source, uint32_t index, uint32_t count,
                   uint32_t& dirtyMin, uint32_t& dirtyMax) {
    for (uint32_t i = index; i < index + count; ++i) {
        bool changed = false;
        for (uint32_t lane = 0; lane < 4; ++lane) {
            if (source[lane] != destination[lane]) {
                destination[lane] = source[lane];
                changed = true;
            }
        }
        if (changed) {
            if (dirtyMin > i) dirtyMin = i;
            if (dirtyMax < i) dirtyMax = i;
        }
        destination += 4;
        source += 4;
    }
}

#if defined(WOWDECOMP_LINUX) && defined(__SSE2__)
inline void Simd(float* destination, const float* source, uint32_t index, uint32_t count,
                 uint32_t& dirtyMin, uint32_t& dirtyMax) {
    const auto src = reinterpret_cast<uintptr_t>(source);
    const auto dst = reinterpret_cast<uintptr_t>(destination);
    const auto distance = src < dst ? dst - src : src - dst;
    if (distance && distance < static_cast<uintptr_t>(count) * 4 * sizeof(float)) {
        // Partial overlap needs the original component-by-component write order.
        Scalar(destination, source, index, count, dirtyMin, dirtyMax);
        return;
    }

    uint32_t firstChanged = UINT32_MAX;
    uint32_t lastChanged = 0;
    for (uint32_t i = index; i < index + count; ++i) {
        const __m128 before = _mm_loadu_ps(destination);
        const __m128 after = _mm_loadu_ps(source);
        const __m128 different = _mm_cmpneq_ps(before, after);
        if (_mm_movemask_ps(different)) {
            // Keep the destination bits when the floats compare equal, including signed zero.
            _mm_storeu_ps(destination, _mm_or_ps(_mm_and_ps(different, after), _mm_andnot_ps(different, before)));
            if (firstChanged > i) firstChanged = i;
            lastChanged = i;
        }
        destination += 4;
        source += 4;
    }

    if (firstChanged != UINT32_MAX) {
        if (dirtyMin > firstChanged) dirtyMin = firstChanged;
        if (dirtyMax < lastChanged) dirtyMax = lastChanged;
    }
}
#endif

} // namespace GxShaderConstantUpdate
