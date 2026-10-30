#ifndef WOW_COMMON_COMPONENTCORE_WXLEQUIPMENTOWNER_H
#define WOW_COMMON_COMPONENTCORE_WXLEQUIPMENTOWNER_H

#include <cstdint>
#include <atomic>

inline std::atomic<uint64_t> s_wxlEquipmentGeneration{1};
inline uint64_t WxlEquipmentGeneration() { return s_wxlEquipmentGeneration.load(std::memory_order_relaxed); }
inline void WxlInvalidateWorldEquipment() { s_wxlEquipmentGeneration.fetch_add(1, std::memory_order_relaxed); }

struct WxlEquipmentOwner {
    const void* model = nullptr;
    const void* shared = nullptr;
    const void* data = nullptr;
    const void* scene = nullptr;
    uint32_t race = 0;
    uint32_t sex = 0;
    bool collectionEnabled = false;
    uint64_t generation = 0;

    bool operator==(const WxlEquipmentOwner& other) const {
        return model == other.model && shared == other.shared && data == other.data && scene == other.scene &&
            race == other.race && sex == other.sex && collectionEnabled == other.collectionEnabled && generation == other.generation;
    }
};

class WxlEquipmentOwnerWatch {
    WxlEquipmentOwner previous;
    bool valid = false;
public:
    bool AppearanceChanged(const WxlEquipmentOwner& current) const {
        return valid && (previous.race != current.race || previous.sex != current.sex);
    }
    bool Observe(const WxlEquipmentOwner& current) {
        if (!current.model || !current.shared || !current.data || !current.scene) {
            valid = false;
            return false;
        }
        if (valid && previous == current) return false;
        previous = current;
        valid = true;
        return true;
    }
    void Reset() { valid = false; }
};
#endif
