#pragma once
// Native GPU integration for modern WMO layered materials (two-layer overlays and
// four-layer height blends). The converted-root snapshots (ModernWmo.h) supply the
// per-vertex UV/weight data; the shaders are the engine's own, patched offline into a
// locally generated pack (tools/build-wmo-shader-pack.py). Without the pack, with the
// profile switch off, or whenever any prerequisite is missing, the stock draw runs.
#include "Engine/Source/ModernAssets/ModernWmo.h"
#include <cstdint>
#include <memory>

class CMapObj;
class CMapObjGroup;
struct SMOBatch;

#if defined(WOWDECOMP_LINUX)
struct WxlWmoContext {
    CMapObj* root = nullptr;
    CMapObjGroup* group = nullptr;
    std::shared_ptr<ModernWmoRootVisuals> rootData;
    std::shared_ptr<ModernWmoGroupVisuals> groupData;
    uint32_t material = UINT32_MAX;
};

// Whether the loaded layered-material pack holds a layered variant for this exact vertex bytecode
// (the surface-light module asks about its light-patched WMO shaders).
bool WxlWmoPackHasVertexVariant(const uint32_t* code, size_t bytes);

// Marks one WMO group's render leaf; nested leaves restore the outer context.
class WxlWmoLeafScope {
public:
    WxlWmoLeafScope(CMapObj* root, CMapObjGroup* group);
    ~WxlWmoLeafScope();
    WxlWmoLeafScope(const WxlWmoLeafScope&) = delete;
    WxlWmoLeafScope& operator=(const WxlWmoLeafScope&) = delete;
private:
    WxlWmoContext m_previous;
};

// Latches the material of a batch that survived culling (or clears the latch).
void WxlWmoNoteBatch(const SMOBatch* batch);
// Once per rendered frame: expires unused GPU caches.
void WxlWmoFrame();
// World leave, device loss and shutdown.
void WxlWmoRelease();
#endif
