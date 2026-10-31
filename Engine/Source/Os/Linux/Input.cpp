#if !defined(_WIN32)

#include <cstdint>
#include <SDL3/SDL.h>
#include "Engine/Source/Base/OsTypes.h"
#include "Engine/Source/Event/Types.h"
#include "Engine/Source/ModernAssets/NativeVisualHooks.h"
#include "Engine/Source/Os/Gui.h"
#include "Engine/Source/Os/Input.h"
#include "Engine/Source/Os/Joystick.h"
#include "Engine/Source/Os/Linux/SdlInternal.h"
#include "Engine/Source/Os/TimeManager.h"
#include "Engine/Source/Storm/H/storm.h"
#include "Engine/Source/Tempest/C2iVector.h"

static int32_t s_appActive = 1;

static int32_t s_relResidueX;

static int32_t s_relResidueY;

static int32_t s_residueWidth;

static int32_t s_residueHeight;

static int32_t s_residuePixelWidth;

static int32_t s_residuePixelHeight;

static C2iVector s_mousePos;

static int32_t s_queueHead;

static int32_t s_queueTail;

static OSEVENT s_queue[16];

static int32_t s_mouseSpeed = 10;

static uint32_t s_osButtonState;

static tagRECT s_defaultWindowRect;

static int32_t s_windowSize;

static OS_MOUSE_MODE s_osMouseMode;

static int32_t s_WindowResizeLock;

static void (*s_eventPollProc)();

static int32_t s_quitEventsOnWindowDestroy;

// SDL3's SDL_MouseMotionEvent::xrel/yrel are floats; SDL2's were Sint32. A
// high-resolution mouse, or any pointer-acceleration curve, delivers
// fractional deltas, and truncating each one on its own would quietly throw
// slow movement away -- 0.4 px per event forever is 0 px. The remainder is
// carried to the next event, which is the same trick ScaleAxis already plays
// with the point-to-pixel residue one stage later.
static float s_relFracX;

static float s_relFracY;

// SDL3's text input is per-window (SDL_StartTextInput takes an SDL_Window*)
// where SDL2's was global. OsInputInitialize runs before the Gx window exists,
// so what it can record is the intent; the state is applied against whatever
// window is live, every pump, from ApplyTextInput below.
static int32_t s_textInputWanted;

static SDL_Window* GxWindow() {
    return static_cast<SDL_Window*>(OsGuiGetWindow(0));
}

// Driven off SDL_TextInputActive rather than a cached SDL_Window*, which is
// deliberate: the Gx window is destroyed and recreated on a device reset, and
// a cached pointer would be dangling exactly when SDL_StopTextInput needed it.
// Asking SDL makes this idempotent, stateless and correct across a recreation,
// for the cost of one call per pump.
static void ApplyTextInput(SDL_Window* window) {
    if (!window) {
        return;
    }

    if (s_textInputWanted) {
        if (!SDL_TextInputActive(window)) {
            SDL_StartTextInput(window);
        }
    } else if (SDL_TextInputActive(window)) {
        SDL_StopTextInput(window);
    }
}

// BEHAVIOUR CHANGE, and a necessary one. SDL2's relative mouse mode was global
// process state and survived anything short of SDL_Quit. SDL3's is a property
// of a window, so it dies with the window -- and the Gx window IS destroyed
// and recreated, on every resolution change and every device reset. Under the
// SDL2 semantics the mode simply stayed on across that; under SDL3 the game
// would come back from a resolution change with the camera no longer turning
// and nothing in the engine aware that anything had been lost.
//
// So s_osMouseMode is the single source of truth and the SDL side is a
// projection of it, re-asserted against whatever window is live. Exactly the
// shape of OsSdlApplyCursorState, and for the same reason: one writer, one
// cache, no way for the two to disagree. OsInputSetMouseMode below does not
// call SDL itself -- it updates s_osMouseMode and comes through here.
static void ApplyMouseMode(SDL_Window* window) {
    if (!window) {
        return;
    }

    bool relative = s_osMouseMode == OS_MOUSE_MODE_RELATIVE;

    if (SDL_GetWindowRelativeMouseMode(window) != relative) {
        SDL_SetWindowRelativeMouseMode(window, relative);
    }
}

