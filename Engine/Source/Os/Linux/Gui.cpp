#if !defined(_WIN32)

#include <cstdint>
#include <cstdio>
#include <SDL3/SDL.h>
#include "Engine/Source/Os/Gui.h"
#include "Engine/Source/Os/Input.h"
#include "Engine/Source/Os/Linux/SdlDisplay.h"
#include "Engine/Source/Os/Linux/SdlInternal.h"
#include "Engine/Source/Storm/H/storm.h"

// 4.3.4 registers an accessor rather than caching a handle, so window 0 is
// always the device's current window even after a device recreation. Matches
// Os/Win64/Gui.cpp.
static OsGuiHandle s_gxDevWindow = nullptr;

static SDL_Window* s_GxProcWindow;

static OsGuiWindowProc s_GxWindowProc;

static void* s_GxWindowParam;

static int32_t s_gxVisibleSent = 1;

// Win32's ShowCursor display counter: the cursor is visible while it is >= 0,
// and a mouse being installed makes 0 the starting value.
static int32_t s_showCursorCount;

// -1 so the first apply always reaches SDL.
static int32_t s_cursorHidden = -1;

static int32_t s_cursorClipped;

static int32_t s_cursorSuppressed;

static int32_t GxWindowVisible(SDL_Window* window);

static SDL_Window* GxWindow() {
    return static_cast<SDL_Window*>(OsGuiGetWindow(0));
}

// INLINED
int32_t COsDialog::OnEvent(int32_t itemId, int32_t notifyCode, int32_t code) {
    if (this->m_callback) {
        OsGuiCallbackParams params;
        params.type = itemId;
        params.subType = notifyCode;
        params.code = code;
        params.user = this->m_callbackParam;
        this->m_callback(params);
    }

    return 1;
}

// OFFSET: 0x0086C520
int32_t COsControl::SendEvent(int32_t event, int32_t code) {
    if (this->OnEvent(this->m_id, event, code)) {
        return 1;
    }

    if (this->m_dialog) {
        return this->m_dialog->OnEvent(this->m_id, event, code);
    }

    return 0;
}

// OFFSET: 0x0086C590
static int32_t sIsCharacterAllowed(char c, uint32_t filters) {
    uint8_t ch = static_cast<uint8_t>(c);

    if (ch <= 0x1F) {
        return 1;
    }

    if (filters & 0x1) {
        if (ch >= 'a' && ch <= 'z') {
            return 1;
        }

        if (ch >= 'A' && ch <= 'Z') {
            return 1;
        }
    }

    if ((filters & 0x2) && ch >= '0' && ch <= '9') {
        return 1;
    }

    if ((filters & 0x4) && (ch == '+' || ch == '-')) {
        return 1;
    }

    if ((filters & 0x8) && ch == '.') {
        return 1;
    }

    if ((filters & 0x10) && ch == ' ') {
        return 1;
    }

    if ((filters & 0x20) && ch == '_') {
        return 1;
    }

    return (filters & 0x40) && ch <= 0x7E;
}

// There are no native edit controls on this platform, so a tree view never has
// an in-place editor and the character filters below never engage.
void* COsTreeView::GetEditControl() {
    return nullptr;
}

// OFFSET: 0x0086C600
int32_t COsTreeView::IsCharacterAllowed(char c) {
    if (!this->GetEditControl()) {
        return 1;
    }

    if (this->m_filtersEnabled) {
        return sIsCharacterAllowed(c, this->m_filters);
    }

    return 1;
}

// OFFSET: 0x0086C650
void OsGuiSetWindowTitle(void* window, const char* title) {
    if (window) {
        SDL_SetWindowTitle(static_cast<SDL_Window*>(window), title ? title : "");
    }
}

// OFFSET: 0x0086C6A0
void* OsGuiGetWindow(int32_t type) {
    switch (type) {
        case 0:
            return s_gxDevWindow;

        // Win32 distinguishes the active window (this thread's) from the
        // foreground one (the system's). SDL only models keyboard focus, which
        // for a single-window game is both.
        case 1:
        case 2:
            return SDL_GetKeyboardFocus();

        default:
            return nullptr;
    }
}

// OFFSET: 0x0086C6D0
void OsGuiSetGxWindow(OsGuiHandle window) {
    s_gxDevWindow = window;
}

