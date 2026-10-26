file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")

# PR1 keeps CGxDeviceD3d9Ex on Linux: it drives dxvk-native's libdxvk_d3d9.so
# (dlopen'd via OsLibrary's "d3d9.dll" map). The fixed-function D3D and the WGL
# OpenGL backends stay Windows-only.
if (NOT WIN32)
    list(FILTER PRIVATE_SOURCES EXCLUDE REGEX "/CGxDevice(D3d|OpenGL)/")
endif ()

add_library(Gx STATIC
    ${PRIVATE_SOURCES}
)

target_include_directories(Gx
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)

if (WIN32)
    target_link_libraries(Gx
        PRIVATE
            d3d9.lib
            nvapi
            opengl32.lib
    )
elseif (TARGET dxvk_d3d9_headers)
    # d3d9.h only (for CGxDeviceD3d9Ex); libdxvk_d3d9.so is dlopen'd at runtime.
    # Scoped to Gx so dxvk's <windows.h> types never reach the rest of the tree.
    target_link_libraries(Gx PRIVATE dxvk_d3d9_headers)
endif ()
