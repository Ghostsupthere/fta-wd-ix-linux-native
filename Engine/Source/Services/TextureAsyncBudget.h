#ifndef ENGINE_SOURCE_SERVICES_TEXTUREASYNCBUDGET_H
#define ENGINE_SOURCE_SERVICES_TEXTUREASYNCBUDGET_H

#include <cstdint>

namespace TextureAsyncBudget {

inline constexpr uint32_t Limit = 4 * 1024 * 1024;

inline bool CanAdmit(uint64_t used, uint32_t bytes) {
    // An oversized file can make progress on an empty queue.
    return used == 0 || (used <= Limit && bytes <= Limit - used);
}

inline void Release(uint64_t& used, uint32_t bytes) {
    used = bytes < used ? used - bytes : 0;
}

} // namespace TextureAsyncBudget

#endif
