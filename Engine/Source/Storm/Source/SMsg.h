#ifndef ENGINE_SOURCE_STORM_SOURCE_SMSG_H
#define ENGINE_SOURCE_STORM_SOURCE_SMSG_H

#ifdef WOWDECOMP_LINUX
#include "Engine/Source/Base/OsTypes.h"
#else
#include <windows.h>
#endif
#include "Engine/Source/Storm/H/stpl.h"

struct WNDREC : TSLinkedNode<WNDREC> {
    /* 0008 */ HWND window;
};

#endif
