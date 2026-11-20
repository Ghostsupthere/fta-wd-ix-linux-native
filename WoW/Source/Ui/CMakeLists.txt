file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")

if (NOT WIN32)
    list(FILTER PRIVATE_SOURCES EXCLUDE REGEX "/UILCD/Windows/")
    list(FILTER PRIVATE_SOURCES EXCLUDE REGEX "/UILCD/LCD\\.cpp$")
endif ()

add_library(Ui STATIC
    ${PRIVATE_SOURCES}
)

# Native bonus loot roll UI (Windows client): the Lua file is embedded as a string constant
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/BonusLootUI.lua")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/BonusLootUI.lua" BONUS_LOOT_UI_SOURCE)
configure_file(BonusLootUIEmbedded.h.in BonusLootUIEmbedded.h @ONLY)

target_include_directories(Ui
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)

# The modern Lua surface: the four phase files are joined into one string constant (the Windows client's layout)
file(GLOB MODERN_LUA_FILES CONFIGURE_DEPENDS "ModernLuaApi/*.lua")
list(SORT MODERN_LUA_FILES)
set(MODERN_LUA_SOURCE "")
foreach(MODERN_LUA_FILE IN LISTS MODERN_LUA_FILES)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${MODERN_LUA_FILE}")
    file(READ "${MODERN_LUA_FILE}" MODERN_LUA_PHASE)
    string(APPEND MODERN_LUA_SOURCE "${MODERN_LUA_PHASE}\n")
endforeach()
configure_file(ModernLuaApi/Embedded.h.in ModernLuaApiEmbedded.h @ONLY)
target_include_directories(Ui PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

if (WIN32)
    target_link_libraries(Ui
        PRIVATE
            lgLcd
    )
endif ()

if (TARGET wxl_imgui)
    target_link_libraries(Ui PRIVATE wxl_imgui dxvk_d3d9_headers)
endif ()
