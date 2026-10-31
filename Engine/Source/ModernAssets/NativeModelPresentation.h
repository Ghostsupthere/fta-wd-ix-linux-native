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
void SetNativeWeaponFeatureEnabled(bool enabled);
bool NativeWeaponFeatureEnabled();
void SetNativeWeaponFlip(uint32_t mode, bool enabled); // Back/main/off/reverse bits: 1/2/4/8/16.
bool GetNativeWeaponFlip(uint32_t mode);
uint32_t NativeWeaponEligibility(); // Current local root; UI thread only.
void SetNativeRemoteWeaponResolver(CM2Model* (*resolver)(uint64_t));
bool SetNativeRemoteWeaponFlags(uint64_t guid, bool hands, uint32_t flags);
void BeginNativeRemoteWeaponSync(bool hands);
void EndNativeRemoteWeaponSync(bool hands);
void ClearNativeRemoteWeaponSync(bool hands);
void ResetNativeWeaponPresentation();
void InvalidateNativeWeaponWorld(); // Clear remote identities/snapshots before world teardown.
#endif