// ---------------------------------------------------------------------------
// Cursor state
// ---------------------------------------------------------------------------
//
// Single owner for SDL_ShowCursor / SDL_HideCursor and for the window mouse
// grab. The game draws its own cursor, so the system arrow has to go away while
// the pointer is over a focused game window -- but the Win32 ShowCursor shim
// also has an opinion, and relative mouse mode a third. All three resolve here.
//
// This is the only place in the tree that calls SDL_ShowCursor, SDL_HideCursor
// or SDL_SetWindowMouseGrab, and it must stay that way. Two writers with
// separate caches desynchronise silently: the cursor ends up visible over the
// game, or invisible over the desktop, with neither writer wrong on its own
// terms. s_cursorHidden is the cache, and it is only ever written here.

// Set when CGxDevice::IWindowCursorCreate installs a cursor image through
// SDL_SetCursor. See the hide logic below for why it matters.
static int32_t s_gxHardwareCursor;

void OsSdlSetGxHardwareCursor(int32_t active) {
    if (s_gxHardwareCursor == active) {
        return;
    }

    s_gxHardwareCursor = active;
    OsSdlApplyCursorState();
}

void OsSdlApplyCursorState() {
    SDL_Window* window = GxWindow();
    SDL_WindowFlags flags = window ? SDL_GetWindowFlags(window) : 0;
    int32_t focused = (flags & SDL_WINDOW_INPUT_FOCUS) != 0;
    int32_t over = (flags & SDL_WINDOW_MOUSE_FOCUS) != 0;

    // SDL3 made relative mouse mode a per-window property
    // (SDL_GetWindowRelativeMouseMode) where SDL2's SDL_GetRelativeMouseMode
    // was global. One window, so the answer is the same -- but it cannot be
    // asked without one, hence the guard.
    int32_t relative = window && SDL_GetWindowRelativeMouseMode(window);

    // The "pointer is over the focused window" term exists to hide the system
    // cursor so the engine's own drawn cursor is the only one visible. That is
    // wrong when a Gx HARDWARE cursor is installed: there, the system cursor
    // IS the game's cursor (SDL_SetCursor was handed the game's image), and
    // hiding it leaves no cursor at all.
    //
    // Relative mode still hides it unconditionally -- during mouse-look there
    // should be no pointer of either kind.
    int32_t hideForDrawnCursor = focused && over && !s_gxHardwareCursor;

    int32_t hide = s_showCursorCount < 0 || s_cursorSuppressed
        || hideForDrawnCursor || (focused && relative);

    if (s_cursorHidden != hide) {
        s_cursorHidden = hide;

        // SDL3 split SDL_ShowCursor(SDL_ENABLE/SDL_DISABLE) into two nullary
        // calls. Both return bool; the result is the usual "did it work", not
        // the previous visibility, and there is nothing useful to do with a
        // failure here.
        if (hide) {
            SDL_HideCursor();
        } else {
            SDL_ShowCursor();
        }
    }

    // Win32 ClipCursor confines the pointer to a screen rectangle. The only
    // rectangle the engine ever passes is the Gx window's own, so a window grab
    // is the faithful equivalent and does not need the rect at all.
    //
    // SDL_SetWindowGrab was split in SDL3 into SDL_SetWindowMouseGrab and
    // SDL_SetWindowKeyboardGrab. SDL2's SDL_SetWindowGrab was the mouse one.
    if (window) {
        SDL_SetWindowMouseGrab(window, s_cursorClipped && focused);
    }
}

void OsSdlSetCursorClip(int32_t clip) {
    s_cursorClipped = clip;
    OsSdlApplyCursorState();
}

// Called by the ShowCursor shim in Win32Window.cpp.
int32_t OsSdlShowCursorCount(int32_t delta) {
    s_showCursorCount += delta;
    OsSdlApplyCursorState();
    return s_showCursorCount;
}

// Called by the SetCursor shim in Win32Window.cpp.
void OsSdlSetCursorSuppressed(int32_t suppressed) {
    s_cursorSuppressed = suppressed;
    OsSdlApplyCursorState();
}

// ---------------------------------------------------------------------------
// The Gx window
// ---------------------------------------------------------------------------

