#include "WoW/Source/Magic/MagicClient/MacroGroundTarget.h"

#include <cassert>
#include <limits>

int main() {
    using MacroGroundPlacement::Cursor;
    using MacroGroundPlacement::None;
    using MacroGroundPlacement::Player;
    using namespace MacroGroundTarget;

    assert(ParsePlacement("cursor", true) == Cursor);
    assert(ParsePlacement("CuRsOr", true) == Cursor);
    assert(ParsePlacement("player", true) == Player);
    assert(ParsePlacement("PLAYER", true) == Player);
    assert(ParsePlacement("cursor", false) == None);
    assert(ParsePlacement("player", false) == None);
    assert(ParsePlacement(nullptr, true) == None);
    assert(ParsePlacement("", true) == None);
    assert(ParsePlacement("focus", true) == None);
    assert(ParsePlacement("playertarget", true) == None);
    assert(ParsePlacement("cursorx", true) == None);

    assert(IsGroundOnly(Source));
    assert(IsGroundOnly(Destination));
    assert(IsGroundOnly(Source | Destination));
    assert(!IsGroundOnly(0));
    assert(!IsGroundOnly(0x2));
    assert(!IsGroundOnly(Destination | 0x80));
    assert(!IsGroundOnly(Source | Destination | 0x10000));

    assert(IsInRange(0.0f, 0.0f, 5.0f));
    assert(IsInRange(4.0f, 2.0f, 5.0f));
    assert(IsInRange(25.0f, 2.0f, 5.0f));
    assert(!IsInRange(3.99f, 2.0f, 5.0f));
    assert(!IsInRange(25.01f, 2.0f, 5.0f));
    assert(!IsInRange(std::numeric_limits<float>::quiet_NaN(), 2.0f, 5.0f));
}