static void GetPixelSize(SDL_Window* window, int32_t* width, int32_t* height) {
    OsGuiGetDrawableSize(window, width, height);
}

static int32_t ScaleAxis(int32_t value, int32_t from, int32_t to, int32_t* residue) {
    int64_t scaled = static_cast<int64_t>(value) * to;

    if (residue) {
        scaled += *residue;
        int64_t whole = scaled / from;
        *residue = static_cast<int32_t>(scaled - whole * from);
        return static_cast<int32_t>(whole);
    }

    if (scaled >= 0) {
        scaled += from / 2;
    } else {
        scaled -= from / 2;
    }

    return static_cast<int32_t>(scaled / from);
}

// SDL reports window points; Gx and the UI work in drawable pixels, which
// differ under HiDPI and fractional Wayland scaling. Relative deltas carry a
// residue so slow mouse movement is not quantised away.
static void ScalePoint(int32_t* x, int32_t* y, int32_t toPixels, int32_t relative = 0) {
    SDL_Window* window = GxWindow();

    if (!window) {
        return;
    }

    int width;
    int height;
    int32_t pixelWidth;
    int32_t pixelHeight;
    SDL_GetWindowSize(window, &width, &height);
    GetPixelSize(window, &pixelWidth, &pixelHeight);

    if (width <= 0 || height <= 0 || pixelWidth <= 0 || pixelHeight <= 0 || (width == pixelWidth && height == pixelHeight)) {
        if (relative) {
            s_relResidueX = 0;
            s_relResidueY = 0;
        }

        return;
    }

    if (relative && (s_residueWidth != width || s_residueHeight != height || s_residuePixelWidth != pixelWidth || s_residuePixelHeight != pixelHeight)) {
        s_residueWidth = width;
        s_residueHeight = height;
        s_residuePixelWidth = pixelWidth;
        s_residuePixelHeight = pixelHeight;
        s_relResidueX = 0;
        s_relResidueY = 0;
    }

    if (toPixels) {
        *x = ScaleAxis(*x, width, pixelWidth, relative ? &s_relResidueX : nullptr);
        *y = ScaleAxis(*y, height, pixelHeight, relative ? &s_relResidueY : nullptr);
    } else {
        *x = ScaleAxis(*x, pixelWidth, width, nullptr);
        *y = ScaleAxis(*y, pixelHeight, height, nullptr);
    }
}

// OFFSET: 0x00868D40
void OsQueuePut(OSINPUT id, int32_t param0, int32_t param1, int32_t param2, int32_t param3) {
    int32_t next = s_queueTail == 15 ? 0 : s_queueTail + 1;

    if (next == s_queueHead) {
        s_queueHead = s_queueHead == 15 ? 0 : s_queueHead + 1;
    }

    OSEVENT& event = s_queue[s_queueTail];
    event.id = id;
    event.param[0] = param0;
    event.param[1] = param1;
    event.param[2] = param2;
    event.param[3] = OsGetAsyncTimeMs();

    s_queueTail = next;
}

// OFFSET: 0x00868DB0
int32_t OsQueueGet(OSINPUT* id, int32_t* param0, int32_t* param1, int32_t* param2, int32_t* param3) {
    if (s_queueHead == s_queueTail) {
        return 0;
    }

    OSEVENT& event = s_queue[s_queueHead];
    *id = event.id;
    *param0 = event.param[0];
    *param1 = event.param[1];
    *param2 = event.param[2];
    *param3 = event.param[3];

    s_queueHead = s_queueHead == 15 ? 0 : s_queueHead + 1;

    return 1;
}

