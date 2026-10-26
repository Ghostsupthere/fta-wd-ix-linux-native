#ifndef ENGINE_SOURCE_OS_LINUX_SDLDISPLAY_H
#define ENGINE_SOURCE_OS_LINUX_SDLDISPLAY_H

#if !defined(_WIN32)

#include <SDL3/SDL.h>
#include <cstdint>

// ---------------------------------------------------------------------------
// The HMONITOR encoding
// ---------------------------------------------------------------------------
//
// dxvk-native's SDL3 WSI identifies monitors as an SDL_DisplayID cast straight
// to HMONITOR, with NO offset -- see dxvk/include/native/wsi/native_sdl3.h:
//
//     inline SDL_DisplayID fromHmonitor(HMONITOR h) {
//       return SDL_DisplayID(reinterpret_cast<uintptr_t>(h));
//     }
//     inline HMONITOR toHmonitor(SDL_DisplayID d) {
//       return reinterpret_cast<HMONITOR>(uintptr_t(d));
//     }
//
// The two functions below are those, verbatim, with HMONITOR spelled as void*
// so this header does not have to drag in windows.h.
//
// The SDL2 WSI added +1 in both directions, because display *index* 0 would
// otherwise encode as a NULL handle. SDL3 has no such problem: an
// SDL_DisplayID is never 0 -- 0 is the documented failure return of
// SDL_GetPrimaryDisplay and SDL_GetDisplayForWindow -- so the offset is gone
// and a zero handle means "no monitor" by itself.
//
// GetMonitorInfoA/EnumDisplaySettingsA in Win32Window.cpp must use the same
// mapping, or the handles DXGI hands the engine will not resolve. A mismatch
// here fails silently, with nothing logged anywhere.
inline SDL_DisplayID SdlDisplayFromHmonitor(const void* monitor) {
    return SDL_DisplayID(reinterpret_cast<uintptr_t>(monitor));
}

inline void* SdlHmonitorFromDisplay(SDL_DisplayID display) {
    return reinterpret_cast<void*>(uintptr_t(display));
}

// ---------------------------------------------------------------------------
// Display enumeration
// ---------------------------------------------------------------------------
//
// SDL3 replaced SDL_GetNumVideoDisplays() plus 0..n-1 index loops with a
// single call returning an SDL_malloc'd array of opaque IDs. This owns that
// array so no caller has to remember the SDL_free, and gives back the two
// things the old index loops were really asking for: "how many" and "the nth".
//
// An index is NOT a display ID and the two are not interchangeable. Only this
// class converts between them, and only where a Win32 API genuinely enumerates
// by position (EnumDisplaySettingsA's mode number is the other such case).
class SdlDisplayList {
public:
    SdlDisplayList()
        : m_displays(SDL_GetDisplays(&m_count)) {
        if (!m_displays) {
            m_count = 0;
        }
    }

    ~SdlDisplayList() {
        SDL_free(m_displays);
    }

    SdlDisplayList(const SdlDisplayList&) = delete;
    SdlDisplayList& operator=(const SdlDisplayList&) = delete;

    int32_t Count() const {
        return m_count;
    }

    // 0 -- never a valid SDL_DisplayID -- for an out-of-range index.
    SDL_DisplayID operator[](int32_t index) const {
        return index >= 0 && index < m_count ? m_displays[index] : 0;
    }

    int32_t IndexOf(SDL_DisplayID display) const {
        if (!display) {
            return -1;
        }

        for (int32_t i = 0; i < m_count; i++) {
            if (m_displays[i] == display) {
                return i;
            }
        }

        return -1;
    }

    bool Contains(SDL_DisplayID display) const {
        return this->IndexOf(display) >= 0;
    }

private:
    int m_count = 0;
    SDL_DisplayID* m_displays;
};

// Replaces `display >= 0 && display < SDL_GetNumVideoDisplays()`, which was
// the SDL2 way of saying "is this a display I can ask about". A display ID is
// not an index into anything, so the range test has no meaning and a live
// lookup is the only honest check -- a hot-unplugged monitor keeps its old ID
// but stops appearing in the list.
inline bool SdlDisplayValid(SDL_DisplayID display) {
    if (!display) {
        return false;
    }

    SdlDisplayList displays;
    return displays.Contains(display);
}

