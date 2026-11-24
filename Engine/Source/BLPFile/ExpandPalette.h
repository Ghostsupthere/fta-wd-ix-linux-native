#ifndef ENGINE_SOURCE_BLPFILE_EXPANDPALETTE_H
#define ENGINE_SOURCE_BLPFILE_EXPANDPALETTE_H

#include "Engine/Source/BLPFile/Types.h"

namespace BlpDecode {

inline void ExpandAlpha8(BlpPalPixel* output, const uint8_t* indices, const BlpPalPixel* palette, uint32_t count) {
    const auto* alpha = indices + count;
    for (uint32_t i = 0; i < count; ++i) {
        const auto& color = palette[indices[i]];
        output[i] = {color.m_b, color.m_g, color.m_r, alpha[i]};
    }
}

} // namespace BlpDecode

#endif
