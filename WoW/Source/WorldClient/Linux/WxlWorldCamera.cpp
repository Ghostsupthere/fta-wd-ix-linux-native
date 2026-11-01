#if defined(WOWDECOMP_LINUX)
#include "WoW/Source/WorldClient/Linux/WxlWorldCamera.h"

#include <cmath>
#include <cstring>

#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Tempest/C44Matrix.h"
#include "WoW/Source/WorldClient/World.h"

static bool Finite(const float* values, int count) {
    for (int i = 0; i < count; ++i)
        if (!std::isfinite(values[i])) return false;
    return true;
}

bool WxlCaptureWorldCamera(WxlWorldCamera& out) {
    out.valid = false;
    CGxDevice* gx = g_theGxDevicePtr;
    const C3Vector* eye = World::GetCamPos();
    if (!gx || !eye) return false;
    out.eye[0] = eye->x; out.eye[1] = eye->y; out.eye[2] = eye->z;
    C44Matrix view;
    GxXformView(view);
    std::memcpy(out.view, &view, sizeof(out.view));
    // Already the native D3D depth projection: no GL-to-D3D conversion is needed.
    std::memcpy(out.projection, &gx->m_projNative, sizeof(out.projection));
    for (unsigned c = 0; c < 4; ++c)
        out.view[12 + c] -= out.eye[0] * out.view[c] + out.eye[1] * out.view[4 + c] + out.eye[2] * out.view[8 + c];
    out.valid = Finite(out.eye, 3) && Finite(out.view, 16) && Finite(out.projection, 16) &&
                std::abs(out.projection[5]) > 0.001f && std::abs(out.projection[11]) > 0.001f &&
                !(out.eye[0] == 0.0f && out.eye[1] == 0.0f && out.eye[2] == 0.0f);   // origin: no camera yet (glue screen)
    return out.valid;
}
#endif
