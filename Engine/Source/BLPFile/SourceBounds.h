#ifndef ENGINE_SOURCE_BLPFILE_SOURCEBOUNDS_H
#define ENGINE_SOURCE_BLPFILE_SOURCEBOUNDS_H

#include "Engine/Source/BLPFile/Types.h"
#include <cstring>

namespace BlpSourceBounds {

inline constexpr size_t FixedHeaderBytes = offsetof(BLPHeader, m_extended);

inline bool ReadHeader(const void* data, size_t size, BLPHeader& header) {
    if (!data || size < FixedHeaderBytes) return false;
    BLPHeader parsed{};
    std::memcpy(&parsed, data, FixedHeaderBytes);
    if (parsed.m_magic != BLP_MAGIC_BLP2 || parsed.m_formatVersion != 1 ||
        !parsed.m_width || !parsed.m_height) return false;
    const bool palette = parsed.m_colorEncoding == COLOR_PAL || parsed.m_colorEncoding == COLOR_JPEG;
    const auto headerBytes = palette ? sizeof(parsed) : FixedHeaderBytes;
    if (size < headerBytes) return false;
    if (palette) std::memcpy(&parsed, data, headerBytes);
    for (uint32_t i = 0; i < BLP_MIP_SLOTS; ++i) {
        const size_t bytes = parsed.m_mipSizes[i];
        if (!bytes) continue;
        const size_t offset = parsed.m_mipOffsets[i];
        if (offset < headerBytes || offset > size || bytes > size - offset) return false;
    }
    header = parsed;
    return true;
}

} // namespace BlpSourceBounds
#endif
