#pragma once

#include "EquipmentFitRules.h"

#include <algorithm>

namespace EquipmentFit {

#include "EquipmentFitCatalog.inc"

template <std::size_t Count>
bool Contains(const uint32_t (&ids)[Count], uint32_t id) {
    return std::binary_search(ids, ids + Count, id);
}

inline bool IsModern(Slot slot, uint32_t item, uint32_t display) {
    const bool shoulders = slot == Slot::Shoulders;
    if (shoulders ? Contains(RequestedModernShoulderDisplays, display) : Contains(RequestedModernHeadDisplays, display)) {
        return true;
    }

    const bool legacy = shoulders ? Contains(LegacyShoulderDisplays, display) : Contains(LegacyHeadDisplays, display);
    const bool modernDisplay = display >= 66000 ||
        (shoulders ? Contains(ModernLowShoulderDisplays, display) : Contains(ModernLowHeadDisplays, display));
    if (modernDisplay && !legacy) {
        return true;
    }

    if (item) {
        return (shoulders ? Contains(RequestedModernShoulderItems, item) : Contains(RequestedModernHeadItems, item)) ||
            (item >= 56840 && !Contains(LegacyCustomItems, item));
    }
    return false;
}

inline float DefaultScale(const Context& context) {
    if (!TokenEqual(context.raceName, "Tauren") && !TokenEqual(context.racePrefix, "Ta")) {
        return 1.0f;
    }
    if (context.slot == Slot::Shoulders) {
        return context.modern ? 0.85f : 0.94f;
    }
    return context.modern ? (context.sex == 1 ? 0.85f : 0.78f) : 1.0f;
}

} // namespace EquipmentFit
