file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")

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
