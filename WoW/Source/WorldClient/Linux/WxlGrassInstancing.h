#pragma once
// Hardware-instanced ground-effect (grass) drawing. At long grass distances the engine has more
// visible detail batches than it has cached buffers and re-bakes the rest on the CPU every frame;
// this draws each batch from static model geometry plus one 64-byte record per plant instead.
// The engine's own vertex shader is derived offline (Utils/WarcraftXL/visuals/
// grass_instanced.pack, tools/build-wmo-shader-pack.py); without the pack, with
// WXL_GRASS_INSTANCING=0, or whenever anything is missing the stock bake and draw run.
#include <cstdint>

class CDetailDoodadInstBatch;
class CGxShader;

#if defined(WOWDECOMP_LINUX)
namespace wxl::grass::instancing { struct GpuInstance; struct GpuVertex; }
#include <vector>

// Once, after the grass-motion module loaded its shaders.
void WxlGrassInstancingInitialize();
// The vertex shader the next detail batches draw with (from the per-chunk constant setup).
void WxlGrassInstancingNoteShader(CGxShader* vertex);
// Opaque plan for a batch (its instance buffer is built or reused), or null to draw the stock way.
const void* WxlGrassInstancingPlan(const CDetailDoodadInstBatch* batch);
// Brackets the engine's draw call that the instanced draw replaces.
void WxlGrassInstancingArm(const void* plan);
void WxlGrassInstancingDisarm();
// The batch's plants changed or it was freed.
void WxlGrassInstancingForget(const CDetailDoodadInstBatch* batch);
// Once per rendered frame: expires unused buffers.
void WxlGrassInstancingFrame();
// Device loss, world leave and shutdown.
void WxlGrassInstancingRelease();

// Implemented beside the engine's own bake (DetailDoodad.cpp) so both share its math.
bool WxlGrassBakeInstances(const CDetailDoodadInstBatch& batch, std::vector<wxl::grass::instancing::GpuInstance>& plants, std::vector<uint32_t>& doodads);
bool WxlGrassModelGeometry(uint32_t doodadId, std::vector<wxl::grass::instancing::GpuVertex>& vertices, std::vector<uint16_t>& indices);
#endif
