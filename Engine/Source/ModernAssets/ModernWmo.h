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

// C++17-clean views over the snapshots, for the native draw integration.
struct ModernWmoMaterialView {
    uint32_t index = 0, layers = 0, flags = 0, tint = 0;
    const char* textures[8] = {};
};
struct ModernWmoMeshView {
    uint32_t uvSets = 0;
    bool hasWeights = false;
    size_t vertexCount = 0;
    size_t vertexStride = 0;
    const void* vertices = nullptr;
};
bool ModernWmoFindMaterial(const ModernWmoRootVisuals& root, uint32_t index, ModernWmoMaterialView& out);
ModernWmoMeshView ModernWmoMesh(const ModernWmoGroupVisuals& group);

// Aggregate owned CPU side data is capped independently of native WMO buffers.
size_t ModernWmoVisualBytes();

#endif