// The engine's KEY enum is the Win32 virtual-key set renumbered; its printable
// range is literally ASCII, which is what makes the last two cases work.
static int32_t ConvertKeyCode(SDL_Keycode code, KEY* key) {
    if (code >= SDLK_F1 && code <= SDLK_F12) {
        *key = static_cast<KEY>(KEY_F1 + (code - SDLK_F1));
        return 1;
    }

    if (code >= SDLK_KP_1 && code <= SDLK_KP_9) {
        *key = static_cast<KEY>(KEY_NUMPAD1 + (code - SDLK_KP_1));
        return 1;
    }

    switch (code) {
        case SDLK_LSHIFT: *key = KEY_LSHIFT; return 1;
        case SDLK_RSHIFT: *key = KEY_RSHIFT; return 1;
        case SDLK_LCTRL: *key = KEY_LCONTROL; return 1;
        case SDLK_RCTRL: *key = KEY_RCONTROL; return 1;
        case SDLK_LALT: *key = KEY_LALT; return 1;
        case SDLK_RALT: *key = KEY_RALT; return 1;
        case SDLK_ESCAPE: *key = KEY_ESCAPE; return 1;
        case SDLK_RETURN: *key = KEY_ENTER; return 1;
        case SDLK_KP_ENTER: *key = KEY_ENTER; return 1;
        case SDLK_BACKSPACE: *key = KEY_BACKSPACE; return 1;
        case SDLK_TAB: *key = KEY_TAB; return 1;
        case SDLK_LEFT: *key = KEY_LEFT; return 1;
        case SDLK_UP: *key = KEY_UP; return 1;
        case SDLK_RIGHT: *key = KEY_RIGHT; return 1;
        case SDLK_DOWN: *key = KEY_DOWN; return 1;
        case SDLK_INSERT: *key = KEY_INSERT; return 1;
        case SDLK_DELETE: *key = KEY_DELETE; return 1;
        case SDLK_HOME: *key = KEY_HOME; return 1;
        case SDLK_END: *key = KEY_END; return 1;
        case SDLK_PAGEUP: *key = KEY_PAGEUP; return 1;
        case SDLK_PAGEDOWN: *key = KEY_PAGEDOWN; return 1;
        case SDLK_CAPSLOCK: *key = KEY_CAPSLOCK; return 1;
        case SDLK_NUMLOCKCLEAR: *key = KEY_NUMLOCK; return 1;
        case SDLK_SCROLLLOCK: *key = KEY_SCROLLLOCK; return 1;
        case SDLK_PAUSE: *key = KEY_PAUSE; return 1;
        case SDLK_PRINTSCREEN: *key = KEY_PRINTSCREEN; return 1;
        case SDLK_SPACE: *key = KEY_SPACE; return 1;
        case SDLK_KP_0: *key = KEY_NUMPAD0; return 1;
        case SDLK_KP_PLUS: *key = KEY_NUMPAD_PLUS; return 1;
        case SDLK_KP_MINUS: *key = KEY_NUMPAD_MINUS; return 1;
        case SDLK_KP_MULTIPLY: *key = KEY_NUMPAD_MULTIPLY; return 1;
        case SDLK_KP_DIVIDE: *key = KEY_NUMPAD_DIVIDE; return 1;
        case SDLK_KP_PERIOD: *key = KEY_NUMPAD_DECIMAL; return 1;
        default: break;
    }

    // SDL3 renamed SDLK_a..SDLK_z to SDLK_A..SDLK_Z. The *values* did not
    // change -- SDLK_A is still 0x61, 'a' -- so this is still the lower-case
    // ASCII range being folded up to the engine's upper-case KEY_A..KEY_Z.
    if (code >= SDLK_A && code <= SDLK_Z) {
        *key = static_cast<KEY>(code - SDLK_A + 'A');
        return 1;
    }

    if (code > 0x20 && code < 0x7F) {
        *key = static_cast<KEY>(code);
        return 1;
    }

    return 0;
}

