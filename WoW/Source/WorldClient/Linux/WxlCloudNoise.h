#pragma once
#include <cstdint>

// The stock cloud generator scales rand() by 1/32767 (the Windows C runtime's
// RAND_MAX) to build a noise table in [-1, 1]. glibc's rand() reaches 2^31-1,
// which made the table about +/-131000 and turned dense clouds into per-texel
// speckle, so keep the Windows range.
inline float WxlCloudNoiseFromRand(int32_t value) {
    const int32_t windowsRange = value & 0x7FFF;
    return 1.0f - (windowsRange * 0.000030518509f + windowsRange * 0.000030518509f);
}
