#if defined(WOWDECOMP_ARCH_64)

#ifdef WOWDECOMP_LINUX
#include "Engine/Source/Base/OsTypes.h"
#else
#include <windows.h>
#endif
#include "Engine/Source/Os/OsLibrary.h"

// OFFSET: 0x0086C4E0
void* OsLoadLibrary(const char* fileName) {
    return LoadLibraryA(fileName);
}

// OFFSET: 0x0086C4F0
void OsFreeLibrary(void* module) {
    FreeLibrary(static_cast<HMODULE>(module));
}

// OFFSET: 0x0086C500
void* OsGetLibraryEntryPoint(void* module, const char* name) {
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(module), name));
}

#endif