static int32_t ConvertKeyToKeyCode(KEY key, SDL_Keycode* code) {
    if (key >= KEY_F1 && key <= KEY_F12) {
        *code = SDLK_F1 + (key - KEY_F1);
        return 1;
    }

    if (key >= KEY_NUMPAD1 && key <= KEY_NUMPAD9) {
        *code = SDLK_KP_1 + (key - KEY_NUMPAD1);
        return 1;
    }

    switch (key) {
        case KEY_LSHIFT: *code = SDLK_LSHIFT; return 1;
        case KEY_RSHIFT: *code = SDLK_RSHIFT; return 1;
        case KEY_LCONTROL: *code = SDLK_LCTRL; return 1;
        case KEY_RCONTROL: *code = SDLK_RCTRL; return 1;
        case KEY_LALT: *code = SDLK_LALT; return 1;
        case KEY_RALT: *code = SDLK_RALT; return 1;
        case KEY_ESCAPE: *code = SDLK_ESCAPE; return 1;
        case KEY_ENTER: *code = SDLK_RETURN; return 1;
        case KEY_BACKSPACE: *code = SDLK_BACKSPACE; return 1;
        case KEY_TAB: *code = SDLK_TAB; return 1;
        case KEY_LEFT: *code = SDLK_LEFT; return 1;
        case KEY_UP: *code = SDLK_UP; return 1;
        case KEY_RIGHT: *code = SDLK_RIGHT; return 1;
        case KEY_DOWN: *code = SDLK_DOWN; return 1;
        case KEY_INSERT: *code = SDLK_INSERT; return 1;
        case KEY_DELETE: *code = SDLK_DELETE; return 1;
        case KEY_HOME: *code = SDLK_HOME; return 1;
        case KEY_END: *code = SDLK_END; return 1;
        case KEY_PAGEUP: *code = SDLK_PAGEUP; return 1;
        case KEY_PAGEDOWN: *code = SDLK_PAGEDOWN; return 1;
        case KEY_CAPSLOCK: *code = SDLK_CAPSLOCK; return 1;
        case KEY_NUMLOCK: *code = SDLK_NUMLOCKCLEAR; return 1;
        case KEY_SCROLLLOCK: *code = SDLK_SCROLLLOCK; return 1;
        case KEY_PAUSE: *code = SDLK_PAUSE; return 1;
        case KEY_PRINTSCREEN: *code = SDLK_PRINTSCREEN; return 1;
        case KEY_SPACE: *code = SDLK_SPACE; return 1;
        case KEY_NUMPAD0: *code = SDLK_KP_0; return 1;
        case KEY_NUMPAD_PLUS: *code = SDLK_KP_PLUS; return 1;
        case KEY_NUMPAD_MINUS: *code = SDLK_KP_MINUS; return 1;
        case KEY_NUMPAD_MULTIPLY: *code = SDLK_KP_MULTIPLY; return 1;
        case KEY_NUMPAD_DIVIDE: *code = SDLK_KP_DIVIDE; return 1;
        case KEY_NUMPAD_DECIMAL: *code = SDLK_KP_PERIOD; return 1;
        default: break;
    }

    if (key >= KEY_A && key <= KEY_Z) {
        *code = SDLK_A + (key - KEY_A);
        return 1;
    }

    if (key > 0x20 && key < 0x7F) {
        *code = static_cast<SDL_Keycode>(key);
        return 1;
    }

    return 0;
}

static int32_t ConvertButton(uint8_t sdlButton, MOUSEBUTTON* button) {
    switch (sdlButton) {
        case SDL_BUTTON_LEFT: *button = MOUSE_BUTTON_LEFT; return 1;
        case SDL_BUTTON_MIDDLE: *button = MOUSE_BUTTON_MIDDLE; return 1;
        case SDL_BUTTON_RIGHT: *button = MOUSE_BUTTON_RIGHT; return 1;
        case SDL_BUTTON_X1: *button = MOUSE_BUTTON_XBUTTON1; return 1;
        case SDL_BUTTON_X2: *button = MOUSE_BUTTON_XBUTTON2; return 1;
        default: return 0;
    }
}

static void SaveMouse(int32_t x, int32_t y) {
    if (s_osMouseMode == OS_MOUSE_MODE_RELATIVE) {
        return;
    }

    s_mousePos.x = x;
    s_mousePos.y = y;
}

static void WarpMouse(int32_t x, int32_t y) {
    SDL_Window* window = GxWindow();

    if (!window) {
        return;
    }

    ScalePoint(&x, &y, 0);

    // SDL3 takes float window coordinates here. The engine's are integral
    // pixels, so the conversion is exact and nothing is rounded away.
    SDL_WarpMouseInWindow(window, static_cast<float>(x), static_cast<float>(y));
}

static void RestoreMouse() {
    WarpMouse(s_mousePos.x, s_mousePos.y);
}

