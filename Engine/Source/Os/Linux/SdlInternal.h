#ifndef ENGINE_SOURCE_OS_LINUX_SDLINTERNAL_H
#define ENGINE_SOURCE_OS_LINUX_SDLINTERNAL_H

#if !defined(_WIN32)

#include <cstdint>

// The Linux-only part of the Os surface.
//
// Engine/Source/Os/Gui.h, Input.h and the rest are shared across platforms and
// are not ours to extend, so everything the SDL implementation needs on top of
// them lives here instead. Two audiences:
//
//   * Os/Linux itself -- Input.cpp needs the drawable size and the window-state
//     poll that Gui.cpp owns, and Gui.cpp needs Input.cpp's deferred teardown.
//
//   * the Gx backend -- on Windows the Gx device creates its own HWND with
//     CreateWindowExA (Gx/CGxDevice/Win64/CGxDevice.cpp:919). On Linux SDL has
//     to own the window, because dxvk-native's SDL3 WSI calls
//     SDL_Vulkan_CreateSurface on it. A Gx/CGxDevice/Linux/CGxDevice.cpp
//     includes this header and calls OsGuiCreateGxWindow; the resulting
//     SDL_Window* IS the HWND it hands to DXGI_SWAP_CHAIN_DESC.OutputWindow.

enum OSGUIWINDOWMSG {
    OS_GUI_WM_SIZE = 0,
    OS_GUI_WM_MINIMIZE = 1,
    OS_GUI_WM_DISPLAYCHANGE = 2,
    OS_GUI_WM_SETFOCUS = 3,
    OS_GUI_WM_KILLFOCUS = 4,
};

// Width and height are drawable pixels, delivered on the thread pumping SDL
// events.
typedef void (*OsGuiWindowProc)(void* param, OSGUIWINDOWMSG message, int32_t width, int32_t height);

// window is an SDL_Window*, which is also the HWND on this platform.
//
// width and height are WINDOW POINTS, not pixels -- that is what SDL3's
// SDL_CreateWindow takes, and what SDL_GetDisplayBounds/UsableBounds report, so
// a size derived from display bounds can be passed straight through. A
// resolution that came from CGxFormat::m_size or Config.wtf is in PIXELS and
// must go through SdlPixelsToPoints(px, SdlDisplayPixelDensity(display))
// first; see the long note in SdlDisplay.h. On an unscaled display the two are
// equal and it makes no difference, which is exactly why it is easy to get
// wrong.
//
// display is an SDL_DisplayID (uint32_t, so this header needs no SDL headers).
// The window is CENTRED ON IT AT CREATION TIME. 0 means "whichever display
// SDL_GetPrimaryDisplay names", and an ID that is no longer connected is
// treated as 0 rather than failing.
//
// Defaulted so the existing four-argument call site keeps compiling -- but a
// caller that leaves it out gets the primary display, which IS the
// wrong-monitor bug. The Gx backend has to pass the display its CGxFormat
// names.
//
// Implemented with SDL_CreateWindowWithProperties, because SDL3 took the x and
// y parameters off SDL_CreateWindow and properties are the only way left to
// choose the display a window is *born* on. Create-then-SDL_SetWindowPosition
// is not the same thing: on X11 the window is mapped on the wrong monitor
// first and visibly jumps.
//
// LIMITATION, not fixable here: under Wayland the compositor places windows and
// SDL_SetWindowPosition is documented as deniable, so neither the creation
// properties nor a later move is guaranteed to be honoured. A *fullscreen*
// window still lands on the right output, but by a different route --
// SDL_SetWindowFullscreenMode with a mode whose displayID names it, which
// dxvk-native's SDL3 WSI does from the HMONITOR DXGI gives it. That makes the
// HMONITOR encoding in SdlDisplay.h, not this parameter, the thing that decides
// the fullscreen monitor on Wayland.
// Told to the Os layer by CGxDevice::IWindowCursorCreate, so the cursor
// state machine knows the system cursor is the game's own image and must stay
// visible rather than being hidden for an engine-drawn one.
void OsSdlSetGxHardwareCursor(int32_t active);

void* OsGuiCreateGxWindow(const char* title, int32_t width, int32_t height, int32_t fullscreen, uint32_t display = 0);
void OsGuiDestroyGxWindow(void* window);
void OsGuiSetGxWindowProc(void* window, OsGuiWindowProc proc, void* param);
void OsGuiPollGxWindowState();

// Drawable pixels, which is what Gx and the UI work in. Not the same as
// SDL_GetWindowSize under HiDPI or fractional Wayland scaling.
//
// Backed by SDL_GetWindowSizeInPixels: SDL3 removed
// SDL_Vulkan_GetDrawableSize, and the replacement is not Vulkan-specific, so
// it answers for any window rather than only a SDL_WINDOW_VULKAN one.
void OsGuiGetDrawableSize(void* window, int32_t* width, int32_t* height);
float OsGuiGetPixelScale(void* window);

// The size to build the Gx backbuffer / swapchain at so it matches the surface
// dxvk-native actually creates (it sizes the Vulkan surface from the window's
// pixel size). Fullscreen: the display's own size -- a Vulkan window's drawable
// is a pre-map placeholder right after creation and cannot be trusted until
// dxvk presents. Windowed: the drawable pixel size. Without this the backbuffer
// stays at the requested resolution, smaller than the surface, and the client
// renders into a small corner (UI small and centred, the rest black).
void OsGuiGetGxSurfaceSize(void* window, int32_t fullscreen, uint32_t display, int32_t* width, int32_t* height);

// uint64_t, not uint32_t: SDL3's SDL_WindowFlags is a Uint64 bitfield. Every
// flag we set still lives in the low 32 bits, but narrowing the return type
// would silently drop any flag above bit 31.
uint64_t OsGuiGetWindowFlags();

// The system cursor has exactly one owner, in Gui.cpp. Both the input pump
// (pointer entered/left a focused window) and the Win32 ShowCursor/SetCursor
// shims feed into it rather than calling SDL_ShowCursor/SDL_HideCursor
// themselves, or the last writer wins and the two disagree. The window mouse
// grab has the same single owner, for the same reason.
void OsSdlApplyCursorState();
void OsSdlSetCursorClip(int32_t clip);

// Moves Win32's ShowCursor display counter by delta and returns the new value,
// which is what ShowCursor itself returns.
int32_t OsSdlShowCursorCount(int32_t delta);

// SetCursor(NULL) means "hide the system cursor, I draw my own".
void OsSdlSetCursorSuppressed(int32_t suppressed);

// SDL's event subsystem cannot be shut down before the window it owns is
// destroyed, and OsInputDestroy runs first. Gui.cpp calls this after
// SDL_DestroyWindow to finish the job.
void OsInputQuitDeferredEvents();

#endif  // !_WIN32

#endif
