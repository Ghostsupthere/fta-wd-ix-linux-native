// Native stand-in for the proxy import: the settings snapshot comes from the control bridge
// (WoW/Source/Ui/Linux/WxlControlBridge.cpp) instead of d3d9.dll.
#pragma once

#include <cstddef>

bool WxlGetControlSettings(void* out, size_t size);
