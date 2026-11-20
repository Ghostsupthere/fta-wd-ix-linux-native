#ifndef WOW_SOURCE_UI_MODERNLUAAPI_H
#define WOW_SOURCE_UI_MODERNLUAAPI_H

struct lua_State;
class CStatus;

// The modern Lua surface (C_FTA, C_Timer, C_AddOns, C_Item, C_Container, C_Spell, C_UnitAuras, diagnostics), a port of the Windows client's
// ModernLuaApi. Group bits of the ftaModernLuaAPI variable: 1 foundation, 2 game data, 4 UI (not ported yet), 8 diagnostics.
namespace ModernLuaApi {
int ReadEnabledGroups();
bool Install(lua_State* state, int groups, CStatus* status = nullptr);
}

#endif
