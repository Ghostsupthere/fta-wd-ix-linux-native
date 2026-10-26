# dxvk-native D3D9 for the native Linux build (PR1).
#
# Unlike cmake/DxvkNative.cmake (the D3D11 backend, which LINKS libdxvk_d3d11.so
# directly), the stock D3D9 renderer loads its frontend DYNAMICALLY -- upstream
# CGxDeviceD3d9Ex does LoadLibraryA("d3d9.dll") + GetProcAddress("Direct3DCreate9").
# On Linux that LoadLibraryA is our dlopen shim, which maps "d3d9.dll" ->
# "libdxvk_d3d9.so" (Engine/Source/Os/Linux/OsLibrary.cpp). So there is nothing
# to link here: we only need the dxvk-native DirectX headers so d3d9.h compiles,
# and the build directory on the runtime search path so the dlopen resolves.
#
# ./fetch-dxvk.sh populates .deps/dxvk and builds build-native-sdl3; override
# with -DDXVK_SRC / DXVK_NATIVE_DIR to point at an existing tree.

set(DXVK_SRC "${CMAKE_SOURCE_DIR}/.deps/dxvk" CACHE PATH "DXVK source tree (populated by ./fetch-dxvk.sh)")
set(DXVK_NATIVE_DIR "$ENV{DXVK_NATIVE_DIR}" CACHE PATH "dxvk-native meson build directory")
if (NOT DXVK_NATIVE_DIR)
    set(DXVK_NATIVE_DIR "${DXVK_SRC}/build-native-sdl3" CACHE PATH "dxvk-native meson build directory" FORCE)
endif ()

set(_dxvk_d3d9_inc
    "${DXVK_SRC}/include"
    "${DXVK_SRC}/include/native"
    "${DXVK_SRC}/include/native/windows"
    "${DXVK_SRC}/include/native/directx"
)

foreach (_dir ${_dxvk_d3d9_inc})
    if (NOT IS_DIRECTORY "${_dir}")
        message(FATAL_ERROR
            "dxvk-native headers not found: ${_dir} is missing.\n"
            "Run ./fetch-dxvk.sh, or set -DDXVK_SRC=<path to a dxvk source tree>.")
    endif ()
endforeach ()

if (NOT EXISTS "${DXVK_NATIVE_DIR}/src/d3d9/libdxvk_d3d9.so")
    message(FATAL_ERROR
        "libdxvk_d3d9.so not found under ${DXVK_NATIVE_DIR}.\n"
        "Run ./fetch-dxvk.sh (it builds build-native-sdl3), or set DXVK_NATIVE_DIR.")
endif ()

# An INTERFACE target the Gx backend pulls the headers from; the .so is resolved
# at runtime via dlopen, so it is on the rpath, not a link input.
add_library(dxvk_d3d9_headers INTERFACE)
target_include_directories(dxvk_d3d9_headers INTERFACE ${_dxvk_d3d9_inc})

# Runtime search path for the dlopen. Added to executables that render.
set(DXVK_D3D9_RPATH "${DXVK_NATIVE_DIR}/src/d3d9" CACHE INTERNAL "rpath dir holding libdxvk_d3d9.so")
