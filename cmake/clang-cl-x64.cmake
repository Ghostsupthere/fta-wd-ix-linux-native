# Toolchain: clang-cl -> x86_64-pc-windows-msvc, against an xwin-provided
# Windows SDK + MSVC CRT.
#
# Pass -DXWIN_DIR=/abs/path/to/.xwin (the `splat --output` directory).
#
# The project's CMakeLists requires CMAKE_SIZEOF_VOID_P == 8 and an MSVC-ish
# frontend; clang-cl targeting x86_64 satisfies both (COMPILER_ID=Clang,
# COMPILER_FRONTEND_VARIANT=MSVC).

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

# try_compile() runs a throwaway sub-project that re-includes this file; without
# this the cache variable is invisible there and configuration dies during
# compiler ABI detection.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES XWIN_DIR CMAKE_MSVC_RUNTIME_LIBRARY)

# Static release CRT (libcmt.lib) — matches what the project forces for Release.
# xwin does not splat the debug CRT, so msvcrtd.lib/libcmtd.lib do not exist here.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

# Compiler detection otherwise links a test .exe with CMake's default debug
# flags (-MDd), which needs the debug CRT we don't have. Building a static
# library instead exercises the compiler without involving the linker.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

if(NOT DEFINED XWIN_DIR)
  message(FATAL_ERROR "XWIN_DIR is not set. Configure with -DXWIN_DIR=<path to .xwin>")
endif()
if(NOT EXISTS "${XWIN_DIR}/crt/include")
  message(FATAL_ERROR "XWIN_DIR='${XWIN_DIR}' has no crt/include. Run xwin-fetch first.")
endif()

# Honour the wrappers exported by shell.nix, but stay usable standalone.
find_program(CLANG_CL_BIN NAMES clang-cl REQUIRED)
find_program(LLD_LINK_BIN NAMES lld-link REQUIRED)
find_program(LLVM_LIB_BIN NAMES llvm-lib REQUIRED)
find_program(LLVM_RC_BIN  NAMES llvm-rc  REQUIRED)

set(CMAKE_C_COMPILER   "${CLANG_CL_BIN}")
set(CMAKE_CXX_COMPILER "${CLANG_CL_BIN}")
set(CMAKE_LINKER       "${LLD_LINK_BIN}")
set(CMAKE_AR           "${LLVM_LIB_BIN}")
set(CMAKE_RC_COMPILER  "${LLVM_RC_BIN}")

# clang-cl needs an explicit target triple; CMake won't infer 32-bit for us.
set(_tgt "--target=x86_64-pc-windows-msvc")

# clang's own resource headers must come FIRST.
#
# The MSVC CRT declares the SSE intrinsics as plain extern functions
# (extern __m128 _mm_add_ps(...)) because MSVC implements them in the
# compiler. If that emmintrin.h wins, clang emits real calls to them and
# the link fails with undefined _mm_* symbols. Clang's own copy defines
# them inline, so putting its resource include directory ahead of the
# SDK's fixes it.
if(DEFINED ENV{CLANG_RESOURCE_INCLUDE})
  set(_clang_resource_include "$ENV{CLANG_RESOURCE_INCLUDE}")
else()
  execute_process(
    COMMAND "${CLANG_CL_BIN}" -print-resource-dir
    OUTPUT_VARIABLE _clang_resource_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  set(_clang_resource_include "${_clang_resource_dir}/include")
endif()
if(NOT EXISTS "${_clang_resource_include}/emmintrin.h")
  message(FATAL_ERROR
    "clang resource headers not found at ${_clang_resource_include}. "
    "Without them the MSVC CRT's declaration-only intrinsics win and "
    "the link fails on undefined _mm_* symbols.")
endif()
# /imsvc marks these as *system* includes so the SDK's own warnings stay quiet.
set(_inc
  "/imsvc${_clang_resource_include}"
  "/imsvc${XWIN_DIR}/crt/include"
  "/imsvc${XWIN_DIR}/sdk/include/ucrt"
  "/imsvc${XWIN_DIR}/sdk/include/um"
  "/imsvc${XWIN_DIR}/sdk/include/shared"
  "/imsvc${XWIN_DIR}/sdk/include/winrt"
)
list(JOIN _inc " " _inc_flags)

# -vctoolsversion/-winsdkversion don't apply to the xwin layout, so the search
# paths are given explicitly instead.
set(_libpaths
  "-libpath:${XWIN_DIR}/crt/lib/x86_64"
  "-libpath:${XWIN_DIR}/sdk/lib/ucrt/x86_64"
  "-libpath:${XWIN_DIR}/sdk/lib/um/x86_64"
)
list(JOIN _libpaths " " _libpath_flags)

# MSVC accepts constructs clang rejects outright. These are cross-build
# accommodations, not source bugs -- upstream builds clean under MSVC v143:
#
#   register   upstream compiles the bundled libjpeg .c files as C++ via
#              set_source_files_properties(LANGUAGE CXX), and MSVC still
#              tolerates the storage class C++17 removed.
#   narrowing  MSVC warns where clang errors on narrowing conversions in
#              initializer lists, which decompiled code is full of.
#   offsetof   the UCRT defines offsetof with a reinterpret_cast, which is not
#              a constant expression -- MSVC special-cases its own. The header
#              offers this switch to use __builtin_offsetof instead, which is.
#              Needed because the engine uses offsetof as a template argument
#              (TSExplicitList<T, offsetof(T, m_link)>), which is pervasive.
set(_compat "-Wno-register -Wno-c++11-narrowing -D_CRT_USE_BUILTIN_OFFSETOF")

set(CMAKE_C_FLAGS_INIT   "${_tgt} ${_inc_flags} ${_compat}")
set(CMAKE_CXX_FLAGS_INIT "${_tgt} ${_inc_flags} ${_compat}")
set(CMAKE_RC_FLAGS_INIT  "-I ${XWIN_DIR}/sdk/include/um -I ${XWIN_DIR}/sdk/include/shared")

set(CMAKE_EXE_LINKER_FLAGS_INIT    "${_libpath_flags} -machine:x64")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_libpath_flags} -machine:x64")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_libpath_flags} -machine:x64")

# Only look for libraries/headers in the target sysroot, never the host.
set(CMAKE_FIND_ROOT_PATH "${XWIN_DIR}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
