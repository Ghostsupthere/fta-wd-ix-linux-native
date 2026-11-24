#ifndef WOW_SOURCE_WORLDCLIENT_WDLBOUNDS_H
#define WOW_SOURCE_WORLDCLIENT_WDLBOUNDS_H

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace WdlBounds {

inline uint32_t Read32(const char* data) {
    uint32_t value;
    std::memcpy(&value, data, sizeof(value));
    return value;
}

inline bool Validate(const char* data, size_t size, size_t objectRecordBytes) {
    constexpr size_t Header = 8;
    constexpr size_t TileTableBytes = 64 * 64 * sizeof(uint32_t);
    constexpr size_t HeightBytes = (17 * 17 + 16 * 16) * sizeof(int16_t);
    constexpr size_t HoleBytes = 16 * sizeof(uint16_t);
    if (!data || size < Header || !objectRecordBytes) return false;

    // Legacy WDL begins with a version, followed by the optional distant WMO tables.
    const auto chunk = [&](size_t offset, uint32_t type, size_t& bytes) {
        if (offset > size || size - offset < Header || Read32(data + offset) != type) return false;
        bytes = Read32(data + offset + 4);
        return bytes <= size - offset - Header;
    };
    size_t bytes;
    if (!chunk(0, 0x4D564552, bytes) || bytes < sizeof(uint32_t)) return false; // MVER
    size_t offset = Header + bytes;
    size_t names;
    if (chunk(offset, 0x4D574D4F, names)) { // MWMO
        const size_t strings = offset + Header;
        offset = strings + names;
        size_t ids;
        if (!chunk(offset, 0x4D574944, ids) || ids % sizeof(uint32_t)) return false; // MWID
        const size_t indices = offset + Header;
        offset = indices + ids;
        if (!chunk(offset, 0x4D4F4446, bytes) || bytes % objectRecordBytes) return false; // MODF
        for (size_t record = offset + Header, end = record + bytes; record < end; record += objectRecordBytes) {
            const uint32_t id = Read32(data + record);
            if (id >= ids / sizeof(uint32_t)) return false;
            const uint32_t name = Read32(data + indices + id * sizeof(uint32_t));
            if (name >= names || !std::memchr(data + strings + name, '\0', names - name)) return false;
        }
        offset += Header + bytes;
    }
    if (!chunk(offset, 0x4D414F46, bytes) || bytes < TileTableBytes) return false; // MAOF

    const auto* offsets = data + offset + Header;
    for (size_t tile = 0; tile < TileTableBytes; tile += sizeof(uint32_t)) {
        const auto height = Read32(offsets + tile);
        if (!height) continue;
        if (!chunk(height, 0x4D415245, bytes) || bytes < HeightBytes) return false; // MARE
        const size_t next = height + Header + bytes;
        if (size - next >= sizeof(uint32_t) && Read32(data + next) == 0x4D41484F &&
            (!chunk(next, 0x4D41484F, bytes) || bytes < HoleBytes)) return false; // MAHO
    }
    return true;
}

} // namespace WdlBounds
#endif
