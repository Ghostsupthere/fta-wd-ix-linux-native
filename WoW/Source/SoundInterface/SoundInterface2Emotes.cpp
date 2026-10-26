#include <cstddef>
#include <cstdint>
#include <cstring>
#include "WoW/Source/SoundInterface/SoundInterface2Emotes.h"
#include "Engine/Source/Storm/H/stpl.h"
#include "WoW/Source/DB/DBClient/DBClient.h"
#include "WoW/Source/SoundInterface/SoundInterface2.h"
#include "WoW/Source/SoundInterface/SoundInterface2Internal.h"

// STATIC: 0x00B4ADE4
static TSGrowableArray<TEXTEMOTELOOKUP> s_textEmoteLookups;

// STATIC: 0x00B4ADF0
static int32_t s_numTextEmotes;

// STATIC: 0x00B4ADF4
static TSGrowableArray<TEXTEMOTELOOKUP*> s_textEmoteLookupById;

// INLINED
TEXTEMOTELOOKUP::TEXTEMOTELOOKUP() {
    this->m_emotesTextId = 0;
    memset(this->m_soundIds, 0, sizeof(this->m_soundIds));
}

// VERIFY: b8c12fce auto
// OFFSET: 0x004CEB30
int32_t SI2::GetTextEmoteSoundID(uint32_t emotesTextId, uint32_t race, uint32_t sex) {
    TEXTEMOTELOOKUP* lookup;

    if (emotesTextId < s_textEmoteLookupById.Count() && (lookup = s_textEmoteLookupById[emotesTextId]) != nullptr && race < 22 && sex < 2) {
        return lookup->m_soundIds[race][sex];
    }

    return 0;
}

// VERIFY: 8c9fb06d auto
// OFFSET: 0x004CEB70
int32_t SI2::GetTextEmoteSoundIndex(uint32_t emotesTextId, uint32_t race, uint32_t sex) {
    TEXTEMOTELOOKUP* lookup;
    int32_t soundKitId;

    if (emotesTextId < s_textEmoteLookupById.Count() && (lookup = s_textEmoteLookupById[emotesTextId]) != nullptr && race < 22 && sex < 2) {
        soundKitId = lookup->m_soundIds[race][sex];
    } else {
        soundKitId = 0;
    }

    VSOUNDKITDEF* def = SI2::GetSoundKitDef(soundKitId);

    if (!def || !def->m_numFiles) {
        return -1;
    }

    int32_t index = 0;
    def->SelectFile(2, &index, -1, 5);
    return index;
}

// VERIFY: bf97a618 auto
// OFFSET: 0x004CECB0
void SI2::InitEmoteSounds() {
    if (!g_emotesTextDB.m_numRecords || !g_emotesTextSoundDB.m_numRecords) {
        return;
    }

    s_textEmoteLookupById.SetCount(g_emotesTextDB.m_maxID + 1);
    memset(s_textEmoteLookupById.Ptr(), 0, sizeof(TEXTEMOTELOOKUP*) * s_textEmoteLookupById.Count());

    int32_t last = -1;
    int32_t count = 0;

    for (int32_t i = 0; i < g_emotesTextSoundDB.m_numRecords; i++) {
        EmotesTextSoundRec* rec = (i < 0 || i >= g_emotesTextSoundDB.m_numRecords) ? nullptr : &g_emotesTextSoundDB.m_records[i];

        if (last == -1 || last != rec->m_emotesTextID) {
            last = rec->m_emotesTextID;
            count++;
        }
    }

    s_numTextEmotes = count;

    // FIX (2026-10-26): s_textEmoteLookupById caches raw TEXTEMOTELOOKUP*
    // pointers returned by s_textEmoteLookups.New() below. New() returns a
    // pointer INTO s_textEmoteLookups' own backing array (it stores
    // TEXTEMOTELOOKUP objects by value, not pointers) -- so if that array
    // ever grows past its current capacity mid-loop, the backing block gets
    // reallocated and every pointer already cached in s_textEmoteLookupById
    // from an earlier iteration goes dangling. A later iteration whose
    // EmotesTextSoundRec shares an m_emotesTextID with an earlier one then
    // writes through that stale pointer at line ~100 below -- a real
    // heap-use-after-free, confirmed via AddressSanitizer (WRITE to a freed
    // TSGrowableArray<TEXTEMOTELOOKUP> block, inside this exact function).
    // `count` is already exactly the number of New() calls this loop will
    // make (computed in the pass just above) -- reserving that capacity
    // upfront means New() can never reallocate mid-loop, so no pointer
    // cached in s_textEmoteLookupById can ever go stale here.
    s_textEmoteLookups.Reserve(count, 0);

    for (int32_t i = 0; i < g_emotesTextSoundDB.m_numRecords; i++) {
        EmotesTextSoundRec* rec = (i < 0 || i >= g_emotesTextSoundDB.m_numRecords) ? nullptr : &g_emotesTextSoundDB.m_records[i];
        uint32_t id = rec->m_emotesTextID;

        if (id >= s_textEmoteLookupById.Count()) {
            continue;
        }

        TEXTEMOTELOOKUP** slot = &s_textEmoteLookupById[id];

        if (!*slot) {
            *slot = s_textEmoteLookups.New();
            (*slot)->m_emotesTextId = rec->m_emotesTextID;
        }

        if (static_cast<uint32_t>(rec->m_raceID) < 22 && static_cast<uint32_t>(rec->m_sexID) < 2) {
            (*slot)->m_soundIds[rec->m_raceID][rec->m_sexID] = rec->m_soundID;
        }
    }
}

// VERIFY: cf5849ee manual
// OFFSET: 0x004CEDF0
void SI2::FreeEmoteSounds() {
    s_textEmoteLookups.Clear();
    s_textEmoteLookupById.Clear();
}
