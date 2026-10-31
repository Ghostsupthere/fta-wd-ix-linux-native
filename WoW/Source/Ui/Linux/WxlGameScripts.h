#ifndef WOW_SOURCE_UI_LINUX_WXLGAMESCRIPTS_H
#define WOW_SOURCE_UI_LINUX_WXLGAMESCRIPTS_H

// WarcraftXL modules ship small Lua watchers (aura watchers, grant handshakes) that the Windows
// core executes once in every new in-world FrameScript state (lua::RegisterScript). The native
// equivalent registers the source here and runs it once the in-world UI has finished loading.

#if defined(WOWDECOMP_LINUX)

void WxlRegisterGameScript(const char* name, const char* source);
void WxlRunGameScripts();

#endif  // WOWDECOMP_LINUX

#endif
