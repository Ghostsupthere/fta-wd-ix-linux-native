#pragma once
// Native port of the Environment page backend of the supplied wxl-living-azeroth (Options.cpp): the
// WarcraftXL_*EnvironmentOption Lua functions the WarcraftXLControl add-on's Environment page calls,
// persisted in WTF/WXL/Environment.ini beside the Windows client's file (same keys and ranges).
//
// Live here: grass.instanced (WxlGrassInstancingSetEnabled), grass.windStrength / windSpeed /
// interaction (WxlGrassSetMotion). Stored and saved but with no renderer behind them yet:
// grass.memoryMB / buildMs (the native instancing keeps static buffers, there is no pool or build
// budget) and every lights.* option (the clustered surface-light renderer is not ported).
#if defined(WOWDECOMP_LINUX)

// Reads the ini once and applies it; later calls do nothing.
void WxlEnvironmentOptionsLoad();
// Once per rendered frame: feeds the performance sample.
void WxlEnvironmentOptionsFrame();
void WxlRegisterEnvironmentLua();

#endif
