#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <SDL3/SDL.h>
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Os/Linux/SdlDisplay.h"
#include "Engine/Source/Os/Linux/SdlInternal.h"

static int32_t InitVideo() {
    // RETURN CONVENTION FLIPPED. SDL2's SDL_InitSubSystem returned 0 for
    // success, SDL3's returns true for success, so testing `== 0` reported
    // failure exactly when the init had SUCCEEDED (see the same note in
    // OsGuiCreateGxWindow and OsInputInitializeJoystick, which already handle
    // it). Nothing else brings the video subsystem up before the adapter
    // queries below, so the FIRST query in the process always failed while
    // silently initialising video as a side effect, and every later one worked.
    // Two user-visible consequences: a desktop-mode default could never be
    // read, and CVGxResolutionCallback -- which runs during RegisterGxCVars and
    // validates gxResolution against AdapterMonitorModes -- got an empty mode
    // list and fell back to its hardcoded 4:3-only ladder, rejecting any
    // widescreen gxResolution saved in Config.wtf and replacing it with the
    // 1024x768 default (which then vanished from Config.wtf on exit, because
    // CVar::IterateForArchive omits any value equal to its default).
    return SDL_WasInit(SDL_INIT_VIDEO) || SDL_InitSubSystem(SDL_INIT_VIDEO);
}

static uint32_t ReadSysfsHex(const char* path) {
    FILE* file = fopen(path, "r");
    if (!file) {
        return 0;
    }
    unsigned int value = 0;
    if (fscanf(file, "%x", &value) != 1) {
        value = 0;
    }
    fclose(file);
    return value;
}

static int CGxMonitorModeSort(const void* a, const void* b) {
    const CGxMonitorMode* modeA = static_cast<const CGxMonitorMode*>(a);
    const CGxMonitorMode* modeB = static_cast<const CGxMonitorMode*>(b);

    return modeA->m_size.x * modeA->m_size.y - modeB->m_size.x * modeB->m_size.y;
}

int32_t CGxDevice::AdapterFormats(EGxApi api, TSGrowableArray<CGxFormat>& formats) {
    formats.SetCount(0);
    formats.m_chunk = 64;
    formats.Reserve(256, 0);

#if defined(WOWDECOMP_VULKAN)
    if (api == GxApi_Vulkan) {
        CGxDevice::VulkanAdapterFormats(formats);
        return formats.Count() != 0;
    }
#endif

    // The native build runs the stock D3D9 renderer on dxvk-native's
    // libdxvk_d3d9.so. There is no IDirect3D9 to enumerate against the way the
    // Windows D3d9ExAdapterFormats does (and dxvk supports the full set
    // regardless), so publish the standard D3D9 colour/depth/MSAA combinations
    // statically. Resolutions are matched separately against AdapterMonitorModes
    // (the SDL mode list). Without a NON-EMPTY format list here,
    // LoadFormatFromCVars cannot match the requested gxResolution, rejects the
    // CVar format, and the client comes up at a hardware-detected size.
    if (api == GxApi_D3d9 || api == GxApi_D3d9Ex) {
        static const CGxFormat::Format colorFormats[] = { CGxFormat::Fmt_Rgb565, CGxFormat::Fmt_Argb8888, CGxFormat::Fmt_Argb2101010 };
        static const CGxFormat::Format depthFormats[] = { CGxFormat::Fmt_Ds160, CGxFormat::Fmt_Ds248, CGxFormat::Fmt_Ds320 };
        static const uint32_t sampleCounts[] = { 1, 2, 4, 8 };

        for (CGxFormat::Format colorFormat : colorFormats) {
            for (CGxFormat::Format depthFormat : depthFormats) {
                for (uint32_t sampleCount : sampleCounts) {
                    CGxFormat format = {};
                    format.m_colorFormat = colorFormat;
                    format.m_depthFormat = depthFormat;
                    format.m_multisampleCount = sampleCount;

                    if (!CGxDevice::FormatExists(formats, format)) {
                        *formats.New() = format;
                    }
                }
            }
        }
    }

    return formats.Count() != 0;
}

