#include "WoW/Source/Ui/Linux/LuaStringCompatibility.h"

#if defined(WOWDECOMP_LINUX)

#include <cstddef>

#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/lua-5.1/src/lua.h"

namespace {

bool IsTrimWhitespace(unsigned char character) {
    return character == ' ' || character == '\t' || character == '\n' || character == '\r' ||
        character == '\f' || character == '\v';
}

int32_t Script_StrTrim(lua_State* L) {
    size_t length = 0;
    const char* text = luaL_checklstring(L, 1, &length);

    size_t first = 0;
    while (first < length && IsTrimWhitespace(static_cast<unsigned char>(text[first]))) {
        ++first;
    }

    size_t last = length;
    while (last > first && IsTrimWhitespace(static_cast<unsigned char>(text[last - 1]))) {
        --last;
    }

    lua_pushlstring(L, text + first, last - first);
    return 1;
}

}  // namespace

void WxlRegisterLuaStringCompatibility() {
    FrameScript_RegisterFunction("strtrim", &Script_StrTrim);

    lua_State* L = FrameScript_GetContext();
    lua_getglobal(L, "string");
    if (lua_istable(L, -1)) {
        lua_pushcfunction(L, &Script_StrTrim);
        lua_setfield(L, -2, "trim");
    }
    lua_pop(L, 1);
}

#endif  // WOWDECOMP_LINUX
