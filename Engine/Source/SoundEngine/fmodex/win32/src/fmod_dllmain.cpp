#ifdef WOWDECOMP_LINUX
#include "Engine/Source/Base/OsTypes.h"
#else
#include <windows.h>
#endif

BOOL WINAPI _DllMainCRTStartup(HANDLE hInst, ULONG ul_reason_for_call, LPVOID lpReserved)
{
	return TRUE;
}