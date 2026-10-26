#if defined(WOWDECOMP_ARCH_64)

#include <cstdint>
#ifdef WOWDECOMP_LINUX
#include "Engine/Source/Base/OsTypes.h"
#else
#include <windows.h>
#endif
#include "Engine/Source/Os/Tls.h"

// OFFSET: 0x0086D6B0
uint32_t OsTlsAlloc() {
    return TlsAlloc();
}

// OFFSET: 0x0086D6C0
void OsTlsFree(uint32_t index) {
    TlsFree(index);
}

// OFFSET: 0x0086D6D0
void* OsTlsGetValue(uint32_t index) {
    return TlsGetValue(index);
}

// OFFSET: 0x0086D6E0
int32_t OsTlsSetValue(uint32_t index, void* value) {
    return TlsSetValue(index, value);
}

#endif
