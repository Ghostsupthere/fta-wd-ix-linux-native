#include "WoW/Source/Ui/Linux/WxlClickThrough.h"

#if defined(WOWDECOMP_LINUX)

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "WoW/Source/Ui/GameUI.h"
#include "WoW/Source/Ui/ScriptEvents.h"

namespace {

constexpr size_t kMaxUnits = 64;

enum ButtonMask : unsigned { kLeft = 1, kRight = 2 };
enum Modifier : int { kAlt, kShift, kCtrl };

std::vector<uint64_t> s_guids;
unsigned s_buttons = 0;
Modifier s_modifier = kShift;

// Button of the click hit test in progress (0 = none, so mouseover tests never skip anything).
int s_activeButton = 0;
// Whether the modifier was held when that hit test started.
bool s_modifierHeld = false;

bool ModifierDown(Modifier modifier) {
    switch (modifier) {
        case kAlt:
            return CGGameUI::IsKeyDown(KEY_LALT) || CGGameUI::IsKeyDown(KEY_RALT);
        case kCtrl:
            return CGGameUI::IsKeyDown(KEY_LCONTROL) || CGGameUI::IsKeyDown(KEY_RCONTROL);
        default:
            return CGGameUI::IsKeyDown(KEY_LSHIFT) || CGGameUI::IsKeyDown(KEY_RSHIFT);
    }
}

// Lua: WXL_SetClickThrough(button, modifier, unit...) -> number of units stored.
int SetClickThrough(lua_State* L) {
    const char* button = lua_isstring(L, 1) ? lua_tolstring(L, 1, nullptr) : nullptr;
    const char* modifier = lua_isstring(L, 2) ? lua_tolstring(L, 2, nullptr) : nullptr;

    unsigned buttons = 0;
    if (button && !strcasecmp(button, "left")) buttons = kLeft;
    else if (button && !strcasecmp(button, "right")) buttons = kRight;
    else if (button && !strcasecmp(button, "both")) buttons = kLeft | kRight;
    else return luaL_error(L, "Usage: WXL_SetClickThrough(\"left\"|\"right\"|\"both\", \"alt\"|\"shift\"|\"ctrl\", unit, ...)");

    Modifier mod;
    if (modifier && !strcasecmp(modifier, "alt")) mod = kAlt;
    else if (modifier && !strcasecmp(modifier, "shift")) mod = kShift;
    else if (modifier && !strcasecmp(modifier, "ctrl")) mod = kCtrl;
    else return luaL_error(L, "Usage: WXL_SetClickThrough(\"left\"|\"right\"|\"both\", \"alt\"|\"shift\"|\"ctrl\", unit, ...)");

    std::vector<uint64_t> guids;
    const int top = lua_gettop(L);
    for (int i = 3; i <= top && guids.size() < kMaxUnits; i++) {
        if (!lua_isstring(L, i)) continue;
        WOWGUID guid = WOWGUID();
        if (!Script_GetGUIDFromToken(lua_tolstring(L, i, nullptr), guid, false) || !guid.m_guid) continue;
        if (std::find(guids.begin(), guids.end(), guid.m_guid) == guids.end()) guids.push_back(guid.m_guid);
    }

    s_buttons = buttons;
    s_modifier = mod;
    s_guids.swap(guids);
    lua_pushnumber(L, static_cast<lua_Number>(s_guids.size()));
    return 1;
}

}  // namespace

void WxlRegisterClickThroughLua() {
    // Function registration repeats on every UI load; the addon fills the list again after that.
    s_guids.clear();
    s_activeButton = 0;
    FrameScript_RegisterFunction("WXL_SetClickThrough", &SetClickThrough);
}

void WxlClickThroughBegin(int button) {
    s_activeButton = s_guids.empty() ? 0 : button;
    s_modifierHeld = s_activeButton && ModifierDown(s_modifier);
}

void WxlClickThroughEnd() {
    s_activeButton = 0;
    s_modifierHeld = false;
}

bool WxlClickThroughSkips(WOWGUID guid) {
    if (!s_activeButton || s_modifierHeld || !(s_buttons & static_cast<unsigned>(s_activeButton))) return false;
    return std::find(s_guids.begin(), s_guids.end(), guid.m_guid) != s_guids.end();
}

#endif  // WOWDECOMP_LINUX