// SDL hands text input over as UTF-8 -- SDL_TextInputEvent::text, which SDL3
// changed from an inline char[32] to a const char* but left UTF-8 and
// NUL-terminated. The engine's OS_INPUT_CHAR carries one code point per event.
static void QueueText(const char* text) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(text);

    while (*p) {
        uint32_t ch = *p++;
        int32_t extra = ch >= 0xF0 ? 3 : ch >= 0xE0 ? 2 : ch >= 0xC0 ? 1 : 0;

        if (extra) {
            ch &= 0x3F >> extra;
        }

        for (; extra && (*p & 0xC0) == 0x80; extra--) {
            ch = (ch << 6) | (*p++ & 0x3F);
        }

        if (ch >= 0x20) {
            OsQueuePut(OS_INPUT_CHAR, static_cast<int32_t>(ch), 1, 0, 0);
        }
    }
}

// OFFSET: 0x00869620
void OsInputSetWindowResizeLock(int32_t resizeLock) {
    s_WindowResizeLock = resizeLock;
}

// OFFSET: 0x00869630
float OsInputGetMouseSpeed() {
    return s_mouseSpeed * 0.1f;
}

int32_t OsInputIsKeyDown(KEY key) {
    SDL_Keycode code;

    if (!ConvertKeyToKeyCode(key, &code)) {
        return 0;
    }

    int count;

    // SDL3 changed the keyboard state array from Uint8 to bool, and gave
    // SDL_GetScancodeFromKey a second out parameter for the modifier state
    // that would have to be held to produce that keycode. We only want the
    // physical key, so nullptr.
    const bool* state = SDL_GetKeyboardState(&count);
    SDL_Scancode scancode = SDL_GetScancodeFromKey(code, nullptr);

    return scancode > SDL_SCANCODE_UNKNOWN && scancode < count && state[scancode];
}

int32_t OsInputIsMouseButtonDown(MOUSEBUTTON button) {
    SDL_MouseButtonFlags state = SDL_GetMouseState(nullptr, nullptr);

    switch (button) {
        case MOUSE_BUTTON_LEFT:
            return (state & SDL_BUTTON_LMASK) != 0;

        case MOUSE_BUTTON_MIDDLE:
            return (state & SDL_BUTTON_MMASK) != 0;

        case MOUSE_BUTTON_RIGHT:
            return (state & SDL_BUTTON_RMASK) != 0;

        case MOUSE_BUTTON_XBUTTON1:
            return (state & SDL_BUTTON_X1MASK) != 0;

        case MOUSE_BUTTON_XBUTTON2:
            return (state & SDL_BUTTON_X2MASK) != 0;

        default:
            return 0;
    }
}

void OsInputInitialize() {
    // The game closes its own window; letting SDL quit on last-window-close
    // races the engine's shutdown. Minimise-on-focus-loss is a Windows
    // fullscreen behaviour the engine handles itself.
    SDL_SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0");
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    SDL_InitSubSystem(SDL_INIT_EVENTS);

    // Recorded, not applied: SDL3 needs a window to start text input on and
    // there is none yet. ApplyTextInput picks this up on the first pump.
    s_textInputWanted = 1;
}

void OsInputDestroy() {
    s_textInputWanted = 0;
    ApplyTextInput(GxWindow());

    // The Gx window outlives this call, and SDL cannot shut the event
    // subsystem down while a window it owns still exists.
    if (OsGuiGetWindow(0)) {
        s_quitEventsOnWindowDestroy = 1;
        return;
    }

    SDL_QuitSubSystem(SDL_INIT_EVENTS);
}

void OsInputQuitDeferredEvents() {
    if (!s_quitEventsOnWindowDestroy) {
        return;
    }

    s_quitEventsOnWindowDestroy = 0;
    SDL_QuitSubSystem(SDL_INIT_EVENTS);
}

