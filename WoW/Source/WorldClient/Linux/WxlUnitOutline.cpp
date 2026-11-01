// Engine glue for the native unit-outline port: installs the event sources the supplied wxl-unit-outline
// (WxlUnitOutline/Outline.cpp) subscribes to. The Windows core fired OnM2BatchDraw, OnEndScene, OnDeviceLost,
// OnDeviceReset and OnWorldLeave from detours; here the same events are dispatched from the native draw path.
#if defined(WOWDECOMP_LINUX)
#define WOWDECOMP_GX_DXVK
#include <d3d9.h>
#endif
#include "WoW/Source/WorldClient/Linux/WxlUnitOutline.h"
#if defined(WOWDECOMP_LINUX)
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/ModernAssets/NativeDiagnostic.h"
#include "Engine/Source/ModernAssets/NativeVisualHooks.h"
#include "WoW/Source/WorldClient/Linux/WxlUnitOutline/events/EventScript.hpp"
#include "WoW/Source/WorldClient/Linux/WxlUnitOutline/game/camera/Camera.hpp"
#include "WoW/Source/WorldClient/Linux/WxlVisualEnvironment.h"
#include "WoW/Source/WorldClient/World.h"

void WxlUnitOutlineInit();               // WxlUnitOutline/Outline.cpp
bool WxlUnitOutlineStoreCamera();        // WxlUnitOutline/NativeFacade.cpp

namespace {
namespace ev = wxl::events;
bool g_installed = false;
bool g_cameraFresh = false;

void OnBatch(CM2Model* model, void* device, int type, int base, unsigned minimum, unsigned count, unsigned start, unsigned primitives) {
    // The outline reads the world camera for its ground cues; take it once per frame, at the first model batch
    // of the world pass, when the Gx matrices are the world camera's.
    if (!g_cameraFresh) g_cameraFresh = WxlUnitOutlineStoreCamera();
    ev::M2BatchDrawArgs args{};
    args.device = device;
    args.model = model;
    args.primType = type;
    args.baseVertex = base;
    args.minIndex = minimum;
    args.numVerts = count;
    args.startIndex = start;
    args.primCount = primitives;
    args.phase = 1;
    args.uiModelPass = model->m_scene != World::s_m2Scene;
    ev::Dispatch(ev::Event::OnM2BatchDraw, args);
}

void OnOverlay(void* device) {
    ev::Dispatch(ev::Event::OnEndScene, ev::EndSceneArgs{device});
    g_cameraFresh = false;
}

void OnDeviceLost() {
    // A null device releases every default-pool resource the module holds.
    ev::Dispatch(ev::Event::OnDeviceLost, ev::DeviceResetArgs{nullptr});
    g_cameraFresh = false;
}
}  // namespace

void WxlUnitOutlineInstall() {
    if (g_installed || !WxlProfileEnabled("WXL_UNIT_OVERLAYS", false)) return;
    g_installed = true;
    WxlUnitOutlineInit();
    NativeVisualHooks::m2BatchDrawn = &OnBatch;
    NativeVisualHooks::outlineOverlay = &OnOverlay;
    NativeVisualHooks::outlineDeviceLost = &OnDeviceLost;
    WxlNativeDiagnostic("unit-outline: installed");
}

void WxlUnitOutlineWorldLeave() {
    if (!g_installed) return;
    ev::Dispatch(ev::Event::OnWorldLeave, ev::WorldLeaveArgs{});
    g_cameraFresh = false;
}
#else
void WxlUnitOutlineInstall() {}
void WxlUnitOutlineWorldLeave() {}
#endif
