#ifndef WOW_SOURCE_UI_LINUX_WXLHOUSING_H
#define WOW_SOURCE_UI_LINUX_WXLHOUSING_H

// FTA housing, native part: cursor ray, placement ghost and transform handles for the FtaHousing add-on (see WxlHousing.cpp).

#if defined(WOWDECOMP_LINUX)

void WxlRegisterHousingLua();
void WxlHousingUpdate();       // once a frame, from the world update
void WxlHousingDrawGizmo();    // end of the world render
void WxlHousingWorldLeave();   // map unload / logout

#endif  // WOWDECOMP_LINUX

#endif