// SDL2's SDL_WINDOW_ALLOW_HIGHDPI, renamed. Same meaning: opt in to a
// backing store at the display's real pixel density, so OsGuiGetDrawableSize
// can differ from SDL_GetWindowSize and the swapchain is created at native
// resolution rather than upscaled.
uint64_t OsGuiGetWindowFlags() {
    return SDL_WINDOW_HIGH_PIXEL_DENSITY;
}

void OsGuiGetDrawableSize(void* window, int32_t* width, int32_t* height) {
    int pixelWidth = 0;
    int pixelHeight = 0;

    if (window) {
        // SDL3 removed SDL_Vulkan_GetDrawableSize (and its GL/Metal siblings)
        // in favour of one graphics-API-independent call.
        SDL_GetWindowSizeInPixels(static_cast<SDL_Window*>(window), &pixelWidth, &pixelHeight);

        // Unlike SDL_Vulkan_GetDrawableSize this answers for any window, so
        // the fallback is now only for an outright failure rather than for a
        // window created without SDL_WINDOW_VULKAN. Points are still the best
        // available answer when it does fail.
        if (pixelWidth <= 0 || pixelHeight <= 0) {
            SDL_GetWindowSize(static_cast<SDL_Window*>(window), &pixelWidth, &pixelHeight);
        }
    }

    *width = pixelWidth;
    *height = pixelHeight;
}

void OsGuiGetGxSurfaceSize(void* window, int32_t fullscreen, uint32_t display, int32_t* width, int32_t* height) {
    // Fullscreen: the display's own size. A Vulkan window's drawable is a
    // pre-map placeholder right after creation -- taking it would size the
    // backbuffer to the placeholder and leave the real surface mostly black --
    // whereas the display bounds are known and correct now.
    if (fullscreen) {
        SDL_DisplayID target = static_cast<SDL_DisplayID>(display);

        if (!SdlDisplayValid(target)) {
            target = SdlPrimaryDisplay();
        }

        SDL_Rect bounds;

        if (target && SDL_GetDisplayBounds(target, &bounds) && bounds.w > 0 && bounds.h > 0) {
            *width = bounds.w;
            *height = bounds.h;
            return;
        }
    }

    // Windowed: the drawable (pixel) size, which is what was asked for and does
    // not depend on a compositor configure.
    OsGuiGetDrawableSize(window, width, height);
}

float OsGuiGetPixelScale(void* window) {
    if (!window) {
        return 1.0f;
    }

    int pointWidth = 0;
    int pointHeight = 0;
    int32_t pixelWidth = 0;
    int32_t pixelHeight = 0;

    SDL_GetWindowSize(static_cast<SDL_Window*>(window), &pointWidth, &pointHeight);
    OsGuiGetDrawableSize(window, &pixelWidth, &pixelHeight);

    if (pointWidth <= 0 || pointHeight <= 0) {
        return 1.0f;
    }

    float scaleX = static_cast<float>(pixelWidth) / static_cast<float>(pointWidth);
    float scaleY = static_cast<float>(pixelHeight) / static_cast<float>(pointHeight);

    return scaleX > scaleY ? scaleX : scaleY;
}

// Linking SDL3 directly makes this a plain flag read. Under sdl2-compat it was
// a dlopen/dlsym probe for SDL3's own SDL_GetWindowFlags, because bit 0x4 is
// SDL_WINDOW_OCCLUDED in SDL3 and SDL_WINDOW_SHOWN in SDL2 and the shim
// reported the SDL2 meaning. Same answer, no probe.
static int32_t GxWindowVisible(SDL_Window* window) {
    if (!window) {
        return 0;
    }

    SDL_WindowFlags flags = SDL_GetWindowFlags(window);

    int32_t width;
    int32_t height;
    OsGuiGetDrawableSize(window, &width, &height);

    int32_t blocked = (flags & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN | SDL_WINDOW_OCCLUDED)) != 0;

    return !blocked && width > 0 && height > 0;
}

