#pragma once
#include <cstddef>
#include <cstdint>
// Render-thread-only bridges. The graphics backend invokes these before releasing
// default-pool resources, including resize, device loss and shutdown.
class CGxShader;
class CM2Model;
namespace NativeVisualHooks {
inline bool waterDepthWriteOverride=false;
inline void (*deviceLost)()=nullptr;
// Called when the engine selects shaders; the WMO material module latches the pair.
inline void (*wmoBind)(CGxShader* vertex,CGxShader* pixel)=nullptr;
// Called in place of an indexed draw (device is an IDirect3DDevice9*); returns true
// when the callee issued the draw itself.
inline bool (*wmoDraw)(void* device,int type,int base,unsigned minimum,unsigned count,unsigned start,unsigned primitives)=nullptr;
// Called as the engine loads a shader: may substitute offline-patched bytecode (grass motion).
// Set only around a ground-effect batch that is drawn with hardware instancing: called in place of
// the indexed draw (same arguments as wmoDraw); returns true when it drew.
inline bool (*instancedDraw)(void* device,int type,int base,unsigned minimum,unsigned count,unsigned start,unsigned primitives)=nullptr;
// Set only while transparent fog is armed: called for every M2 batch fog setup and may
// rewrite the fog range, exponent and colour in place (colour is the packed ARGB value).
inline void (*m2BatchFog)(float* start,float* end,float* exponent,uint32_t* colour)=nullptr;
inline bool (*replaceShaderCode)(const uint8_t* code,size_t size,const uint32_t** out,size_t* outBytes)=nullptr;
// Game-object editor overlay. overlayDraw runs after the world and UI are flushed and before
// EndScene (device is an IDirect3DDevice9*); overlayDeviceLost runs with deviceLost, before
// default-pool resources are released.
inline void (*overlayDraw)(void* device)=nullptr;
inline void (*overlayDeviceLost)()=nullptr;
// Mouse input before the engine sees it. kind: 0 move, 1 button down, 2 button up, 3 input focus
// lost (an SDL button number is passed for 1 and 2); x and y are engine window coordinates.
// Returning true from a button event swallows it; moves are never swallowed.
inline bool (*mouseInput)(int kind,int button,int x,int y)=nullptr;
// Character Animation Studio: called at the top of CM2Model::SetBoneSequence, before the request is
// queued or applied. The callee may replace *sequenceId (never with the unset sentinel).
inline void (*m2SetBoneSequence)(CM2Model* model,unsigned boneId,unsigned* sequenceId)=nullptr;
}
