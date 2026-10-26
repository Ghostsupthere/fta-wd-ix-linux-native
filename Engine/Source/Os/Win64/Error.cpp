#if defined(WOWDECOMP_ARCH_64)

#ifdef WOWDECOMP_LINUX
#include "Engine/Source/Base/OsTypes.h"
#else
#include <windows.h>
#endif
#include "Engine/Source/Os/Error.h"

// OFFSET: 0x008714E0
char* OsGetLastErrorStr() {
    char* buffer;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<char*>(&buffer), 0, nullptr);
    return buffer;
}

// OFFSET: 0x00871510
void OsFreeLastErrorStr(char* str) {
    LocalFree(str);
}

#endif
