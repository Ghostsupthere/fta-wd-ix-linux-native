#ifndef ENGINE_SOURCE_MODERNASSETS_NATIVEITEMPHYSICS_H
#define ENGINE_SOURCE_MODERNASSETS_NATIVEITEMPHYSICS_H
class CM2Model;
class CM2Scene;
struct WxlItemPhysicsState;
void RestoreNativeItemPhysics(CM2Scene* scene);
void ApplyNativeItemPhysics(CM2Scene* scene);
void RetireNativeItemPhysics(CM2Model* model);
#endif
