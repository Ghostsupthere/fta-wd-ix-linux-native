#ifndef ENGINE_SOURCE_MODERNASSETS_MODERNM2SKIN_H
#define ENGINE_SOURCE_MODERNASSETS_MODERNM2SKIN_H

#include <cstdint>

class M2Data;
class M2SkinProfile;

// Decode the file's 48-byte section records before native runtime setup.
// Input section offsets are still file-relative; output arrays are relocated.
bool RelocateNativeM2SkinSections(M2Data* data, M2SkinProfile* skin, uint32_t skinSize);

// Records a model whose bytes passed through the modern-M2 downporter.  Skin
// batches are source encoded only for these models; applying the rebuild to a
// stock v264 M2 would be unsafe. Native skins may still need geometry-only
// palette subdivision; those allocations do not register source shader encoding.
void RegisterModernM2Data(M2Data* data);

// Check loaded skin references before native array construction or draw setup.
bool ValidateNativeM2Skin(const M2Data* data, const M2SkinProfile* skin);

// Rewrites the source shader/batch contract after the client has pointer-fixed
// the model and skin data, but before CM2Shared sizes its render-side arrays.
// Also aligns skin arrays and subdivides over-budget native v264 palettes while
// preserving their already-native batch/material records.
// True means unchanged or rebuilt successfully; false means unsafe to load.
bool RebuildModernM2Skin(M2Data* data, M2SkinProfile* skin, const char* name);

// Releases the near allocations used for either native or converted models. This must run
// before CM2Shared frees the model file buffer.
void ReleaseModernM2Data(M2Data* data);

#endif
