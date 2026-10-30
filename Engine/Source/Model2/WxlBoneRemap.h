#ifndef ENGINE_SOURCE_MODEL2_WXLBONEREMAP_H
#define ENGINE_SOURCE_MODEL2_WXLBONEREMAP_H

#include <cstdint>
#include <vector>

struct WxlBoneDescriptor {
    int32_t key;
    uint32_t crc;
    uint16_t parent;
};

// Skeleton identifiers are immutable after native model loading. Preserve the
// original matching order: name CRC, key ID, key lookup, then mapped ancestor.
inline std::vector<uint32_t> WxlBuildBoneRemap(const std::vector<WxlBoneDescriptor>& child,
                                             const std::vector<WxlBoneDescriptor>& parent,
                                             const uint16_t* parentLookup = nullptr,
                                             uint32_t parentLookupCount = 0) {
    if (child.empty() || child.size() > 256 || parent.empty() || parent.size() > 65535) return {};
    std::vector<uint32_t> result(child.size(), UINT32_MAX);
    for (uint32_t i = 0; i < child.size(); ++i) {
        if (child[i].crc) {
            for (uint32_t j = 0; j < parent.size(); ++j)
                if (parent[j].crc == child[i].crc) { result[i] = j; break; }
        }
        if (result[i] == UINT32_MAX && child[i].key >= 0) {
            for (uint32_t j = 0; j < parent.size(); ++j)
                if (parent[j].key == child[i].key) { result[i] = j; break; }
            const uint32_t key = static_cast<uint32_t>(child[i].key);
            if (result[i] == UINT32_MAX && parentLookup && key < parentLookupCount && parentLookup[key] < parent.size())
                result[i] = parentLookup[key];
        }
    }
    for (uint32_t pass = 0; pass < child.size(); ++pass) {
        bool changed = false;
        for (uint32_t i = 0; i < child.size(); ++i) {
            const uint16_t ancestor = child[i].parent;
            if (result[i] == UINT32_MAX && ancestor < child.size() && result[ancestor] != UINT32_MAX) {
                result[i] = result[ancestor];
                changed = true;
            }
        }
        if (!changed) break;
    }
    return result;
}
#endif
