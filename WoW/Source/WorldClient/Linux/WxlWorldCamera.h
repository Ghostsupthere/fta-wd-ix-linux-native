#ifndef WOW_SOURCE_WORLDCLIENT_LINUX_WXLWORLDCAMERA_H
#define WOW_SOURCE_WORLDCLIENT_LINUX_WXLWORLDCAMERA_H

// The world camera in world coordinates. The engine's Gx view has the eye at zero and the native world
// draws camera-relative geometry, so the missing translation is composed back in (see WxlDrawLootBeams).
struct WxlWorldCamera {
    float view[16] = {};
    float projection[16] = {};
    float eye[3] = {};
    bool valid = false;
};

// Reads the view/projection the Gx device holds right now; only meaningful while the world pass is
// drawing. Returns false (and leaves `out` invalid) when there is no usable camera yet.
bool WxlCaptureWorldCamera(WxlWorldCamera& out);

#endif