static void HandleMouseButton(const SDL_MouseButtonEvent& event, OSINPUT id) {
    MOUSEBUTTON button;

    if (!ConvertButton(event.button, &button)) {
        return;
    }

    // Win32 sets capture on the first button down so a drag that leaves the
    // window still delivers its up event.
    if (id == OS_INPUT_MOUSE_DOWN) {
        if (!s_osButtonState) {
            SDL_CaptureMouse(true);
        }

        s_osButtonState |= button;
    } else {
        s_osButtonState &= ~button;

        if (!s_osButtonState) {
            SDL_CaptureMouse(false);
        }
    }

    // SDL3's event coordinates are floats in window points. SDL2's were ints
    // in window points, so truncating here reproduces exactly what SDL2 would
    // have reported for the same pointer position.
    int32_t x = static_cast<int32_t>(event.x);
    int32_t y = static_cast<int32_t>(event.y);

    if (s_osMouseMode == OS_MOUSE_MODE_RELATIVE) {
        x = s_mousePos.x;
        y = s_mousePos.y;
    } else {
        ScalePoint(&x, &y, 1);
    }

    // The game-object editor can claim a click on its gizmo.
    if (NativeVisualHooks::mouseInput && NativeVisualHooks::mouseInput(id == OS_INPUT_MOUSE_DOWN ? 1 : 2, event.button, x, y)) {
        return;
    }

    OsQueuePut(id, button, x, y, 0);
}

// SDL3 replaced SDL_WINDOWEVENT, whose SDL_WindowEvent::event field named the
// sub-type, with one discrete SDL_EVENT_WINDOW_* event type per case. So the
// switch is on event.type now, and SDL_WindowEvent has no `event` member at
// all.
static void HandleWindowEvent(const SDL_WindowEvent& event) {
    switch (event.type) {
        // SDL2's SIZE_CHANGED covered any size change and RESIZED only an
        // external one; SDL3's RESIZED covers both and PIXEL_SIZE_CHANGED
        // covers a drawable-size change with no point-size change, which is
        // what a Wayland scale change looks like.
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
        case SDL_EVENT_WINDOW_RESTORED:
        case SDL_EVENT_WINDOW_SHOWN:
        case SDL_EVENT_WINDOW_EXPOSED:
            s_windowSize = 1;
            break;

        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            s_appActive = event.type == SDL_EVENT_WINDOW_FOCUS_GAINED;
            s_osButtonState = 0;

            if (NativeVisualHooks::mouseInput) {
                NativeVisualHooks::mouseInput(3, 0, 0, 0);
            }

            if (!s_appActive) {
                SDL_CaptureMouse(false);
            }

            OsQueuePut(OS_INPUT_FOCUS, s_appActive, 0, 0, 0);
            break;

        // SDL2's SDL_WINDOWEVENT_CLOSE, renamed. Same meaning: the window
        // manager is asking, and the engine decides.
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            OsQueuePut(OS_INPUT_CLOSE, 0, 0, 0, 0);
            break;

        default:
            break;
    }

    OsSdlApplyCursorState();
}

// Win32 hangs this off a SetTimer so the engine keeps ticking inside a modal
// message loop -- a native dialog or a window drag, both of which block
// GetMessage. SDL_PollEvent never blocks, so there is no modal loop to tick
// through and the proc is recorded but never invoked.
// OFFSET: 0x00869D90
void OsInputSetEventPollProc(void (*pollProc)()) {
    s_eventPollProc = pollProc;
}

int32_t OsInputGet(OSINPUT* id, int32_t* param0, int32_t* param1, int32_t* param2, int32_t* param3) {
    *id = static_cast<OSINPUT>(-1);

    SDL_Window* window = GxWindow();

    // Mouse mode first: OsSdlApplyCursorState reads
    // SDL_GetWindowRelativeMouseMode, so re-asserting afterwards would leave
    // the cursor a frame behind the mode.
    ApplyMouseMode(window);
    OsSdlApplyCursorState();
    ApplyTextInput(window);

    // Compared every call rather than driven off SDL_WINDOWEVENT_SIZE_CHANGED:
    // a Wayland scale change alters the drawable size without resizing the
    // window, so there is no resize event to hang this on.
    if (window && !(SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)) {
        int32_t width;
        int32_t height;
        GetPixelSize(window, &width, &height);

        if (width != s_defaultWindowRect.right || height != s_defaultWindowRect.bottom) {
            s_defaultWindowRect.left = 0;
            s_defaultWindowRect.top = 0;
            s_defaultWindowRect.right = width;
            s_defaultWindowRect.bottom = height;

            *id = OS_INPUT_SIZE;
            *param0 = width;
            *param1 = height;
            *param2 = 0;
            *param3 = 0;

            return 1;
        }
    }

    if (s_queueHead == s_queueTail) {
        OsGuiPollGxWindowState();
        OsPollJoysticks(s_appActive);

        SDL_Event event;

        while (s_queueHead == s_queueTail) {
            if (!SDL_PollEvent(&event)) {
                return 0;
            }

            if (event.type == SDL_EVENT_QUIT) {
                *id = OS_INPUT_SHUTDOWN;
                return 1;
            }

            OsWindowProc(window, event.type, 0, reinterpret_cast<intptr_t>(&event));
        }
    }

    OsQueueGet(id, param0, param1, param2, param3);

    return 1;
}

