if (WIN32)
    # Windows: a side-by-side DLL the client resolves at load time, wrapping the
    # vendored xvidcore import library.
    add_library(XvidDecoder SHARED
        XvidDecoder.cpp
        XvidDecoder.def
    )

    set_target_properties(XvidDecoder PROPERTIES
        OUTPUT_NAME XvidDecoder
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/Win
    )

    foreach (config ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE})
        string(TOUPPER ${config} config)
        set_target_properties(XvidDecoder PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} ${CMAKE_SOURCE_DIR}/bin/Win)
    endforeach ()

    target_link_libraries(XvidDecoder
        PRIVATE
            ${CMAKE_SOURCE_DIR}/ThirdParty/xvidcore/lib/x64/libxvidcore.lib
    )

    target_compile_options(XvidDecoder
        PRIVATE
            $<$<CONFIG:Debug>:/MD>
    )
else ()
    # Linux: XvidDecoder.cpp is portable (its only dependency is <xvid.h>).
    # ThirdParty/xvidcore ships headers + Windows binaries only, so the symbols
    # come from the system xvidcore (shell-native.nix provides pkgs.xvidcore),
    # and the decoder is linked straight into the client as a static library
    # rather than reproducing the Windows side-by-side-DLL dance.
    add_library(XvidDecoder STATIC
        XvidDecoder.cpp
    )

    find_library(XVIDCORE_LIB xvidcore REQUIRED)
    target_link_libraries(XvidDecoder
        PUBLIC
            ${XVIDCORE_LIB}
    )
endif ()

target_include_directories(XvidDecoder
    PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/ThirdParty/xvidcore/include
)