// SDL3 replaced the single SDL_WINDOWEVENT carrying a sub-type in
// SDL_WindowEvent::event with one discrete SDL_EVENT_WINDOW_* type per case,
// so the "is this a window event" test is now a range check against
// SDL_EVENT_WINDOW_FIRST/LAST and the inner switch is on event->type itself.
//
// Returns true, where the SDL2 version returned 0. SDL ignores the return
// value of an event *watch* in both versions; the value only matters to
// SDL_SetEventFilter, where SDL3's convention is that false DROPS the event.
// true is therefore the safe constant, and 0/false would be the dangerous one
// if this were ever reused as a filter.
static bool GxWindowEventWatch(void* userdata, SDL_Event* event) {
    if (event->type < SDL_EVENT_WINDOW_FIRST || event->type > SDL_EVENT_WINDOW_LAST) {
        return true;
    }

    if (!s_GxWindowProc || SDL_GetWindowFromID(event->window.windowID) != s_GxProcWindow) {
        return true;
    }

    int32_t width;
    int32_t height;
    OsGuiGetDrawableSize(s_GxProcWindow, &width, &height);

    switch (event->type) {
        // SDL2's SDL_WINDOWEVENT_SIZE_CHANGED fired for any size change,
        // however caused. SDL3 split that into SDL_EVENT_WINDOW_RESIZED (the
        // window size, in points) and SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED (the
        // drawable size, which moves on its own under a Wayland scale change),
        // and both have to be handled to cover what the one event covered.
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_RESTORED:
        case SDL_EVENT_WINDOW_MAXIMIZED:
        case SDL_EVENT_WINDOW_SHOWN:
        case SDL_EVENT_WINDOW_EXPOSED:
            s_gxVisibleSent = GxWindowVisible(s_GxProcWindow);
            s_GxWindowProc(s_GxWindowParam, s_gxVisibleSent ? OS_GUI_WM_SIZE : OS_GUI_WM_MINIMIZE, width, height);
            break;

        case SDL_EVENT_WINDOW_MINIMIZED:
        case SDL_EVENT_WINDOW_HIDDEN:
            s_gxVisibleSent = 0;
            s_GxWindowProc(s_GxWindowParam, OS_GUI_WM_MINIMIZE, width, height);
            break;

        case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
            s_GxWindowProc(s_GxWindowParam, OS_GUI_WM_DISPLAYCHANGE, width, height);
            break;

        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            s_GxWindowProc(s_GxWindowParam, OS_GUI_WM_SETFOCUS, width, height);
            break;

        case SDL_EVENT_WINDOW_FOCUS_LOST:
            s_GxWindowProc(s_GxWindowParam, OS_GUI_WM_KILLFOCUS, width, height);
            break;

        default:
            break;
    }

    return true;
}

void OsGuiPollGxWindowState() {
    if (!s_GxWindowProc || !s_GxProcWindow) {
        return;
    }

    int32_t visible = GxWindowVisible(s_GxProcWindow);

    if (visible == s_gxVisibleSent) {
        return;
    }

    s_gxVisibleSent = visible;

    int32_t width;
    int32_t height;
    OsGuiGetDrawableSize(s_GxProcWindow, &width, &height);
    s_GxWindowProc(s_GxWindowParam, visible ? OS_GUI_WM_SIZE : OS_GUI_WM_MINIMIZE, width, height);
}

void OsGuiSetGxWindowProc(void* window, OsGuiWindowProc proc, void* param) {
    // FIX (2026-10-26): was `= 1` ("assume visible"). By the time this runs,
    // the window (created in OsGuiCreateGxWindow/FinalizeGxWindow, earlier)
    // has usually already been shown and is already genuinely visible --
    // and OsGuiPollGxWindowState() below only notifies the caller of a
    // CHANGE relative to this value, never the current state outright. With
    // `= 1` and a window that is, and stays, visible from the start, no
    // change is ever observed, so the caller (CGxDevice, via
    // GxWindowStateProc) never receives the OS_GUI_WM_SIZE that sets
    // CGxDevice::m_windowVisible true -- it stays stuck at its constructor
    // default of false FOREVER, which silently skips all real per-frame
    // scene/UI drawing in Scrn/Layer.cpp's OnPaint() (GxHasContext() &&
    // GxIsWindowVisible() gate) while Present() keeps right on flowing, so
    // the symptom is a window that is genuinely mapped, genuinely
    // compositing, and genuinely solid black -- indefinitely, since nothing
    // was ever going to call that dispatch again on its own. Confirmed via
    // direct instrumentation: SDL's own flags showed MINIMIZED=0 HIDDEN=0
    // OCCLUDED=0 the entire time, while GxIsWindowVisible() stayed 0 --
    // this was never a compositor/libdecor/explicit-sync quirk (all three
    // were tested and ruled out first). A minimize+restore "fixed" it only
    // as a side effect: that is the first time the SDL-reported state
    // actually CHANGES relative to s_gxVisibleSent, so it is the first time
    // the dispatch -- and the m_windowVisible = true it causes -- ever
    // fires.
    //
    // -1 (not a real GxWindowVisible() return value, which is always 0/1)
    // guarantees the very first OsGuiPollGxWindowState() call after
    // install sees a mismatch and dispatches the window's REAL current
    // state immediately, visible or not, instead of assuming.
    s_gxVisibleSent = -1;

    if (s_GxWindowProc) {
        SDL_RemoveEventWatch(GxWindowEventWatch, nullptr);
    }

    s_GxProcWindow = static_cast<SDL_Window*>(window);
    s_GxWindowProc = proc;
    s_GxWindowParam = param;

    if (proc) {
        SDL_AddEventWatch(GxWindowEventWatch, nullptr);
    }
}

