file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")

add_library(WorldClient STATIC
    ${PRIVATE_SOURCES}
)

target_include_directories(WorldClient
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(WorldClient PRIVATE ModernAssets)
# Native loot-beam draw uses the existing D3D9 device, without linking another frontend.
if (TARGET dxvk_d3d9_headers)
    target_link_libraries(WorldClient PRIVATE dxvk_d3d9_headers)
endif ()
