#include "Engine/Source/Gx/Types.h"
#include "Engine/Source/Model2/Types.h"

#include <limits>

constexpr bool StockBlendModesKeepTheirIndices() {
    for (uint32_t mode = 0; mode <= M2BLEND_MOD2X; ++mode) {
        if (M2BlendModeIndex(mode) != mode) {
            return false;
        }
    }
    return true;
}

static_assert(StockBlendModesKeepTheirIndices());
static_assert(M2BlendModeIndex(7) == M2BLEND_BLEND_ADD);
static_assert(M2BlendModeIndex(8) == M2BLEND_ALPHA);
static_assert(M2BlendModeIndex(std::numeric_limits<uint32_t>::max()) == M2BLEND_ALPHA);
static_assert(GxBlend_ConstantAlpha == 11);
static_assert(GxBlend_BlendAdd == 12);
static_assert(GxBlends_Last == GxBlend_BlendAdd + 1);

int main() {}
