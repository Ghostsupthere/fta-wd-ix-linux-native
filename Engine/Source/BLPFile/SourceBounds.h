#ifndef ENGINE_SOURCE_BLPFILE_SOURCEBOUNDS_H
#define ENGINE_SOURCE_BLPFILE_SOURCEBOUNDS_H

#include "Engine/Source/BLPFile/Types.h"
#include <algorithm>
#include <cstring>

namespace BlpSourceBounds {

inline constexpr uint8_t PIXEL_DXT1_VALUE = 0; // PIXEL_FORMAT::PIXEL_DXT1 as stored in the header

inline constexpr size_t FixedHeaderBytes = offsetof(BLPHeader, m_extended);

// Bytes the loader really reads for one level: it works from the level's offset and the texture dimensions and
// never from the size field, which several shipped textures get wrong (a paletted skin whose sizes are doubled,
// for example). The span check therefore uses the dimensions too.
inline size_t ExpectedLevelBytes(const BLPHeader& header, uint32_t level) {
    const uint64_t width = std::max<uint32_t>(1, header.m_width >> level);
    const uint64_t height = std::max<uint32_t>(1, header.m_height >> level);
    switch (header.m_colorEncoding) {
        case COLOR_PAL: {
            const uint64_t pixels = width * height;
            return static_cast<size_t>(pixels + (pixels * header.m_alphaSize + 7) / 8);
        }
        case COLOR_DXT:
            return static_cast<size_t>(((width + 3) / 4) * ((height + 3) / 4) * (header.m_preferredFormat == PIXEL_DXT1_VALUE ? 8 : 16));
        case COLOR_RAW:
            return static_cast<size_t>(width * height * 4);
        default: // legacy JPEG encoding is not decoded in this client
            return 1;
    }
}

// Some shipped textures list a final mip whose offset is at or past the end of the file (the smallest
// levels were cut off by the authoring tool), and the stock client never read those entries. Level 0 must
// lie inside the buffer, or the file is rejected. A later slot that does not is cleared together with every
// slot after it and reported through `truncated`, so the caller can shorten the mip chain to the levels
// that exist instead of refusing a texture the stock client loads.
inline bool ReadHeader(const void* data, size_t size, BLPHeader& header, bool* truncated = nullptr) {
    if (truncated) *truncated = false;
    if (!data || size < FixedHeaderBytes) return false;
    BLPHeader parsed{};
    std::memcpy(&parsed, data, FixedHeaderBytes);
    if (parsed.m_magic != BLP_MAGIC_BLP2 || parsed.m_formatVersion != 1 ||
        !parsed.m_width || !parsed.m_height) return false;
    const bool palette = parsed.m_colorEncoding == COLOR_PAL || parsed.m_colorEncoding == COLOR_JPEG;
    const auto headerBytes = palette ? sizeof(parsed) : FixedHeaderBytes;
    if (size < headerBytes) return false;
    if (palette) std::memcpy(&parsed, data, headerBytes);
    bool cut = false;
    for (uint32_t i = 0; i < BLP_MIP_SLOTS; ++i) {
        if (cut) {
            parsed.m_mipSizes[i] = 0;
            parsed.m_mipOffsets[i] = 0;
            continue;
        }
        if (!parsed.m_mipSizes[i]) continue;
        const size_t bytes = ExpectedLevelBytes(parsed, i);
        const size_t offset = parsed.m_mipOffsets[i];
        if (offset < headerBytes || offset > size || bytes > size - offset) {
            if (i == 0) return false;
            cut = true;
            parsed.m_mipSizes[i] = 0;
            parsed.m_mipOffsets[i] = 0;
        }
    }
    if (truncated) *truncated = cut;
    header = parsed;
    return true;
}

} // namespace BlpSourceBounds
#endif
