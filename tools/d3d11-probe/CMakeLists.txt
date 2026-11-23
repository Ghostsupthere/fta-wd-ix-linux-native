cmake_minimum_required(VERSION 3.20)
project(WxlD3d11Probe LANGUAGES CXX)
option(WOWDECOMP_D3D11 "Build the standalone Linux DXVK D3D11 probe" OFF)
if(WOWDECOMP_D3D11 AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(DXVK_NATIVE_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/../../.deps/dxvk/include/native"
        CACHE PATH "dxvk-native header directory")
    add_executable(wxl-d3d11-probe probe.cpp)
    target_compile_features(wxl-d3d11-probe PRIVATE cxx_std_17)
    target_include_directories(wxl-d3d11-probe SYSTEM PRIVATE
        "${DXVK_NATIVE_HEADERS}/windows" "${DXVK_NATIVE_HEADERS}/directx")
    target_link_libraries(wxl-d3d11-probe PRIVATE "${CMAKE_DL_LIBS}")
endif()
