#ifndef ENGINE_SOURCE_MODERNASSETS_MODERNBLP_H
#define ENGINE_SOURCE_MODERNASSETS_MODERNBLP_H

#include <cstdint>
#include <vector>

enum class ModernBlpTransformResult {
    NotApplicable,
    Transformed,
    Rejected,
};

// Converts source-era BLP2 encodings to formats understood by this client.
ModernBlpTransformResult TransformModernBlp(const char* name, const void* source, uint32_t sourceSize,
                                            std::vector<uint8_t>& output);

#endif