// A freshly created window must be MAPPED before the compositor gives its
// surface a size, and only then does asserting fullscreen take effect. On a
// tiling Wayland compositor SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN does not
// reliably survive creation (SDL_GetWindowFlags comes back FULLSCREEN=0), so the
// window is tiled to a fraction of the output -- and dxvk's swapchain then
// adopts that odd size, which is a mispositioned/clipped (often black) surface.
// Map, sync, re-assert fullscreen, sync again: that is what makes the surface
// the size of the display. (The D3D11 path does the same in IWindowCreate.)
static void FinalizeGxWindow(SDL_Window* window, int32_t fullscreen) {
    if (!window) {
        return;
    }

    SDL_ShowWindow(window);
    SDL_RaiseWindow(window);

    // Window-state changes are asynchronous on Wayland and X11; wait for the
    // compositor to configure the mapped surface before its size is read (by
    // the caller or by dxvk at swapchain creation).
    SDL_SyncWindow(window);

    if (fullscreen) {
        SDL_SetWindowFullscreen(window, true);
        SDL_SyncWindow(window);
    }
}

void* OsGuiCreateGxWindow(const char* title, int32_t width, int32_t height, int32_t fullscreen, uint32_t display) {
    if (width <= 0 || height <= 0) {
        return nullptr;
    }

    // RETURN CONVENTION FLIPPED. SDL2's SDL_InitSubSystem returned 0 for
    // success, SDL3's returns true for success, so the sense of this test is
    // the opposite of what it was.
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        return nullptr;
    }

    // SDL_WINDOW_VULKAN is not optional: dxvk-native's SDL3 WSI presents by
    // calling SDL_Vulkan_CreateSurface on this window
    // (dxvk/src/wsi/sdl3/wsi_window_sdl3.cpp:createSurface).
    SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | OsGuiGetWindowFlags();

    if (fullscreen) {
        // SDL3 dropped SDL_WINDOW_FULLSCREEN_DESKTOP. SDL_WINDOW_FULLSCREEN
        // alone is now the borderless-desktop kind, and an exclusive mode
        // switch is requested separately with SDL_SetWindowFullscreenMode --
        // which this does not do, and did not do before either.
        flags |= SDL_WINDOW_FULLSCREEN;
    }

    // 0 means "wherever the system says is primary". A stale ID -- a monitor
    // unplugged since the caller read it -- is treated the same rather than
    // producing an unplaceable window.
    SDL_DisplayID target = static_cast<SDL_DisplayID>(display);

    if (!SdlDisplayValid(target)) {
        target = SdlPrimaryDisplay();
    }

    // SDL_WINDOWPOS_CENTERED_DISPLAY takes an SDL_DisplayID in SDL3 where it
    // took a display index in SDL2. The macro body is unchanged; the meaning
    // of its argument is not, which is the single most likely place for the
    // old code to have landed on the wrong monitor via sdl2-compat.
    int32_t position = target ? SDL_WINDOWPOS_CENTERED_DISPLAY(target) : SDL_WINDOWPOS_UNDEFINED;

    // Properties rather than SDL_CreateWindow, because SDL3 took the x and y
    // parameters off SDL_CreateWindow and SDL_CreateWindowWithProperties is
    // the only remaining way to choose the display a window is *born* on.
    //
    // Create-then-SDL_SetWindowPosition is not equivalent and is not good
    // enough here: on X11 the window is mapped at the default position first
    // and visibly jumps, and the window manager has already picked a work area
    // from the wrong monitor; on Wayland SDL_SetWindowPosition is advisory and
    // may never take effect at all. Both are the wrong-monitor symptom.
    //
    // The flag set above stays the single declaration of what this window is;
    // the booleans are derived from it mechanically so the two cannot drift.
    SDL_PropertiesID props = SDL_CreateProperties();

    if (props) {
        SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title ? title : "");
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width);
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height);
        // Position is set ONLY for a windowed window.
        //
        // An explicit position is windowed intent, and asking for it at the
        // same time as fullscreen is a contradiction the compositor resolves
        // by refusing the fullscreen. Measured: with both set,
        // SDL_SetWindowFullscreen returns success with an empty error and
        // SDL_SyncWindow completes, yet SDL_GetWindowFlags still reports
        // FULLSCREEN=0 and the surface stays at the requested window size
        // rather than the display's.
        //
        // dethrace (src/harness/platforms/sdl3.c:263), which works correctly
        // on this compositor, passes no position at all when going fullscreen.
        //
        // The display is still honoured: SDL places a fullscreen window on
        // the display it is created for, and SDL_WINDOWPOS_CENTERED_DISPLAY
        // is only needed to choose between displays for a windowed one.
        if (!(flags & SDL_WINDOW_FULLSCREEN)) {
            SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, position);
            SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, position);
        }
        SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_VULKAN_BOOLEAN, (flags & SDL_WINDOW_VULKAN) != 0);
        SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, (flags & SDL_WINDOW_RESIZABLE) != 0);
        SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN, (flags & SDL_WINDOW_HIGH_PIXEL_DENSITY) != 0);
        SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, (flags & SDL_WINDOW_FULLSCREEN) != 0);

        SDL_Window* window = SDL_CreateWindowWithProperties(props);
        SDL_DestroyProperties(props);

        if (window) {
            FinalizeGxWindow(window, fullscreen);
            return window;
        }
    }

    // SDL_CreateProperties only fails on allocation failure, and
    // SDL_CreateWindowWithProperties and SDL_CreateWindow fail for the same
    // reasons -- so this is a belt-and-braces path, not an expected one. It is
    // here because a window with the wrong position is still a playable game
    // and no window at all is not.
    SDL_Window* window = SDL_CreateWindow(title ? title : "", width, height, flags);

    if (window && target) {
        SDL_SetWindowPosition(window, position, position);
    }

    FinalizeGxWindow(window, fullscreen);
    return window;
}