void OsInputSetMouseMode(OS_MOUSE_MODE mode) {
    if (mode >= OS_MOUSE_MODES) {
        SErrSetLastError(STORM_ERROR_INVALID_PARAMETER);
        return;
    }

    if (mode == s_osMouseMode) {
        return;
    }

    // SDL3 made relative mouse mode per-window, so this no longer calls SDL
    // directly: it records the mode and lets ApplyMouseMode project it onto
    // the live window. See the note there for why that indirection is not
    // optional.
    //
    // The shape below is the SDL2 original's, unchanged. OS_MOUSE_MODE has
    // exactly two values, so "not relative" is "normal" and the assignment is
    // a no-op in the one case the original left s_osMouseMode alone in.
    int32_t leavingRelative = s_osMouseMode == OS_MOUSE_MODE_RELATIVE;

    s_osMouseMode = mode == OS_MOUSE_MODE_RELATIVE ? OS_MOUSE_MODE_RELATIVE : OS_MOUSE_MODE_NORMAL;

    ApplyMouseMode(GxWindow());

    if (leavingRelative) {
        // The sub-pixel remainder belongs to the relative-motion stream and
        // would otherwise be applied to the next drag, long after the fact.
        s_relFracX = 0.0f;
        s_relFracY = 0.0f;
        RestoreMouse();
    }

    OsSdlApplyCursorState();
}

// OFFSET: 0x0086A070
void OsInputSetMouseSpeed(float speed) {
    float clamped = speed < 0.1f ? 0.1f : (speed >= 2.0f ? 2.0f : speed);
    s_mouseSpeed = static_cast<int32_t>(clamped * 10.0f);
}

void OsInputGetMousePosition(int32_t* x, int32_t* y) {
    int32_t mouseX;
    int32_t mouseY;

    if (s_osMouseMode == OS_MOUSE_MODE_RELATIVE) {
        mouseX = s_mousePos.x;
        mouseY = s_mousePos.y;
    } else {
        // SDL3 reports the pointer position as floats in window points.
        float pointX;
        float pointY;
        SDL_GetMouseState(&pointX, &pointY);

        mouseX = static_cast<int32_t>(pointX);
        mouseY = static_cast<int32_t>(pointY);
        ScalePoint(&mouseX, &mouseY, 1);
        SaveMouse(mouseX, mouseY);
    }

    if (x) {
        *x = mouseX;
    }

    if (y) {
        *y = mouseY;
    }
}

void OsInputSetMousePosition(int32_t x, int32_t y) {
    SaveMouse(x, y);
    WarpMouse(x, y);
}

// OFFSET: 0x0086A1C0
int32_t OsGetDefaultWindowRect(tagRECT* rect) {
    if (!rect) {
        SErrSetLastError(STORM_ERROR_INVALID_PARAMETER);
        return 0;
    }

    if (!s_defaultWindowRect.right || !s_defaultWindowRect.bottom) {
        SDL_Window* window = GxWindow();

        if (!window) {
            return 0;
        }

        int32_t width;
        int32_t height;
        GetPixelSize(window, &width, &height);

        s_defaultWindowRect.left = 0;
        s_defaultWindowRect.top = 0;
        s_defaultWindowRect.right = width;
        s_defaultWindowRect.bottom = height;
    }

    *rect = s_defaultWindowRect;

    return 1;
}

