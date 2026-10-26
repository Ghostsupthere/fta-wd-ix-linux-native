#include <cstdint>
#include <cstring>
#include <string>
#include <dlfcn.h>
#include "Engine/Source/Os/OsLibrary.h"

void* OsLoadLibrary(const char* fileName) {
    if (!fileName || !std::strchr(fileName, '\\')) {
        return dlopen(fileName, RTLD_NOW | RTLD_LOCAL);
    }

    std::string path(fileName);
    for (char& c : path) {
        if (c == '\\') {
            c = '/';
        }
    }

    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
}

void OsFreeLibrary(void* module) {
    dlclose(module);
}

// Win32 spelling used by the shared renderer (CGxDeviceD3d9Ex frees the dynamic
// d3d9 library this way). Returns non-zero on success like the Win32 API.
int32_t FreeLibrary(void* module) {
    if (module) {
        dlclose(module);
    }
    return 1;
}

void* OsGetLibraryEntryPoint(void* module, const char* name) {
    if (reinterpret_cast<uintptr_t>(name) < 0x10000) {
        return nullptr;
    }
    return dlsym(module, name);
}

// --- Win32 dynamic-loading spellings used by the shared renderer -------------
// CGxDeviceD3d9Ex loads its D3D9 frontend dynamically with LoadLibraryA +
// GetProcAddress. On Linux the DirectX "DLL" names map to dxvk-native's shared
// objects so that LoadLibraryA("d3d9.dll") resolves to libdxvk_d3d9.so.
void* LoadLibraryA(const char* name) {
    std::string lib(name ? name : "");
    if (lib == "d3d9.dll") {
        lib = "libdxvk_d3d9.so";
    } else if (lib == "d3d11.dll") {
        lib = "libdxvk_d3d11.so";
    } else if (lib == "dxgi.dll") {
        lib = "libdxvk_dxgi.so";
    }
    return dlopen(lib.c_str(), RTLD_NOW | RTLD_LOCAL);
}

void* GetProcAddress(void* module, const char* name) {
    return dlsym(module, name);
}

// Win32 GetModuleHandleA(NULL) returns the current process's module handle;
// dlopen(NULL) yields a handle to the main program's symbol scope. Named
// modules are only ever queried (RTLD_NOLOAD), never loaded, matching Win32.
void* GetModuleHandleA(const char* name) {
    return dlopen(name, RTLD_NOW | RTLD_NOLOAD);
}
