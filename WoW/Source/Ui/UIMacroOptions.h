#ifndef WOW_SOURCE_UI_UIMACROOPTIONS_H
#define WOW_SOURCE_UI_UIMACROOPTIONS_H

#include "WoW/Source/Object/WGUID.h"
#include <cstdint>

void InitializeMacroOptions();
void RequestMacroTimewalking();
void ResetMacroTimewalking();
void HandleMacroTimewalking(const char* data, uint32_t length, uint8_t type, bool gm, WOWGUID from, WOWGUID to);

#endif