// The Win32 original is a WNDPROC. Here message is an SDL_EventType and lparam
// points at the SDL_Event; wparam is unused. OsInputGet above is the only
// caller, so the reinterpretation is contained.
// OFFSET: 0x0086A230
intptr_t OsWindowProc(void* window, uint32_t message, uintptr_t wparam, intptr_t lparam) {
    const SDL_Event& event = *reinterpret_cast<const SDL_Event*>(lparam);

    // SDL3 has no single SDL_WINDOWEVENT to switch on -- every window event is
    // its own type in a contiguous SDL_EVENT_WINDOW_FIRST..LAST block -- so
    // that case comes out of the switch and becomes a range test.
    if (message >= SDL_EVENT_WINDOW_FIRST && message <= SDL_EVENT_WINDOW_LAST) {
        HandleWindowEvent(event.window);
        return 0;
    }

    switch (message) {
        case SDL_EVENT_KEYMAP_CHANGED:
            OsQueuePut(OS_INPUT_IME, 0, 0, 0, 0);
            break;

        case SDL_EVENT_TEXT_INPUT:
            QueueText(event.text.text);
            break;

        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            KEY key;

            // SDL3 flattened SDL_KeyboardEvent::keysym into the event: .sym is
            // .key, .scancode is .scancode, .mod is .mod.
            if (ConvertKeyCode(event.key.key, &key)) {
                OsQueuePut(message == SDL_EVENT_KEY_DOWN ? OS_INPUT_KEY_DOWN : OS_INPUT_KEY_UP, key, 1, 0, 0);
            }

            break;
        }

        case SDL_EVENT_MOUSE_MOTION: {
            if (s_osMouseMode != OS_MOUSE_MODE_NORMAL) {
                // Fractional deltas accumulate instead of truncating. See
                // s_relFracX. Truncation is toward zero on both signs, so the
                // remainder keeps the sign of the motion and a direction
                // reversal does not stall.
                float relX = event.motion.xrel + s_relFracX;
                float relY = event.motion.yrel + s_relFracY;

                int32_t x = static_cast<int32_t>(relX);
                int32_t y = static_cast<int32_t>(relY);

                s_relFracX = relX - static_cast<float>(x);
                s_relFracY = relY - static_cast<float>(y);

                ScalePoint(&x, &y, 1, 1);

                if (x || y) {
                    OsQueuePut(OS_INPUT_MOUSE_MOVE_RELATIVE, 0, x, y, 0);
                }
            } else {
                int32_t x = static_cast<int32_t>(event.motion.x);
                int32_t y = static_cast<int32_t>(event.motion.y);

                ScalePoint(&x, &y, 1);

                if (NativeVisualHooks::mouseInput) {
                    NativeVisualHooks::mouseInput(0, 0, x, y);
                }

                OsQueuePut(OS_INPUT_MOUSE_MOVE, 0, x, y, 0);
                SaveMouse(x, y);
            }

            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            HandleMouseButton(event.button, OS_INPUT_MOUSE_DOWN);
            break;

        case SDL_EVENT_MOUSE_BUTTON_UP:
            HandleMouseButton(event.button, OS_INPUT_MOUSE_UP);
            break;

        case SDL_EVENT_MOUSE_WHEEL: {
            int32_t x;
            int32_t y;
            OsInputGetMousePosition(&x, &y);

            // The engine expects WHEEL_DELTA-scaled notches, as WM_MOUSEWHEEL
            // delivers them.
            //
            // SDL3's wheel y is a float, and SDL 3.2.12 added integer_y: the
            // same motion accumulated into whole detents. integer_y is what
            // this wants -- a trackpad's 0.3 of a notch would otherwise
            // truncate to nothing, and 120 * 0.3 is not a thing WM_MOUSEWHEEL
            // ever delivers.
            int32_t delta = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED
                ? -event.wheel.integer_y
                : event.wheel.integer_y;

            if (delta) {
                OsQueuePut(OS_INPUT_MOUSE_WHEEL, static_cast<int16_t>(delta * 120), x, y, 0);
            }

            break;
        }

        default:
            break;
    }

    return 0;
}

// Win32 uses this to block the low-level keyboard hook while a password field
// has focus. There is no equivalent global hook to disable.
void OsInputEnableSecureMode(int32_t enable) {
}

#endif  // !_WIN32
