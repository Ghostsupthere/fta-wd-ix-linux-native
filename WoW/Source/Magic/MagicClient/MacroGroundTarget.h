#ifndef WOW_SOURCE_MAGIC_MAGICCLIENT_MACROGROUNDTARGET_H
#define WOW_SOURCE_MAGIC_MAGICCLIENT_MACROGROUNDTARGET_H

#include <cstdint>
#include <string_view>

enum class MacroGroundPlacement : uint8_t { None, Cursor, Player };

namespace MacroGroundTarget {

constexpr uint32_t Source = 0x20;
constexpr uint32_t Destination = 0x40;

inline bool TokenMatches(std::string_view token, std::string_view expected) {
    if (token.size() != expected.size()) {
        return false;
    }

    for (std::size_t i = 0; i < token.size(); ++i) {
        const char c = token[i];
        const char lower = c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
        if (lower != expected[i]) {
            return false;
        }
    }
    return true;
}

inline MacroGroundPlacement ParsePlacement(const char* token, bool allowed) {
    if (!token || !allowed) {
        return MacroGroundPlacement::None;
    }
    if (TokenMatches(token, "cursor")) {
        return MacroGroundPlacement::Cursor;
    }
    if (TokenMatches(token, "player")) {
        return MacroGroundPlacement::Player;
    }
    return MacroGroundPlacement::None;
}

constexpr bool IsGroundOnly(uint32_t targets) {
    constexpr uint32_t ground = Source | Destination;
    return (targets & ground) && !(targets & ~ground);
}

constexpr bool IsInRange(float distanceSquared, float minRange, float maxRange) {
    return distanceSquared >= minRange * minRange && distanceSquared <= maxRange * maxRange;
}

}

#endif
