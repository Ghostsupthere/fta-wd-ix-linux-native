#include "WoW/Source/WorldClient/Linux/WxlWmoVisibility.h"
#include <cassert>

int main() {
    assert(!WxlWmoReopensExterior(0, 0));
    assert(WxlWmoReopensExterior(0x40, 0));
    assert(!WxlWmoReopensExterior(0x2000, 0));
    assert(WxlWmoReopensExterior(0x2000, 1));
    assert(WxlWmoReopensExterior(0x20002000, 0));
    assert(!WxlWmoReopensExterior(0x20000000, 1));
}
