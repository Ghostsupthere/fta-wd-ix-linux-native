cmake_minimum_required(VERSION 3.13)

if (${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
    message(FATAL_ERROR
        "In-source builds not allowed.
        Please make a new directory (called a build directory) and run CMake from there.
        You may need to remove CMakeCache.txt."
    )
endif ()

project(wowdecomp CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_SOURCE_DIR}/cmake/Platform.cmake)

# Two supported targets. Windows x64 is the one that ships (cross-compiled with
# clang-cl + xwin by ./build.sh); native Linux is the port in progress
# (./build-native-cmake.sh). Everything else is unported.
if (NOT CMAKE_SYSTEM_NAME MATCHES "Windows" AND NOT WOWDECOMP_LINUX)
    message(FATAL_ERROR "wowdecomp targets Windows and Linux.")
endif ()

# x64 is the target. Win32 stays buildable: it is the configuration whose struct layouts can be
# checked against Wow.exe 3.3.5a (12340).
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(WOWDECOMP_ARCH_64 1)
    add_compile_definitions(WOWDECOMP_ARCH_64=1)
elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(WOWDECOMP_ARCH_32 1)
    add_compile_definitions(WOWDECOMP_ARCH_32=1)
    # x87 floating point like the VS2005 original; keeps Win32 code comparable with the binary (re/tools/verify.py).
    if (MSVC)
        add_compile_options(/arch:IA32 /fp:fast)

    endif ()
    # 32-bit time_t like the original build: time/localtime/mktime resolve to _time32/_localtime32/_mktime32.
    add_compile_definitions(_USE_32BIT_TIME_T)
else ()
    message(FATAL_ERROR "Unsupported pointer size.")
endif ()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if (WIN32)
    add_compile_definitions(
        WIN32
        _WINDOWS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
    )

    # <dirent.h> and <unistd.h> for the Windows build only.
    #
    # The TerrainLod family scans its cache with opendir/readdir/closedir and
    # names a temp file with getpid(); it was written against the native Linux
    # build and four of its translation units simply do not compile for
    # Windows without these. See Engine/Source/Os/Win32/posix/dirent.h for the
    # whole story, including why OsFileList is the proper fix and why it is not
    # being done here.
    #
    # LAST on the include path, and only under WIN32, so it can never shadow a
    # real platform header.
    include_directories(AFTER SYSTEM ${CMAKE_SOURCE_DIR}/Engine/Source/Os/Win32/posix)
else ()
    include(${CMAKE_SOURCE_DIR}/cmake/LinuxNative.cmake)
endif ()

# Off by default: the client checks MPQ file MD5s (signaturefile) and GlueXML/FrameXML/AddOn .toc.sig files like the
# original. ON accepts unsigned or modified game data and interface files.
option(WOWDECOMP_DISABLE_SIGNATURE_CHECKS "Skip MPQ file signature and .toc signature checks" OFF)
if (WOWDECOMP_DISABLE_SIGNATURE_CHECKS)
    add_compile_definitions(DISABLE_SIGNATURE_CHECKS=1)
endif ()

# Game identity (build 12340, version 3.3.5, client name) lives in GameVersion.h at the repository root.
# WaystoneVersion.h is regenerated from git on every build; the login screen shows its build number.
set(WAYSTONE_VERSION_HEADER ${CMAKE_BINARY_DIR}/generated/WaystoneVersion.h)
add_custom_target(WaystoneVersion
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DOUTPUT=${WAYSTONE_VERSION_HEADER} -P ${CMAKE_SOURCE_DIR}/cmake/GenerateWaystoneVersion.cmake
    BYPRODUCTS ${WAYSTONE_VERSION_HEADER}
    VERBATIM
)
include_directories(${CMAKE_BINARY_DIR}/generated)

# Third-party code is linked, not decompiled, for now.
if (EXISTS ${CMAKE_SOURCE_DIR}/ThirdParty/CMakeLists.txt)
    add_subdirectory(ThirdParty)
endif ()

add_subdirectory(Engine/Source/BLPFile)
add_subdirectory(Engine/Source/Base)
add_subdirectory(Engine/Source/BlizzardCore)
add_subdirectory(Engine/Source/ComSat)
add_subdirectory(Engine/Source/ConsoleBase)
add_subdirectory(Engine/Source/Event)
add_subdirectory(Engine/Source/FFX)
add_subdirectory(Engine/Source/Frame)
add_subdirectory(Engine/Source/FrameScript)
add_subdirectory(Engine/Source/FrameXML)
add_subdirectory(Engine/Source/Gx)
add_subdirectory(Engine/Source/Gxu)
add_subdirectory(Engine/Source/Images)
add_subdirectory(Engine/Source/jpeg)
add_subdirectory(Engine/Source/lua-5.1)
add_subdirectory(Engine/Source/Model2)
add_subdirectory(Engine/Source/Os)
add_subdirectory(Engine/Source/pcre)
add_subdirectory(Engine/Source/Scrn)
add_subdirectory(Engine/Source/Services)
add_subdirectory(Engine/Source/ShaderEffect)
# Both platforms. The vendored FMOD Ex builds natively against ALSA; its own
# CMakeLists carries the WIN32/else split.
add_subdirectory(Engine/Source/SoundEngine)
add_subdirectory(Engine/Source/SteelseriesMouse)
add_subdirectory(Engine/Source/Storm)
add_subdirectory(Engine/Source/Tempest)
add_subdirectory(WoW/Common)
add_subdirectory(WoW/Source/Battlenet)
add_subdirectory(WoW/Source/Console)
add_subdirectory(WoW/Source/ConsoleDevice)
add_subdirectory(WoW/Source/DB)
add_subdirectory(WoW/Source/Game)
add_subdirectory(WoW/Source/Glue)
add_subdirectory(WoW/Source/Magic)
add_subdirectory(WoW/Source/LiquidSystem)
add_subdirectory(WoW/Source/Minigame)
add_subdirectory(WoW/Source/Net)
add_subdirectory(WoW/Source/Object)
add_subdirectory(WoW/Source/ObjectAlloc)
add_subdirectory(WoW/Source/ObjectMgrClient)
add_subdirectory(WoW/Source/SoundInterface)
add_subdirectory(WoW/Source/UIUtil)
add_subdirectory(WoW/Source/Ui)
add_subdirectory(WoW/Source/WorldClient)
add_subdirectory(WoW/Source/WowServices)
add_subdirectory(WoW/Source/WowSvcs)
add_subdirectory(WoW/Source)
# The client executable. In the rosemyne model this is built on every platform
# (its entry point is WoW/Source/<platform>/WinMain.cpp); on Linux it links the
# stock D3D9 renderer running on dxvk-native.
add_subdirectory(Tools/WowClient)
# On Windows this wraps ThirdParty/xvidcore/lib/x64/libxvidcore.lib, a Windows
# import library, and ships as a side-by-side DLL that the client resolves at
# load time.
#
# On Linux there is no such import library -- ThirdParty/xvidcore carries only
# headers and Windows binaries -- but XvidDecoder.cpp itself is portable: its
# only dependency is <xvid.h>, and the system xvidcore provides the same API.
# So it is built as a static library and linked straight into the executable
# rather than dlopen'd, which is simpler than reproducing the DLL dance.
# On Linux XvidDecoder is built as a static library by ThirdParty/CMakeLists
# (from the system xvidcore) and linked straight in; only the Windows DLL dance
# needs the Tools/ subdirectory.
if ((WIN32 AND WOWDECOMP_ARCH_64) OR WOWDECOMP_LINUX)
    add_subdirectory(Tools/XvidDecoder)
endif ()

add_dependencies(Glue WaystoneVersion)
add_dependencies(Ui WaystoneVersion)

# --- platform source filtering, applied after the fact ------------------------
#
# Almost every CMakeLists in this tree does file(GLOB_RECURSE "*.cpp"), which
# on Linux would pick up Os/Win32, Os/Win64, SteelseriesMouse/Win32 and the
# rest. Those directories are selected by path, and the files inside them
# carry their own `#if defined(WOWDECOMP_ARCH_32|_64)` guards -- which is
# exactly why Os/Win64/*.cpp WOULD compile on a 64-bit Linux build if left in.
#
# Done here, over the finished targets, rather than by editing forty
# CMakeLists: target SOURCES is writable from any scope, file(GLOB) returns
# absolute paths, and re-filtering an already-filtered list is a no-op. That
# keeps this mechanism in one place and keeps it from fighting the per-directory
# files, several of which are owned by other work in progress.
#
# It runs on Windows too, and has to: Engine/Source/Os/Linux/*.cpp would
# otherwise be compiled into the shipping PE, which happens to survive today
# only because those files guard themselves with #ifndef _WIN32. That is luck,
# not design.
function(wowdecomp_strip_platform_sources dir)
    get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach (_target ${_targets})
        get_target_property(_type ${_target} TYPE)
        if (NOT _type STREQUAL "INTERFACE_LIBRARY" AND NOT _type STREQUAL "UTILITY")
            get_target_property(_srcs ${_target} SOURCES)
            if (_srcs)
                wowdecomp_filter_platform_sources(_srcs)
                set_property(TARGET ${_target} PROPERTY SOURCES ${_srcs})
            endif ()
        endif ()
    endforeach ()
    get_property(_subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach (_subdir ${_subdirs})
        wowdecomp_strip_platform_sources("${_subdir}")
    endforeach ()
endfunction ()

wowdecomp_strip_platform_sources("${CMAKE_SOURCE_DIR}")

# The icon and version resource. A Windows .rc needs the RC compiler, which
# only the cross-build has.
if (WIN32)
    foreach (target Wow WowClient MapViewer M2Viewer GxTest MpqTest BlpTest DbcTest AssetCheck)
        if (TARGET ${target})
            target_sources(${target} PRIVATE ${CMAKE_SOURCE_DIR}/waystone.rc)
        endif ()
    endforeach ()
endif ()