int32_t CGxDevice::DeviceAdapterID(uint16_t& vendorID, uint16_t& deviceID, uint32_t& driverVersionHi, uint32_t& driverVersionLo) {
    driverVersionHi = 0;
    driverVersionLo = 0;
    vendorID = static_cast<uint16_t>(ReadSysfsHex("/sys/class/drm/card0/device/vendor"));
    deviceID = static_cast<uint16_t>(ReadSysfsHex("/sys/class/drm/card0/device/device"));

    int32_t result = vendorID && deviceID;
    if (!result) {
        vendorID = 0xFFFF;
        deviceID = 0xFFFF;
    }

    CGxDevice::Log("CGxDevice::DeviceAdapterID(): RET: %d, VID: %x, DID: %x, DVER: %x.%x", result, vendorID, deviceID, driverVersionHi, driverVersionLo);

    return result;
}

int32_t CGxDevice::DeviceAdapterInfer(uint16_t& deviceID) {
    deviceID = 3;
    CGxDevice::Log("CGxDevice::DeviceAdapterInfer(): RET: %d, DID: %x", 1, deviceID);
    return 1;
}

// Hardware cursor via SDL. dxvk-native's D3D9 SetCursorProperties is a no-op on
// Linux ("D3D9Cursor::SetHardwareCursor: Not supported"), so the engine's
// hardware-cursor request would otherwise leave no cursor at all (the engine
// skips its own software cursor whenever m_hwCursor is set). The engine fills
// m_cursor (32x32 0xAARRGGBB) and the hotspot, then CursorUnlock calls this; we
// turn that into an SDL color cursor, which the compositor draws. m_cursor is
// 0xAARRGGBB = SDL_PIXELFORMAT_ARGB8888 on a little-endian host, and
// SDL_CreateColorCursor copies the pixels so the surface is only a view.
static SDL_Cursor* s_gxSdlCursor = nullptr;

void CGxDevice::IWindowCursorCreate() {
    SDL_Surface* surface = SDL_CreateSurfaceFrom(32, 32, SDL_PIXELFORMAT_ARGB8888, this->m_cursor, 32 * sizeof(uint32_t));

    if (!surface) {
        CGxDevice::Log("CGxDevice::IWindowCursorCreate(): SDL_CreateSurfaceFrom failed: %s", SDL_GetError());
        return;
    }

    SDL_Cursor* cursor = SDL_CreateColorCursor(surface, this->m_cursorHotspotX, this->m_cursorHotspotY);
    SDL_DestroySurface(surface);

    if (!cursor) {
        CGxDevice::Log("CGxDevice::IWindowCursorCreate(): SDL_CreateColorCursor failed: %s", SDL_GetError());
        return;
    }

    if (s_gxSdlCursor) {
        SDL_DestroyCursor(s_gxSdlCursor);
    }

    s_gxSdlCursor = cursor;
    SDL_SetCursor(cursor);

    // Tell the Os cursor-state owner the system cursor IS the game's image, so
    // it stays visible rather than being hidden for an engine-drawn one.
    OsSdlSetGxHardwareCursor(1);
}

void CGxDevice::IWindowCursorDestroy() {
    if (s_gxSdlCursor) {
        SDL_DestroyCursor(s_gxSdlCursor);
        s_gxSdlCursor = nullptr;
    }

    OsSdlSetGxHardwareCursor(0);
}

// Translate the SDL window events (delivered by Gui.cpp's event watch and
// OsGuiPollGxWindowState) into the engine's DeviceWM messages. This is the SDL
// stand-in for CGxDeviceD3d9Ex::WindowProcD3d, which handles WM_SIZE &c. on
// Windows but is compiled out on Linux. Without it the engine never receives
// GxWM_Size, never learns the window is sized/visible, and its render loop
// stays gated -- the 0-draws black screen.
static void GxWindowStateProc(void* param, OSGUIWINDOWMSG message, int32_t width, int32_t height) {
    CGxDevice* device = static_cast<CGxDevice*>(param);

    if (!device) {
        return;
    }

    CRect rect;
    rect.minX = 0.0f;
    rect.minY = 0.0f;
    rect.maxX = static_cast<float>(width);
    rect.maxY = static_cast<float>(height);

    switch (message) {
        case OS_GUI_WM_SIZE:
            device->DeviceWM(GxWM_Size, reinterpret_cast<uintptr_t>(&rect), 0);
            break;

        // Win32 SIZE_MINIMIZED (param2 == 1): stop presenting, do not resize to 0.
        case OS_GUI_WM_MINIMIZE:
            device->DeviceWM(GxWM_Size, reinterpret_cast<uintptr_t>(&rect), 1);
            break;

        case OS_GUI_WM_SETFOCUS:
            device->DeviceWM(GxWM_SetFocus, 0, 0);
            break;

        case OS_GUI_WM_KILLFOCUS:
            device->DeviceWM(GxWM_KillFocus, 0, 0);
            break;

        default:
            break;
    }
}

