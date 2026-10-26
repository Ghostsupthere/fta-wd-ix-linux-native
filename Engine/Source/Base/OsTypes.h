#ifndef ENGINE_SOURCE_BASE_OSTYPES_H
#define ENGINE_SOURCE_BASE_OSTYPES_H

#include "Engine/Source/Base/OsCrt.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <pthread.h>
#include <signal.h>
#if defined(__linux__)
#include <ucontext.h>
#elif defined(__APPLE__)
#include <sys/ucontext.h>
#endif

#ifndef WINAPI
#define WINAPI
#endif
#ifndef CALLBACK
#define CALLBACK
#endif
#ifndef __stdcall
#define __stdcall
#endif
#ifndef __cdecl
#define __cdecl
#endif

#ifndef MAX_PATH
#define MAX_PATH 260
#endif
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef INFINITE
#define INFINITE 0xFFFFFFFF
#endif
#ifndef WAIT_OBJECT_0
#define WAIT_OBJECT_0 0x00000000
#endif
#ifndef WAIT_TIMEOUT
#define WAIT_TIMEOUT 0x00000102
#endif
#ifndef WAIT_FAILED
#define WAIT_FAILED 0xFFFFFFFF
#endif
#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE (reinterpret_cast<HANDLE>(static_cast<intptr_t>(-1)))
#endif

// The Win32 types. In the one translation unit that talks to dxvk-native's D3D9
// frontend (CGxDeviceD3d9Ex, which defines WOWDECOMP_GX_DXVK before any include)
// dxvk's <windows.h> is the authoritative source -- its HWND/DWORD/POINT/RECT/
// ... are shape-compatible with the ones below but must not be redefined. Every
// other TU uses the definitions here and never sees dxvk's headers.
#ifdef WOWDECOMP_GX_DXVK
#include <windows.h>
#else
typedef void* HANDLE;
typedef int32_t BOOL;
typedef uint8_t BYTE;
typedef uint8_t UCHAR;
typedef uint16_t WORD;
typedef uint16_t USHORT;
typedef uint32_t DWORD;
typedef uint32_t ULONG;
typedef int32_t LONG;

struct HINSTANCE__;
typedef HINSTANCE__* HINSTANCE;

struct HWND__;
typedef HWND__* HWND;

union LARGE_INTEGER {
    struct {
        DWORD LowPart;
        LONG HighPart;
    };
    int64_t QuadPart;
};

extern "C" {

// Declared here rather than behind Os/Time.h because parts of the engine time
// with these directly, the way the Windows original does. Defined in
// Os/Linux/Time.cpp over CLOCK_MONOTONIC.
BOOL QueryPerformanceCounter(LARGE_INTEGER* counter);
BOOL QueryPerformanceFrequency(LARGE_INTEGER* frequency);

}  // extern "C"

struct SYSTEMTIME {
    WORD wYear;
    WORD wMonth;
    WORD wDayOfWeek;
    WORD wDay;
    WORD wHour;
    WORD wMinute;
    WORD wSecond;
    WORD wMilliseconds;
};

struct tagRECT {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
};
typedef tagRECT RECT;

struct tagPOINT {
    LONG x;
    LONG y;
};
typedef tagPOINT POINT;

typedef void* HDC;
#endif  // WOWDECOMP_GX_DXVK

// dxvk-native's <windows.h> does not define these, so they are declared for
// every TU regardless of which Win32 type source is in effect.
typedef uint16_t ATOM;
typedef pthread_mutex_t CRITICAL_SECTION;
typedef ucontext_t CONTEXT;

// The SDL window destructor (Os/Linux/Gui.cpp); forward-declared so the Win32
// DestroyWindow spelling can route to it without a Gx-layer include here.
void OsGuiDestroyGxWindow(void* window);

