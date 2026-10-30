// Production presentation state/policy, shared with the native API fixture.
namespace {
std::atomic<float> weaponScale{1.0f}, shieldScale{1.0f};
std::atomic<CM2Model*> localModel{nullptr};
CM2Model* (*localResolver)() = nullptr; // Registered and called on the UI thread.
uint64_t cameraGeneration = 1;
std::atomic<uint64_t> equipmentGeneration{1};
}
bool NormalizeNativePreview(double zoom, double pan, double panX, NativePreviewView& result) {
    if (!std::isfinite(zoom) || !std::isfinite(pan) || !std::isfinite(panX)) return false;
    result = {float(std::clamp(zoom, 1.0, 8.0)), float(std::clamp(pan, -8.0, 8.0)), float(std::clamp(panX, -8.0, 8.0))};
    return true;
}
void ApplyNativePreview(C44Matrix& matrix, const NativePreviewView& view) {
    matrix.a0 *= view.zoom; matrix.b1 *= view.zoom;
    matrix.c0 += view.panX * matrix.c3; matrix.c1 += view.pan * matrix.c3;
}
uint64_t NativePreviewGeneration() { return cameraGeneration; }
void ResetNativePreviewCameras() { ++cameraGeneration; }
uint64_t NativeEquipmentPreviewGeneration() { return equipmentGeneration.load(std::memory_order_relaxed); }
void ClearNativeEquipmentPreviews() { ++equipmentGeneration; }
bool SetNativeEquipmentScales(double weapon, double shield) {
    float w, s;
    if (!wxl::equipmentscale::Normalize(weapon, w) || !wxl::equipmentscale::Normalize(shield, s)) return false;
    weaponScale.store(w, std::memory_order_relaxed); shieldScale.store(s, std::memory_order_relaxed);
    return true;
}
void GetNativeEquipmentScales(float& weapon, float& shield) {
    weapon = weaponScale.load(std::memory_order_relaxed); shield = shieldScale.load(std::memory_order_relaxed);
}
void SetNativeEquipmentResolver(CM2Model* (*resolver)()) {
    localResolver = resolver; localModel.store(nullptr, std::memory_order_relaxed);
}
void RefreshNativeEquipmentOwner() {
    localModel.store(localResolver ? localResolver() : nullptr, std::memory_order_relaxed);
}
#include "Engine/Source/ModernAssets/NativeEquipmentScale.inl"
