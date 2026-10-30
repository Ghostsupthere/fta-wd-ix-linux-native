#ifndef WOW_SOURCE_WORLDCLIENT_LINUX_WXLFOGRANGE_H
#define WOW_SOURCE_WORLDCLIENT_LINUX_WXLFOGRANGE_H

#include <cmath>

// WarcraftXLVisuals' native_fog_scale.h contract: preserve the start and
// change the slope. Reject invalid input without touching the output.
inline bool WxlScaleFogRange(float start, float end, float scale, float& result) {
    if (!std::isfinite(start) || !std::isfinite(end) || !std::isfinite(scale) ||
        start < 0.0f || end <= start || end > 1000000.0f || scale < 0.0f || scale > 2.0f) return false;
    const float scaled = scale > 0.0f ? start + (end - start) / scale : 1000000.0f;
    if (!std::isfinite(scaled) || scaled <= start) return false;
    result = scaled;
    return true;
}

#endif
