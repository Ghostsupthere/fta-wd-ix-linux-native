#ifndef WOW_SOURCE_WORLDCLIENT_STATICCULLREUSE_H
#define WOW_SOURCE_WORLDCLIENT_STATICCULLREUSE_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace StaticCullReuse {

// This is a projected query, independent of which occluders have been submitted.
// A prior visible column can only be reused if it still passes against current depth.
struct BoxQuery {
    float height = 0.0f;
    int32_t first = 0;
    int32_t last = -1;
    int32_t visibleColumn = -1;
    bool testsColumns = false;

    bool Hidden(const float* depth) {
        if (!testsColumns) return false;
        if (visibleColumn >= 0 && height > depth[visibleColumn]) return false;
        for (int32_t column = first; column <= last; ++column) {
            if (height > depth[column]) {
                visibleColumn = column;
                return false;
            }
        }
        visibleColumn = -1;
        return true;
    }
};

using Bounds = std::array<float, 6>;

template <typename ViewKey, size_t Capacity>
class Cache {
    static_assert(Capacity && (Capacity & (Capacity - 1)) == 0);

public:
    struct Entry {
        uintptr_t source = 0;
        uint32_t generation = 0;
        Bounds bounds{};
        bool inFrustum = false;
        bool projected = false;
        BoxQuery query;
    };

    void Begin(uint32_t frame, const ViewKey& view) {
        const bool consecutive = m_generation && m_frame + 1 == frame;
        if (!consecutive || std::memcmp(&view, &m_view, sizeof(view)) != 0) {
            if (++m_generation == 0) {
                m_entries = {};
                ++m_generation;
            }
        }
        m_frame = frame;
        std::memcpy(&m_view, &view, sizeof(view));
    }

    Entry& Lookup(uintptr_t source, const Bounds& bounds, bool& hit) {
        const size_t mixed = (source >> 4) ^ (source >> 13);
        auto& entry = m_entries[mixed & (Capacity - 1)];
        hit = entry.generation == m_generation && entry.source == source &&
            std::memcmp(entry.bounds.data(), bounds.data(), sizeof(bounds)) == 0;
        if (!hit) entry = {source, m_generation, bounds, false, false, {}};
        return entry;
    }

private:
    std::array<Entry, Capacity> m_entries{};
    ViewKey m_view{};
    uint32_t m_generation = 0;
    uint32_t m_frame = 0;
};

} // namespace StaticCullReuse

#endif