// Win32 window-management calls the shared D3D9 renderer makes. Off Windows the
// window is an SDL window owned by the Os layer: showing is implicit, there are
// no window classes or device contexts, and destruction goes through the Os
// layer. These keep CGxDeviceD3d9Ex compiling unchanged.
inline BOOL ShowWindow(HWND, int32_t) { return 1; }
inline BOOL DestroyWindow(HWND window) { if (window) { OsGuiDestroyGxWindow(window); } return 1; }
inline BOOL UnregisterClassA(const char*, void*) { return 1; }
inline HDC GetDC(HWND) { return nullptr; }
inline int32_t ReleaseDC(HWND, HDC) { return 1; }
inline BOOL GetDeviceGammaRamp(HDC, void*) { return 0; }
inline BOOL GetCursorPos(POINT* point) { if (point) { point->x = 0; point->y = 0; } return 1; }
inline BOOL ScreenToClient(HWND, POINT*) { return 1; }
inline BOOL GetClientRect(HWND, RECT* rect) { if (rect) { rect->left = rect->top = rect->right = rect->bottom = 0; } return 1; }
inline BOOL GetWindowRect(HWND, RECT* rect) { if (rect) { rect->left = rect->top = rect->right = rect->bottom = 0; } return 1; }
inline BOOL ClipCursor(const RECT*) { return 1; }
inline int32_t MapWindowPoints(HWND, HWND, POINT*, unsigned int) { return 0; }

// Win32 ShowWindow commands (the SDL ShowWindow shim ignores them) and the
// WORD-extraction macros the renderer uses.
#ifndef SW_HIDE
#define SW_HIDE 0
#endif
#ifndef SW_SHOWNORMAL
#define SW_SHOWNORMAL 1
#endif
#ifndef LOWORD
#define LOWORD(l) (static_cast<WORD>(static_cast<uintptr_t>(l) & 0xFFFF))
#endif
#ifndef HIWORD
#define HIWORD(l) (static_cast<WORD>((static_cast<uintptr_t>(l) >> 16) & 0xFFFF))
#endif

// Windows ANSI code page. Linux has no ANSI code page concept; the client only
// uses this to tag IME composition, and Windows-1252 is the engine's default.
inline unsigned int GetACP() {
    return 1252;
}

struct OVERLAPPED {
    uintptr_t Internal;
    uintptr_t InternalHigh;
    DWORD Offset;
    DWORD OffsetHigh;
    HANDLE hEvent;
};

struct EXCEPTION_POINTERS {
    siginfo_t* ExceptionRecord;
    CONTEXT* ContextRecord;
};

// Defined in Os/Linux/OsLibrary.cpp. Declared with void* (== HMODULE) so this
// header needs no handle-type include.
void* GetModuleHandleA(const char* name);
int32_t FreeLibrary(void* module);
void* LoadLibraryA(const char* name);
void* GetProcAddress(void* module, const char* name);

inline void OutputDebugStringA(const char* string) {
    fputs(string, stderr);
}

template <typename T, typename V>
inline T InterlockedExchange(volatile T* target, V value) {
    return __atomic_exchange_n(target, static_cast<T>(value), __ATOMIC_SEQ_CST);
}

template <typename T, typename V>
inline T InterlockedExchangeAdd(volatile T* target, V value) {
    return __atomic_fetch_add(target, static_cast<T>(value), __ATOMIC_SEQ_CST);
}

template <typename T>
inline T InterlockedIncrement(volatile T* target) {
    return __atomic_add_fetch(target, 1, __ATOMIC_SEQ_CST);
}

template <typename T>
inline T InterlockedDecrement(volatile T* target) {
    return __atomic_sub_fetch(target, 1, __ATOMIC_SEQ_CST);
}

template <typename T, typename V, typename C>
inline T InterlockedCompareExchange(volatile T* target, V exchange, C comparand) {
    T expected = static_cast<T>(comparand);
    __atomic_compare_exchange_n(target, &expected, static_cast<T>(exchange), false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return expected;
}

inline void* InterlockedCompareExchangePointer(void* volatile* target, void* exchange, void* comparand) {
    __atomic_compare_exchange_n(target, &comparand, exchange, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return comparand;
}
#endif

#endif
