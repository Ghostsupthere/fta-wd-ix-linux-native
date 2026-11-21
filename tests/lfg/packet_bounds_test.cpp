#include "WoW/Source/Ui/LFGPacketBounds.h"

#include <cassert>
#include <cstdint>

int main() {
    using namespace LFGPacketBounds;
    static_assert(RemainingBytes(100, 40) == 60);
    static_assert(RemainingBytes(40, 100) == 0);      // read position past the end (failed read) never underflows
    static_assert(ClampCount(3, 100, 40, 8) == 3);    // a valid count is untouched
    static_assert(ClampCount(7, 100, 40, 8) == 7);    // exactly fits
    static_assert(ClampCount(8, 100, 40, 8) == 7);    // one more than fits is cut to what fits
    static_assert(ClampCount(0xFFFFFFFFu, 100, 40, 8) == 7);   // garbage count from stack: bounded
    static_assert(ClampCount(0xFFFFFFFFu, 40, 40, 8) == 0);    // nothing left
    static_assert(ClampCount(5, 40, 100, 8) == 0);             // read past the end
    static_assert(ClampCount(5, 100, 40, 0) == 0);             // bad entry size is safe
    static_assert(ClampCount(0, 100, 40, 8) == 0);
    // Max loop length for a 64 KB packet is bounded by packet size, not by the count field.
    static_assert(ClampCount(0xFFFFFFFFu, 65536, 0, 8) == 8192);
    return 0;
}
