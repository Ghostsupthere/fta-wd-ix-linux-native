# dxvk-native: the D3D11 implementation the native Linux build renders through.
#
# On Windows the client gets D3D11 from the OS (or from a drop-in DXVK DLL) and
# resolves D3D11CreateDevice with LoadLibraryA + GetProcAddress. Natively there
# is no d3d11.dll to load, so libdxvk_d3d11.so is linked directly --
# D3D11CreateDevice and D3D11CreateDeviceAndSwapChain are real exported ELF
# symbols.
#
# dxvk-native is a meson project and is built out of tree, deliberately kept
# separate from the Windows static build under dxvk-build/:
#
#   nix-shell -p meson ninja pkg-config gcc sdl3 vulkan-headers vulkan-loader glslang python3 \
#     --run 'meson setup build-native-sdl3 --buildtype release -Dnative_sdl3=enabled \
#            -Dnative_sdl2=disabled -Dnative_glfw=disabled && ninja -C build-native-sdl3'
#
# This file defines two imported targets:
#
#   dxvk::d3d11   libdxvk_d3d11.so  + the COM/DirectX headers
#   dxvk::dxgi    libdxvk_dxgi.so
#
# Both carry dxvk-native's include/native headers as INTERFACE includes. Those
# headers own the Windows types on Linux (HANDLE, HWND, DWORD, LARGE_INTEGER
# ...) because d3d11.h is built on their definitions; Base/LinuxCompat.h only
# supplements what they leave out.

set(DXVK_SRC "${CMAKE_SOURCE_DIR}/.deps/dxvk" CACHE PATH "DXVK source tree (populated by ./fetch-dxvk.sh)")
set(DXVK_NATIVE_DIR "$ENV{DXVK_NATIVE_DIR}" CACHE PATH "dxvk-native meson build directory")
if (NOT DXVK_NATIVE_DIR)
    set(DXVK_NATIVE_DIR "${DXVK_SRC}/build-native-sdl3" CACHE PATH "dxvk-native meson build directory" FORCE)
endif ()

set(_dxvk_native_inc
    "${DXVK_SRC}/include"
    "${DXVK_SRC}/include/native"
    "${DXVK_SRC}/include/native/windows"
    "${DXVK_SRC}/include/native/directx"
)

foreach (_dir ${_dxvk_native_inc})
    if (NOT IS_DIRECTORY "${_dir}")
        message(FATAL_ERROR
            "dxvk-native headers not found: ${_dir} is missing.\n"
            "Set -DDXVK_SRC=<path to the dxvk source tree>.")
    endif ()
endforeach ()

# The SONAME-versioned file, not the unversioned development symlink. Both
# record libdxvk_d3d11.so.0 as DT_NEEDED because the SONAME is set, but naming
# it explicitly is what the backend's linkage was verified against and leaves
# nothing to infer.
#
# Deliberately absent: libdxvk_d3d9.so. The backend compiled out its one D3D9
# reference (a no-op D3DPERF_SetOptions) precisely so a second DirectX frontend
# does not get dragged in; linking it here would undo that.
find_file(DXVK_NATIVE_D3D11_LIB
    NAMES libdxvk_d3d11.so.0 libdxvk_d3d11.so
    PATHS "${DXVK_NATIVE_DIR}/src/d3d11"
    NO_DEFAULT_PATH)
find_file(DXVK_NATIVE_DXGI_LIB
    NAMES libdxvk_dxgi.so.0 libdxvk_dxgi.so
    PATHS "${DXVK_NATIVE_DIR}/src/dxgi"
    NO_DEFAULT_PATH)

if (NOT DXVK_NATIVE_D3D11_LIB OR NOT DXVK_NATIVE_DXGI_LIB)
    message(FATAL_ERROR
        "dxvk-native libraries not found under ${DXVK_NATIVE_DIR}.\n"
        "  libdxvk_d3d11.so: ${DXVK_NATIVE_D3D11_LIB}\n"
        "  libdxvk_dxgi.so:  ${DXVK_NATIVE_DXGI_LIB}\n"
        "Build dxvk-native (see the header of cmake/DxvkNative.cmake) or pass "
        "-DDXVK_NATIVE_DIR=<meson build dir>.")
endif ()

add_library(dxvk::d3d11 SHARED IMPORTED GLOBAL)
set_target_properties(dxvk::d3d11 PROPERTIES
    IMPORTED_LOCATION "${DXVK_NATIVE_D3D11_LIB}"
    IMPORTED_NO_SONAME FALSE
    INTERFACE_INCLUDE_DIRECTORIES "${_dxvk_native_inc}"
)

add_library(dxvk::dxgi SHARED IMPORTED GLOBAL)
set_target_properties(dxvk::dxgi PROPERTIES
    IMPORTED_LOCATION "${DXVK_NATIVE_DXGI_LIB}"
    IMPORTED_NO_SONAME FALSE
    INTERFACE_INCLUDE_DIRECTORIES "${_dxvk_native_inc}"
)

# The .so files live in the meson build tree rather than a prefix, so the
# executable needs them on its RPATH or it will not start. Recorded as an
# absolute path on purpose: this is a development build, not something
# installed.
get_filename_component(_dxvk_d3d11_dir "${DXVK_NATIVE_D3D11_LIB}" DIRECTORY)
get_filename_component(_dxvk_dxgi_dir  "${DXVK_NATIVE_DXGI_LIB}"  DIRECTORY)
set(DXVK_NATIVE_RPATH "${_dxvk_d3d11_dir};${_dxvk_dxgi_dir}")

# Not a link-time dependency and must not become one: DXVK dlopens
# libvulkan.so.1. If it is not findable at runtime the failure is
# "vkGetInstanceProcAddr not found" followed by a segfault, which reads like a
# broken build rather than a missing LD_LIBRARY_PATH. shell-native.nix puts the
# loader on the path. --allow-shlib-undefined on the executable exists for the
# same reason.

message(STATUS "dxvk-native: ${DXVK_NATIVE_DIR}")
message(STATUS "dxvk-native: ${DXVK_NATIVE_D3D11_LIB}")
message(STATUS "dxvk-native: ${DXVK_NATIVE_DXGI_LIB}")
