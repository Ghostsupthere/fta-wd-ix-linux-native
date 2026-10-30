#ifndef WOW_SOURCE_WORLDCLIENT_LINUX_WXLWMOVISIBILITY_H
#define WOW_SOURCE_WORLDCLIENT_LINUX_WXLWMOVISIBILITY_H

#include <cstdint>

// WarcraftXL's modern-WMO visibility rule for the viewer's group.
constexpr bool WxlWmoReopensExterior(uint32_t flags, uint16_t transitionBatches) {
    return (flags & 0x40u) || ((flags & 0x2000u) && ((flags & 0x20000000u) || transitionBatches));
}

#endif
