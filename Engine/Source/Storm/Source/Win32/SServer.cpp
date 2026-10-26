#if defined(WOWDECOMP_ARCH_32)

#ifdef WOWDECOMP_LINUX
#include "Engine/Source/Base/OsTypes.h"
#else
#include <windows.h>
#endif
#include "Engine/Source/Storm/H/storm.h"

// STATIC: 0x00CB74F0
static BOOL (WINAPI* s_tryEnterPtr)(LPCRITICAL_SECTION);

// STATIC: 0x00CB74F4
static HMODULE s_kernel32lib;

// VERIFY: 8f8a605b auto
// OFFSET: 0x0077E9A0
void SServerInitialize() {
    if (!s_tryEnterPtr) {
        s_kernel32lib = LoadLibraryA("kernel32.dll");

        if (s_kernel32lib) {
            s_tryEnterPtr = reinterpret_cast<BOOL (WINAPI*)(LPCRITICAL_SECTION)>(GetProcAddress(s_kernel32lib, "TryEnterCriticalSection"));
        }
    }
}

// VERIFY: 55c3ab9e auto
// OFFSET: 0x0077E9D0
void SServerDestroy() {
    if (s_kernel32lib) {
        FreeLibrary(s_kernel32lib);
    }
}

#endif