void CGxDevice::IInstallGxWindowProc(void* window) {
    OsGuiSetGxWindowProc(window, GxWindowStateProc, this);
}

int32_t CGxDevice::AdapterDesktopMode(CGxMonitorMode& mode) {
    if (!InitVideo()) {
        return 0;
    }

    // SDL3 returns the desktop mode by pointer (owned by SDL) rather than
    // filling a caller struct, and takes only the display id.
    const SDL_DisplayMode* displayMode = SDL_GetDesktopDisplayMode(SdlPrimaryDisplay());
    if (!displayMode) {
        return 0;
    }

    mode.m_size.x = displayMode->w;
    mode.m_size.y = displayMode->h;
    mode.m_refreshRate = static_cast<int32_t>(displayMode->refresh_rate);
    mode.m_bpp = SdlModeBpp(displayMode->format);

    return 1;
}

int32_t CGxDevice::AdapterMonitorModes(TSGrowableArray<CGxMonitorMode>& modes) {
    modes.SetCount(0);

    if (!InitVideo()) {
        return 0;
    }

    // SDL3 returns the fullscreen mode list as a NULL-terminated, SDL-owned
    // array of pointers plus a count, instead of SDL2's index-and-fill pair.
    int32_t count = 0;
    SDL_DisplayMode** sdlModes = SDL_GetFullscreenDisplayModes(SdlPrimaryDisplay(), &count);

    if (sdlModes) {
        for (int32_t i = 0; i < count; i++) {
            const SDL_DisplayMode* displayMode = sdlModes[i];

            uint32_t bpp = SdlModeBpp(displayMode->format);
            if (displayMode->w >= 640 && displayMode->h >= 480 && bpp >= 16) {
                CGxMonitorMode* mode = modes.New();
                mode->m_size.x = displayMode->w;
                mode->m_size.y = displayMode->h;
                mode->m_bpp = bpp;
                mode->m_refreshRate = static_cast<int32_t>(displayMode->refresh_rate);
            }
        }

        SDL_free(sdlModes);
    }

    // Wayland's fullscreen-mode list omits most resolutions (SDL reports only a
    // handful, often not the native one), so the client can't select them and
    // ValidateFormatMonitor steps gxResolution down to whatever small mode IS
    // listed (e.g. 1024x768). dxvk scales any backbuffer to the fullscreen
    // surface, so every standard resolution is actually usable -- publish the
    // common set plus the desktop's own size, deduped, using the desktop mode's
    // bpp and refresh so ValidateFormatMonitor's refresh match succeeds.
    const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(SdlPrimaryDisplay());
    uint32_t stdBpp = desktop ? SdlModeBpp(desktop->format) : 32;
    int32_t stdRefresh = desktop ? static_cast<int32_t>(desktop->refresh_rate) : 60;
    if (stdBpp < 16) {
        stdBpp = 32;
    }

    C2iVector common[] = {
        { 640, 480 }, { 800, 600 }, { 1024, 768 }, { 1152, 864 },
        { 1280, 720 }, { 1280, 1024 }, { 1366, 768 }, { 1600, 900 },
        { 1600, 1200 }, { 1680, 1050 }, { 1920, 1080 }, { 1920, 1200 },
        { 2560, 1080 }, { 2560, 1440 }, { 3440, 1440 }, { 3840, 2160 },
        { desktop ? desktop->w : 0, desktop ? desktop->h : 0 },
    };

    for (const C2iVector& res : common) {
        if (res.x < 640 || res.y < 480) {
            continue;
        }

        int32_t present = 0;
        for (uint32_t i = 0; i < modes.Count(); i++) {
            if (modes[i].m_size.x == res.x && modes[i].m_size.y == res.y) {
                present = 1;
                break;
            }
        }

        if (!present) {
            CGxMonitorMode* mode = modes.New();
            mode->m_size.x = res.x;
            mode->m_size.y = res.y;
            mode->m_bpp = stdBpp;
            mode->m_refreshRate = stdRefresh;
        }
    }

    qsort(modes.Ptr(), modes.Count(), sizeof(CGxMonitorMode), CGxMonitorModeSort);

    return modes.Count() != 0;
}
