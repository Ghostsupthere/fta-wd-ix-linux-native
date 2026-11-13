#ifndef WOW_SOURCE_OBJECT_OBJECTCLIENT_UNITSOUND_C_H
#define WOW_SOURCE_OBJECT_OBJECTCLIENT_UNITSOUND_C_H

#include <cstddef>
#include <cstdint>
#include "WoW/Source/Object/ObjectClient/Types.h"

class CreatureSoundDataRec;

struct DEATTHUDSOUNDINFO {
    /* 0000 */ int32_t m_soundEntryId;
    /* 0004 */ int32_t m_soundEntryIdWater;

    DEATTHUDSOUNDINFO();
};

void UnitSoundInitialize();
void UnitSoundShutdown();
int32_t GetSoundID(const CreatureSoundDataRec* soundData, UNITSOUNDTYPE soundType);
int32_t CheckUnitSoundTimer(UNITSOUNDTYPE soundType);

class CGUnit_C;
// Linux client switch WXL_MUTE_MURLOC_VOICE: true when this unit uses the Murloc race model (CreatureModelData 2000001, also every
// NPCBots alt-bot display) and the switch is on. Always false elsewhere.
bool MurlocVoiceMuted(const CGUnit_C* unit);

#endif
