// Native stand-in for the WarcraftXL core's event definitions, covering only the events and argument
// structs the supplied wxl-unit-outline uses (Outline.cpp, GPL-3.0-or-later, Copyright (C) 2026
// WarcraftXL). The Windows core fires them from detours; here the native engine calls
// events::Dispatch from the matching points (see WxlUnitOutline.cpp).
#pragma once

#include <chrono>
#include <cstdint>

// Match the existing native D3D9 translation units' Windows type boundary (see WxlLootBeam.cpp).
#if defined(WOWDECOMP_LINUX)
#define WOWDECOMP_GX_DXVK
#include <d3d9.h>
#endif

// The supplied code guards a few raw-pointer reads with MSVC structured exception handling. The native
// bindings below validate before dereferencing, so these keywords become ordinary C++ try/catch.
#undef __try
#undef __except
#undef EXCEPTION_EXECUTE_HANDLER
#define __try try
#define __except(filter) catch (...)
#define EXCEPTION_EXECUTE_HANDLER 1

inline uint32_t GetTickCount() {
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

namespace wxl::events {

enum class Event : int {
    OnEndScene,
    OnDeviceLost,
    OnDeviceReset,
    OnM2BatchDraw,
    OnWorldLeave,
    Count,
};

struct EndSceneArgs { void* device; };
struct DeviceResetArgs { void* device; };
struct WorldLeaveArgs { int unused = 0; };
// Fired immediately after the native indexed draw of one M2 batch, with the same parameters, while the
// source state and vertex/index buffers are still bound. uiModelPass is true for glue/paper-doll models.
struct M2BatchDrawArgs {
    void* device;
    void* model;
    int primType;
    int baseVertex;
    uint32_t minIndex;
    uint32_t numVerts;
    uint32_t startIndex;
    uint32_t primCount;
    uint32_t phase;
    uint16_t skinSectionId;
    bool uiModelPass;
    void* uiModelFrame;
};

}  // namespace wxl::events
