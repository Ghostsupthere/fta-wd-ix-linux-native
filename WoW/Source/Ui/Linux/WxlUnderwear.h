#ifndef WOW_SOURCE_UI_LINUX_WXLUNDERWEAR_H
#define WOW_SOURCE_UI_LINUX_WXLUNDERWEAR_H

// Native port of WarcraftXL's wxl-underwear: a per-character base-underwear choice for the
// Transmogrification add-on (WXL_UnderwearSet / Reset / Refresh / Ready). The DLL detours three
// client functions at fixed addresses; here the same three decisions are made from inside
// CCharacterComponent (GetSectionsRecord, CreateRequestTL, RenderPrepTL). Shared DBC rows and
// texture pixels stay immutable: the override is a per-character copy of the section record.

#if defined(WOWDECOMP_LINUX)

#include "WoW/Common/TextureCache.h"

class CCharacterComponent;
class CharSectionsRec;

void WxlRegisterUnderwearLua();
void WxlUnderwearWorldLeave();

// The underwear record the component should use instead of `original`, or `original` itself.
CharSectionsRec* WxlUnderwearRecord(const CCharacterComponent* component, CharSectionsRec* original);
// The torso-lower underwear band for this component, or null (no profile, or a covered top).
HTEXTURECACHE WxlUnderwearLowerTexture(const CCharacterComponent* component);

#endif  // WOWDECOMP_LINUX

#endif