void OsGuiDestroyGxWindow(void* window) {
    if (window == s_GxProcWindow) {
        if (s_GxWindowProc) {
            SDL_RemoveEventWatch(GxWindowEventWatch, nullptr);
        }

        s_GxProcWindow = nullptr;
        s_GxWindowProc = nullptr;
        s_GxWindowParam = nullptr;
    }

    OsInputSetMouseMode(OS_MOUSE_MODE_NORMAL);
    SDL_DestroyWindow(static_cast<SDL_Window*>(window));

    OsInputQuitDeferredEvents();
}

// ---------------------------------------------------------------------------
// Dialogs
// ---------------------------------------------------------------------------

// OFFSET: 0x0086C6E0
int32_t OsGuiMessageBox(void* parentWindow, int32_t style, const char* message, const char* title) {
    enum { RESULT_YES = 0, RESULT_NO = 1, RESULT_CANCEL = 2 };

    if (!SDL_WasInit(SDL_INIT_VIDEO)) {
        return RESULT_CANCEL;
    }

    SDL_MessageBoxButtonData buttons[3];
    int32_t count = 0;

    switch (style) {
        case 1:
            buttons[count++] = { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, RESULT_YES, "OK" };
            buttons[count++] = { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, RESULT_CANCEL, "Cancel" };
            break;

        case 2:
            buttons[count++] = { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, RESULT_YES, "Yes" };
            buttons[count++] = { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, RESULT_NO, "No" };
            break;

        case 3:
            buttons[count++] = { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, RESULT_YES, "Yes" };
            buttons[count++] = { 0, RESULT_NO, "No" };
            buttons[count++] = { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, RESULT_CANCEL, "Cancel" };
            break;

        default:
            buttons[count++] = { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, RESULT_YES, "OK" };
            break;
    }

    SDL_Window* parent = static_cast<SDL_Window*>(parentWindow);

    if (parent != GxWindow()) {
        parent = nullptr;
    }

    SDL_MessageBoxData data = {};
    data.flags = SDL_MESSAGEBOX_INFORMATION;
    data.window = parent;
    data.title = title ? title : "";
    data.message = message ? message : "";
    data.numbuttons = count;
    data.buttons = buttons;

    int pressed;

    // RETURN CONVENTION FLIPPED. SDL2's SDL_ShowMessageBox returned 0 on
    // success, SDL3's returns true on success. The pressed < 0 arm is
    // unchanged: that is "the box was dismissed without a button", which SDL
    // reports through the out parameter in both versions.
    if (!SDL_ShowMessageBox(&data, &pressed) || pressed < 0) {
        return RESULT_CANCEL;
    }

    return pressed;
}

