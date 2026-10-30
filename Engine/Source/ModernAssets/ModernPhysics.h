#ifndef ENGINE_SOURCE_MODERNASSETS_MODERNPHYSICS_H
#define ENGINE_SOURCE_MODERNASSETS_MODERNPHYSICS_H

#include "Engine/Source/ModernAssets/WxlPhysics/PhysicsData.hpp"

enum class ModernPhysicsLoadResult { Missing, Loaded, PendingFileDataId, Rejected };
using ModernPhysicsReader = bool (*)(const char*, std::vector<uint8_t>&);
using ModernPhysicsResolver = bool (*)(uint32_t, std::string&);

// Readers must return original mounted bytes, before M2 downporting strips
// MD21 metadata. A pending FileDataID must be retried, never cached as missing.
ModernPhysicsLoadResult LoadModernPhysicsAsset(const char* modelPath, ModernPhysicsReader reader,
    ModernPhysicsResolver resolver, wow::ec::physics::Model& output,
    std::string& error, uint32_t& pendingFileId);

#endif
