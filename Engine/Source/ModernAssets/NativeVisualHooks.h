#pragma once
#include <cstddef>
#include <cstdint>
// Render-thread-only bridges. The graphics backend invokes these before releasing
// default-pool resources, including resize, device loss and shutdown.
class CGxShader;
namespace NativeVisualHooks {
inline bool waterDepthWriteOverride=false;
inline void (*deviceLost)()=nullptr;
// Called when the engine selects shaders; the WMO material module latches the pair.
inline void (*wmoBind)(CGxShader* vertex,CGxShader* pixel)=nullptr;
// Called in place of an indexed draw (device is an IDirect3DDevice9*); returns true
// when the callee issued the draw itself.
inline bool (*wmoDraw)(void* device,int type,int base,unsigned minimum,unsigned count,unsigned start,unsigned primitives)=nullptr;
// Called as the engine loads a shader: may substitute offline-patched bytecode (grass motion).
inline bool (*replaceShaderCode)(const uint8_t* code,size_t size,const uint32_t** out,size_t* outBytes)=nullptr;
}