// OFFSET: 0x0086C7D0
int32_t OsGuiIsModifierKeyDown(int32_t key) {
    SDL_Keymod mod = SDL_GetModState();

    switch (key) {
        case 0:
            return (mod & SDL_KMOD_CTRL) != 0;

        case 1:
            return (mod & SDL_KMOD_SHIFT) != 0;

        case 2:
            return (mod & SDL_KMOD_ALT) != 0;

        default:
            return 0;
    }
}

// Win32 calls MessageBeep. There is no portable system-beep API; the glue
// screens are the only caller and they are not load-bearing.
// INLINED
void OsGuiBeep() {
}

// INLINED
int32_t COsEditBox::IsCharacterAllowed(char c) {
    if (!this->m_filtersEnabled) {
        return 1;
    }

    return sIsCharacterAllowed(c, this->m_filters);
}

// No native tab control exists, so it reports no tabs and OnControlTab below
// becomes a no-op rather than misbehaving.
// INLINED
int32_t COsTabControl::GetNumItems() {
    return 0;
}

// INLINED
void* COsDialog::GetParentWindow() {
    return nullptr;
}

// INLINED
int32_t COsDialog::HasFlag(uint32_t flag) {
    return (this->m_flags & flag) != 0;
}

// INLINED
int32_t COsDialog::IsInFront() {
    return this->m_handle == OsGuiGetWindow(2);
}

// OFFSET: 0x0086C840
COsControl* COsDialog::FindControl(void* handle) {
    for (uint32_t i = 0; i < this->m_controls.Count(); i++) {
        if (this->m_controls[i]->IsHandleFromControl(handle)) {
            return this->m_controls[i];
        }
    }

    return nullptr;
}

// OFFSET: 0x0086C890
int32_t COsDialog::OnCancel() {
    if (!this->m_cancelButton || this->m_cancelButton->OnEvent(this->m_cancelButton->m_id, 0, 0)) {
        return 1;
    }

    return this->OnEvent(this->m_cancelButton->m_id, 0, 0);
}

// OFFSET: 0x0086C900
int32_t COsTabControl::OnControlTab() {
    int32_t shift = OsGuiIsModifierKeyDown(1);
    int32_t value = this->GetValue();
    int32_t count = this->GetNumItems();

    int32_t next = shift ? value - 1 : value + 1;

    if (next < 0) {
        next = count - 1;
    }

    if (next >= count) {
        next = 0;
    }

    this->SetValue(next);
    this->SendEvent(2, 0);

    return 1;
}

// OFFSET: 0x0086C980
int32_t COsDialog::OnControlTab() {
    int32_t count = this->m_controls.Count();

    for (int32_t i = 0; i < count; i++) {
        if (this->m_controls[i]->m_type == 15) {
            COsTabControl* tabControl = static_cast<COsTabControl*>(this->m_controls[i]);
            return tabControl ? tabControl->OnControlTab() : 0;
        }
    }

    return 0;
}

// The Win32 originals walk a MSG: dialog keyboard navigation, menu
// accelerators and tooltip relaying. None of it has a counterpart under SDL,
// where there are no native dialogs and no MSG to dispatch -- and the native
// dialogs were only ever used by the Windows-only tools, not the game.
// OFFSET: 0x0086C9C0
int32_t COsDialog::ProcessMessage(void* message) {
    return 0;
}

// OFFSET: 0x0086CB00
int32_t OsGuiProcessMessage(void* message) {
    return 0;
}

#endif  // !_WIN32
