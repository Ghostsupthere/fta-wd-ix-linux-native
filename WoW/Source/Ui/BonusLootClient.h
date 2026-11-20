#pragma once

#include "WoW/Source/Object/WGUID.h"
#include <cstdint>

struct lua_State;

namespace BonusLootClient {
void Register(lua_State* state);
void InstallUI();
void EnterWorld();
void Reset();
bool HandleMessage(const char* text, uint32_t length, uint8_t type, bool gm, WOWGUID sender, WOWGUID receiver);
}
