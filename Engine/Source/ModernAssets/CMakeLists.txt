file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")

if (NOT WOWDECOMP_LINUX)
    list(FILTER PRIVATE_SOURCES EXCLUDE REGEX "/WxlVisuals/")
endif ()

add_library(ModernAssets STATIC
    ${PRIVATE_SOURCES}
)

target_include_directories(ModernAssets
    PUBLIC
        ${CMAKE_SOURCE_DIR}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/WxlM2
)

# The imported converter is the shared host-safe half of WarcraftXL's M2 path.
# Its live DLL render hooks are intentionally excluded; the native renderer remains authoritative.
target_compile_definitions(ModernAssets PRIVATE WXL_HOST=1)
target_compile_features(ModernAssets PRIVATE cxx_std_20)

if (WOWDECOMP_LINUX)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_custom_target(VerifyNativeVisualShaders
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-native-visual-shaders.py --verify
        VERBATIM)
    add_dependencies(ModernAssets VerifyNativeVisualShaders)
endif ()

if (TARGET dxvk_d3d9_headers)
    target_link_libraries(ModernAssets PRIVATE dxvk_d3d9_headers)
    target_include_directories(ModernAssets PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/WxlVisuals/compiled)
endif ()