// SDL3 answers this directly, which SDL2 could not: there was no
// SDL_GetPrimaryDisplay, display 0 was connector order rather than the desktop
// origin, and the heuristic below was the best available substitute.
//
// Keeping that heuristic as a fallback rather than returning 0: every caller
// uses the result to place or interrogate a window, and 0 is not a usable
// display ID. SDL_GetPrimaryDisplay only fails when the video subsystem is not
// up, in which case the fallback returns 0 too.
inline SDL_DisplayID SdlPrimaryDisplay() {
    SDL_DisplayID primary = SDL_GetPrimaryDisplay();

    if (primary) {
        return primary;
    }

    SdlDisplayList displays;

    SDL_DisplayID origin = 0;
    SDL_DisplayID largest = 0;
    int32_t largestArea = -1;

    for (int32_t i = 0; i < displays.Count(); i++) {
        SDL_DisplayID display = displays[i];

        SDL_Rect bounds;
        if (!SDL_GetDisplayBounds(display, &bounds) || bounds.w <= 0 || bounds.h <= 0) {
            continue;
        }

        if (bounds.x == 0 && bounds.y == 0) {
            origin = display;
        }

        int32_t area = bounds.w * bounds.h;
        if (area > largestArea) {
            largestArea = area;
            largest = display;
        }
    }

    return origin ? origin : largest;
}

// ---------------------------------------------------------------------------
// Points versus pixels
// ---------------------------------------------------------------------------
//
// SDL3 is explicit about two different units, and the engine is not. Which one
// a number is in is never visible from its type, so it is written down here
// once:
//
//   POINTS   SDL_CreateWindow's w/h, SDL_GetDisplayBounds,
//            SDL_GetDisplayUsableBounds, SDL_GetWindowSize,
//            SDL_SetWindowPosition, SDL_DisplayMode::w/h, and every mouse
//            coordinate in an SDL event.
//
//   PIXELS   SDL_GetWindowSizeInPixels, the Vulkan surface dxvk-native sizes
//            from it (wsi_window_sdl3.cpp: getWindowSize), the D3D11
//            swapchain, CGxFormat::m_size, and the resolution in Config.wtf.
//
// They differ by exactly this factor. On an unscaled display it is 1.0 and the
// distinction is invisible, which is why it has gone unnoticed: SDL2 had the
// same two units with less ceremony about them.
//
// This matters because a resolution read from Config.wtf is in PIXELS and
// SDL_CreateWindow's w/h are in POINTS. Handing one straight to the other asks
// for a window `density` times too large on a scaled display, and the
// compositor clamps it to the monitor -- so the client renders at the monitor's
// point size rather than the configured pixel size. Convert with
// SdlPixelsToPoints first.
//
// Taken from the desktop mode's pixel_density, which SDL documents as exactly
// this ratio ("a 1920x1080 mode with 2.0 scale would have 3840x2160 pixels").
inline float SdlDisplayPixelDensity(SDL_DisplayID display) {
    const SDL_DisplayMode* mode = display ? SDL_GetDesktopDisplayMode(display) : nullptr;

    return mode && mode->pixel_density > 0.0f ? mode->pixel_density : 1.0f;
}

// Never returns 0: a zero-sized window cannot be created, and the caller's
// own `<= 0` guard would turn a rounding artefact into a failed device create.
inline int32_t SdlPixelsToPoints(int32_t pixels, float density) {
    if (pixels <= 0 || density <= 0.0f || density == 1.0f) {
        return pixels;
    }

    int32_t points = static_cast<int32_t>(static_cast<float>(pixels) / density + 0.5f);

    return points > 0 ? points : 1;
}

inline int32_t SdlPointsToPixels(int32_t points, float density) {
    if (points <= 0 || density <= 0.0f || density == 1.0f) {
        return points;
    }

    int32_t pixels = static_cast<int32_t>(static_cast<float>(points) * density + 0.5f);

    return pixels > 0 ? pixels : 1;
}

// The engine's DEVMODEA.dmBitsPerPel and CGxFormat::m_bpp are Win32 colour
// depths, where 24-bit is spelled 32.
inline uint32_t SdlModeBpp(SDL_PixelFormat format) {
    uint32_t bpp = SDL_BITSPERPIXEL(format);

    if (bpp == 24) {
        return 32;
    }

    return bpp;
}

// SDL3's SDL_DisplayMode::refresh_rate is a float (SDL2's was an int). Win32
// reports DEVMODEA.dmDisplayFrequency and CGxFormat refresh rates as whole Hz,
// so a 59.94Hz mode has to round rather than truncate to 59.
inline uint32_t SdlModeRefreshRate(float refreshRate) {
    if (refreshRate <= 0.0f) {
        return 0;
    }

    return static_cast<uint32_t>(refreshRate + 0.5f);
}

#endif  // !_WIN32

#endif
