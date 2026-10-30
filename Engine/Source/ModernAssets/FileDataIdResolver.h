#ifndef ENGINE_SOURCE_MODERNASSETS_FILEDATAIDRESOLVER_H
#define ENGINE_SOURCE_MODERNASSETS_FILEDATAIDRESOLVER_H

#include <cstdint>
#include <string>

// Resolves an MD21 texture FileDataID through the client-local WarcraftXL sidecar.
bool ResolveModernAssetFileDataId(uint32_t id, std::string& path);

#endif
