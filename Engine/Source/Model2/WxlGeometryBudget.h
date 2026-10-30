#ifndef ENGINE_SOURCE_MODEL2_WXLGEOMETRYBUDGET_H
#define ENGINE_SOURCE_MODEL2_WXLGEOMETRYBUDGET_H

#include "Engine/Source/Model2/M2Data.h"

// Keep the legacy optimizer's compact index-start cache within uint16_t.
// Wide skins can draw through the unoptimized path with full runtime offsets.
// Material expansion can repeat windows beyond the source skin's index count.
inline bool WxlAccumulateOptimizedIndices(uint32_t& total, uint32_t count) {
    constexpr uint32_t limit = UINT16_MAX;
    if (total > limit || count > limit - total) return false;
    total += count;
    return true;
}

// Geometry ranges can merge only when their complete draw contracts agree.
// Section/geoset IDs differ naturally; visibility has already selected them.
inline bool WxlBatchesMatch(const M2Batch& a, const M2Batch& b,
                            const M2SkinSection& x, const M2SkinSection& y) {
    return a.m_flags == b.m_flags && a.m_priorityPlane == b.m_priorityPlane &&
        a.m_shader == b.m_shader && a.m_colorIndex == b.m_colorIndex &&
        a.m_materialIndex == b.m_materialIndex && a.m_materialLayer == b.m_materialLayer &&
        a.m_textureCount == b.m_textureCount && a.m_textureComboIndex == b.m_textureComboIndex &&
        a.m_textureCoordComboIndex == b.m_textureCoordComboIndex &&
        a.m_textureWeightComboIndex == b.m_textureWeightComboIndex &&
        a.m_textureTransformComboIndex == b.m_textureTransformComboIndex &&
        x.m_boneComboIndex == y.m_boneComboIndex && x.m_boneCount == y.m_boneCount &&
        x.m_boneInfluences == y.m_boneInfluences;
}

#endif
