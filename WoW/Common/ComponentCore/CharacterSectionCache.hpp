#ifndef WOW_COMMON_COMPONENTCORE_CHARACTERSECTIONCACHE_HPP
#define WOW_COMMON_COMPONENTCORE_CHARACTERSECTIONCACHE_HPP

#include "WoW/Common/ComponentCore/CharacterCompositor.hpp"

#include <array>
#include <cstring>
#include <new>
#include <utility>
#include <vector>

namespace wxl::compositor {

// Opaque scaled sections are independent of destination pixels. Keep source content
// alongside the result so reloaded textures and recycled cache handles are safe.
inline bool ReplayOpaqueScale(const uint8_t* pixels, const BlpPalPixel* palette,
    unsigned width, unsigned height, unsigned x, unsigned y, unsigned columns, unsigned rows,
    C4Pixel* output, unsigned stride, unsigned shift)
{
    constexpr unsigned MaxSize = 1024;
    constexpr unsigned MaxOutputPixels = 256 * 1024;
    if (!pixels || !palette || !output || !width || !height || width > MaxSize || height > MaxSize ||
        x >= width || y >= height || !columns || !rows || columns > width - x || rows > height - y ||
        shift < 2 || shift > 4 || (columns << shift) > stride ||
        (columns * rows << (2 * shift)) > MaxOutputPixels) return false;

    struct Entry {
        std::array<BlpPalPixel, BLP_PALETTE_ENTRIES> palette;
        unsigned columns = 0, rows = 0, shift = 0;
        std::vector<uint8_t> source;
        std::vector<C4Pixel> scaled;
    };
    static thread_local std::array<Entry, 4> cache;
    static thread_local unsigned replacement = 0;
    const unsigned outColumns = columns << shift;
    const unsigned outRows = rows << shift;
    const auto* source = pixels + y * width + x;
    const auto replay = [&](const Entry& entry) {
        for (unsigned row = 0; row < outRows; ++row)
            std::memcpy(output + row * stride, entry.scaled.data() + row * outColumns, outColumns * sizeof(C4Pixel));
    };

    for (const auto& entry : cache) {
        if (entry.columns != columns || entry.rows != rows || entry.shift != shift ||
            std::memcmp(entry.palette.data(), palette, sizeof(entry.palette)) != 0) continue;
        unsigned row = 0;
        while (row < rows && std::memcmp(entry.source.data() + row * columns, source + row * width, columns) == 0)
            ++row;
        if (row == rows) {
            replay(entry);
            return true;
        }
    }

    try {
        Entry entry{};
        entry.source.resize(columns * rows);
        entry.scaled.resize(outColumns * outRows);
        std::memcpy(entry.palette.data(), palette, sizeof(entry.palette));
        entry.columns = columns;
        entry.rows = rows;
        entry.shift = shift;
        for (unsigned row = 0; row < rows; ++row)
            std::memcpy(entry.source.data() + row * columns, source + row * width, columns);
        ScaleSection(entry.source.data(), palette, columns, rows, 0, 0, 0, columns, rows,
            entry.scaled.data(), outColumns, shift);
        replay(entry);
        cache[replacement] = std::move(entry);
        replacement = (replacement + 1) % cache.size();
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    }
}

} // namespace wxl::compositor
#endif
