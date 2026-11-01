#ifndef ENGINE_SOURCE_MODERNASSETS_MODERNM2_H
#define ENGINE_SOURCE_MODERNASSETS_MODERNM2_H

#include <cstdint>
#include <string>
#include <vector>

using ModernM2FileReader = bool (*)(const char* path, std::vector<uint8_t>& output);

enum class ModernM2TransformResult {
    NotApplicable,
    Transformed,
    RepairedNative,
    Rejected,
};

// Converts modern models and applies scoped compatibility repairs to native v264 models.
ModernM2TransformResult TransformModernM2(const char* name, const void* source, uint32_t sourceSize,
                                          std::vector<uint8_t>& output,
                                          ModernM2FileReader sidecarReader = nullptr);

enum class ModernAnimationTransformResult { NotApplicable, Unwrapped, Rejected };

// Removes a single AFM2 wrapper in place. Split AFSA/AFSB payloads and malformed
// wrappers must be rejected before the native flat-animation parser sees them.
ModernAnimationTransformResult TransformModernExternalAnimation(void* buffer, uint32_t& size);

// One-line description of a chunked animation file ("AFM2:1024 AFSB:4096 ..."), for diagnostics
// when split streams are rejected. Never reads past `size`; stops at the first inconsistent chunk.
std::string DescribeModernAnimationChunks(const void* buffer, uint32_t size);

#endif
