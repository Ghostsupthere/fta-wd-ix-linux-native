#ifndef ENGINE_SOURCE_MODERNASSETS_MODERNWMO_H
#define ENGINE_SOURCE_MODERNASSETS_MODERNWMO_H

#include <cstdint>
#include <cstddef>
#include <memory>
#include <vector>

namespace wxl::modern::assets::wmo::visual { struct Root; struct Group; }
using ModernWmoRootVisuals = const wxl::modern::assets::wmo::visual::Root;
using ModernWmoGroupVisuals = const wxl::modern::assets::wmo::visual::Group;

enum class ModernWmoTransformResult {
    NotApplicable,
    Transformed,
    Rejected,
};

ModernWmoTransformResult TransformModernWmoRoot(const void* source, uint32_t sourceSize,
    std::vector<uint8_t>& output, std::shared_ptr<ModernWmoRootVisuals>* visuals = nullptr);
ModernWmoTransformResult TransformModernWmoGroup(const void* source, uint32_t sourceSize,
    std::vector<uint8_t>& output, std::shared_ptr<ModernWmoGroupVisuals>* visuals = nullptr);

// Aggregate owned CPU side data is capped independently of native WMO buffers.
size_t ModernWmoVisualBytes();

#endif
