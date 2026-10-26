# Everything the native Linux build needs that the Windows build does not.
#
# Included from the root CMakeLists in the `else()` branch of `if (WIN32)`,
# BEFORE the per-module add_subdirectory() calls, so the options and targets it
# establishes are visible to every module's CMakeLists.
#
# This follows the rosemyne Linux model: a NATIVE Os layer, no Win32-API shim,
# no force-included compatibility header. The MSVC-only spellings the decompiled
# tree uses (__stdcall, __int64, _alloca) are neutralised as preprocessor
# definitions; everything else is real POSIX/SDL code under Engine/Source/*/Linux.
#
# PR1 renders through the stock CGxDeviceD3d9Ex on dxvk-native's libdxvk_d3d9.so
# (loaded dynamically; see cmake/DxvkD3d9.cmake and OsLibrary's d3d9.dll map).
# The native-Vulkan backend rosemyne ships is deliberately OFF here.

# --- backend selection -------------------------------------------------------
# D3D9-on-dxvk is PR1's renderer; the Vulkan Gx backend is not built. The Gx and
# ThirdParty CMakeLists gate the whole Vulkan/volk tree on this option.
set(WOWDECOMP_ENABLE_VULKAN OFF CACHE BOOL "Build the Vulkan Gx backend" FORCE)
# Only meaningful for the Windows-on-ARM cross build; the Gx source filter reads
# it, so it must have a defined (false) value on Linux.
set(WOWDECOMP_WIN_ARM64 OFF)

# --- D3D9, from dxvk-native --------------------------------------------------
include(${CMAKE_SOURCE_DIR}/cmake/DxvkD3d9.cmake)

# --- system libraries --------------------------------------------------------
find_package(PkgConfig REQUIRED)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# SDL3 owns the window, the input and dxvk-native's WSI (built with
# -Dnative_sdl3=enabled), so there is exactly one window system in the process.
# pkg-config rather than find_package: the Nix derivation ships sdl3.pc
# unconditionally, while the CMake config package depends on the version.
pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)

# The rosemyne per-module CMakeLists link the SDL window system as SDL2::SDL2.
# PR1 targets SDL3 directly, so alias that name onto the SDL3 imported target:
# the module CMakeLists stay untouched and the source is ported to the SDL3 API.
if (NOT TARGET SDL2::SDL2)
    add_library(SDL2::SDL2 ALIAS PkgConfig::SDL3)
endif ()

# Vorbis for BlizzardCore/OggDecompress.cpp (system library off Windows; the
# vendored FMOD Ex codec does not build here).
pkg_check_modules(VORBISFILE REQUIRED IMPORTED_TARGET vorbisfile)

# --- definitions -------------------------------------------------------------
# The MSVC spellings that have to compile as C as well as C++. cmake/Platform.cmake
# sets the WOWDECOMP_* CMake variables; these are the matching preprocessor defs.
add_compile_definitions(
    WOWDECOMP_LINUX=1
    WOWDECOMP_UNIX=1
    __cdecl=
    __stdcall=
    __fastcall=
    __thiscall=
    "__int64=long long"
    _alloca=__builtin_alloca
)

# --- warnings that are MSVC-vs-GCC differences, not bugs ---------------------
add_compile_options(
    -Wno-multichar
    -Wno-format-security
    "$<$<COMPILE_LANGUAGE:CXX>:-Wno-invalid-offsetof;-Wno-register>"
)
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-Wno-changes-meaning>")
endif ()
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-Wno-c++11-narrowing>")
endif ()

# Decompiled code type-puns constantly; strict aliasing would let the optimiser
# act on assumptions the original VS2005 build never made.
# MSVC-lenient constructs in the decompiled tree that GCC rejects by default:
#   -fpermissive         out-of-range enumerator values (MSVC truncates; GCC errors)
#   -Wno-template-body   name shadowing inside template bodies (GCC 14+ diagnostic)
# Linux/GCC only; the Windows/MSVC build never sees these.
add_compile_options(-fpermissive -Wno-template-body)
add_compile_options(-fno-strict-aliasing)

# --- include paths -----------------------------------------------------------
# The tree root, so repository-relative includes ("Engine/Source/...") resolve.
#
# dxvk-native's DirectX headers are deliberately NOT global: dxvk-native's
# <windows.h> types (HWND = void*) conflict with the engine's own Win32 types
# from Engine/Source/Base/OsTypes.h (HWND = struct HWND__*), which every module
# sees. The DirectX headers are scoped to the Gx target alone (the only place
# d3d9.h is needed -- CGxDeviceD3d9Ex) via the dxvk_d3d9_headers interface
# target defined in cmake/DxvkD3d9.cmake. See Engine/Source/Gx/CMakeLists.txt.
include_directories(${CMAKE_SOURCE_DIR})

# --- linking -----------------------------------------------------------------
# The static libraries in this tree are mutually recursive (Gx calls Storm calls
# Base calls Gx); GNU ld resolves left to right in one pass, so wrap the whole
# set in --start-group/--end-group rather than hand-topologise 40 libraries.
set(CMAKE_CXX_LINK_GROUP_USING_wowdecomp_circular "LINKER:--start-group" "LINKER:--end-group")
set(CMAKE_CXX_LINK_GROUP_USING_wowdecomp_circular_SUPPORTED TRUE)
set(CMAKE_C_LINK_GROUP_USING_wowdecomp_circular "LINKER:--start-group" "LINKER:--end-group")
set(CMAKE_C_LINK_GROUP_USING_wowdecomp_circular_SUPPORTED TRUE)
