#ifndef ENGINE_SOURCE_MODERNASSETS_NATIVEMODELPRESENTATION_H
#define ENGINE_SOURCE_MODERNASSETS_NATIVEMODELPRESENTATION_H
#include <cstdint>
class CM2Model;
class C44Matrix;
struct NativePreviewView { float zoom = 1.0f, pan = 0.0f, panX = 0.0f; };
bool NormalizeNativePreview(double zoom, double pan, double panX, NativePreviewView& result);
void ApplyNativePreview(C44Matrix& matrix, const NativePreviewView& view);
uint64_t NativePreviewGeneration();
void ResetNativePreviewCameras();
uint64_t NativeEquipmentPreviewGeneration();
void ClearNativeEquipmentPreviews();
bool SetNativeEquipmentScales(double weapon, double shield);
void GetNativeEquipmentScales(float& weapon, float& shield);
void SetNativeEquipmentResolver(CM2Model* (*resolver)());
void RefreshNativeEquipmentOwner(); // UI thread, before native animation workers.
void ApplyNativeEquipmentScale(CM2Model* model);
#endif
