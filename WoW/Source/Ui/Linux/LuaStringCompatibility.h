#ifndef WOW_SOURCE_UI_LINUX_LUASTRINGCOMPATIBILITY_H
#define WOW_SOURCE_UI_LINUX_LUASTRINGCOMPATIBILITY_H

// Retail-style Lua string helpers expected by the FTA add-on corpus. The
// decompiled client currently omits them, while the WarcraftXL client exposes
// them to the same Interface/AddOns tree.

#if defined(WOWDECOMP_LINUX)

void WxlRegisterLuaStringCompatibility();

#endif  // WOWDECOMP_LINUX

#endif
