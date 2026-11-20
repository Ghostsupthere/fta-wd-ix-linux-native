#include "MacroConditions.h"
#include "ChatFrame.h"
#include "UIMacroOptions.h"
#include "ExtendedMacroFormat.h"
#include "WoW/Source/DB/DBClient/DBClient.h"
#include "WoW/Source/DB/Rec/GlyphPropertiesRec.h"
#include "WoW/Source/DB/Rec/SpellRec.h"
#include "WoW/Source/DB/Rec/TalentRec.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/Ui/ActionBarFrame.h"
#include "WoW/Source/Ui/PetInfo.h"
#include "WoW/Source/Ui/SpellBookFrame.h"
#include "WoW/Source/Ui/TalentInfo.h"
#include "WoW/Source/Ui/GameUI.h"
#include "WoW/Source/Ui/UIMacros.h"
#include "WoW/Source/Net/Types.h"
#include "WoW/Source/WowSvcs/WowSvcsClient/ClientServices.h"
#include "Engine/Source/Base/CDataStore.h"
#include <cstring>
#include <unordered_map>

namespace {

std::unordered_map<uint32_t, int32_t> talentBySpell;
uint32_t timewalkingMap = UINT32_MAX;
bool timewalkingActive = false;

std::string_view Argument(const MacroSubstring& arg) {
    return {arg.m_start, static_cast<size_t>(arg.m_end - arg.m_start)};
}

bool ReadSpellID(std::string_view text, uint32_t& value) {
    return ExtendedMacros::ReadUnsigned(text, value) && value && value <= INT32_MAX;
}

CGTalentGroup_C* ActiveTalents() {
    return CGTalentInfo::GetTalentGroup(CGTalentInfo::GetActiveTalentGroup(0), 0);
}

}

void InitializeMacroOptions() {
    talentBySpell.clear();
    for (int32_t i = 0; i < g_talentDB.m_numRecords; ++i) {
        if (const auto* record = g_talentDB.GetRecordByIndex(i)) {
            for (int32_t spell : record->m_spellRank) {
                if (spell > 0) {
                    talentBySpell.emplace(spell, record->m_id);
                }
            }
        }
    }
}

int32_t EvaluateKnown(const WOWGUID*, const TSGrowableArray<MacroSubstring>* args) {
    for (uint32_t i = 0; i < args->m_count; ++i) {
        auto text = Argument(args->m_data[i]);
        uint32_t id = 0;
        if (!ReadSpellID(text, id)) {
            char name[256];
            if (text.empty() || text.size() >= sizeof(name)) {
                continue;
            }
            memcpy(name, text.data(), text.size());
            name[text.size()] = '\0';
            int32_t pet = 0;
            const int32_t spell = GetSpellIdByName(name, &pet);
            if (spell <= 0 || pet) {
                continue;
            }
            id = static_cast<uint32_t>(spell);
        }
        if (TalentInfoIsSpellKnown(static_cast<int32_t>(id), 0, 0)) {
            return 1;
        }
    }
    return 0;
}

int32_t EvaluateWrathTalent(const WOWGUID*, const TSGrowableArray<MacroSubstring>* args) {
    auto* group = ActiveTalents();
    if (!group) {
        return 0;
    }
    for (uint32_t i = 0; i < args->m_count; ++i) {
        auto text = Argument(args->m_data[i]);
        const size_t separator = text.find(':');
        uint32_t id = 0, rank = 1;
        if (!ReadSpellID(text.substr(0, separator), id) || (separator != std::string_view::npos &&
            (!ExtendedMacros::ReadUnsigned(text.substr(separator + 1), rank) || rank < 1 || rank > 9))) {
            continue;
        }
        const auto found = talentBySpell.find(id);
        if (found != talentBySpell.end()) {
            auto* talent = group->GetTalentItem(found->second);
            if (talent && talent->m_rank + 1 >= static_cast<int32_t>(rank)) {
                return 1;
            }
        }
    }
    return 0;
}

int32_t EvaluateTalentTree(const WOWGUID*, const TSGrowableArray<MacroSubstring>* args) {
    auto* group = ActiveTalents();
    if (!group) {
        return 0;
    }
    uint32_t points = 0, tree = 0;
    for (uint32_t i = 0; i < 3 && i < group->m_tabs.m_count; ++i) {
        auto* tab = group->GetTab(i);
        if (tab && tab->m_pointsSpent > points) {
            points = tab->m_pointsSpent;
            tree = i + 1;
        }
    }
    for (uint32_t i = 0; tree && i < args->m_count; ++i) {
        uint32_t wanted = 0;
        if (ExtendedMacros::ReadUnsigned(Argument(args->m_data[i]), wanted) && wanted == tree) {
            return 1;
        }
    }
    return 0;
}

int32_t EvaluateGlyph(const WOWGUID*, const TSGrowableArray<MacroSubstring>* args) {
    auto* group = ActiveTalents();
    auto* player = ClntObjMgrGetActivePlayerObj();
    if (!group || !player) {
        return 0;
    }
    for (uint32_t i = 0; i < 6; ++i) {
        if (!(player->m_player->m_playerGlyphsEnabled & (1u << i))) {
            continue;
        }
        auto* glyph = g_glyphPropertiesDB.GetRecord(group->GetGlyph(i));
        if (!glyph) {
            continue;
        }
        for (uint32_t j = 0; j < args->m_count; ++j) {
            uint32_t id = 0;
            if (ReadSpellID(Argument(args->m_data[j]), id) && id == static_cast<uint32_t>(glyph->m_spellID)) {
                return 1;
            }
        }
    }
    return 0;
}

int32_t EvaluateResting(const WOWGUID*, const TSGrowableArray<MacroSubstring>*) {
    auto* player = ClntObjMgrGetActivePlayerObj();
    return player && (player->m_player->m_playerFlags & 0x20);
}

int32_t EvaluateOverrideBar(const WOWGUID*, const TSGrowableArray<MacroSubstring>*) {
    return (CGActionBar::m_tempPageActiveFlags & 0x4) != 0;
}

int32_t EvaluatePossessBar(const WOWGUID*, const TSGrowableArray<MacroSubstring>*) {
    SpellRec spell;
    return CGPetInfo::m_possessSpell && g_spellDB.GetRecord(CGPetInfo::m_possessSpell, &spell) &&
        !SpellRec::HasAura(&spell, 236);
}

int32_t EvaluateTimewalking(const WOWGUID*, const TSGrowableArray<MacroSubstring>*) {
    return timewalkingActive && timewalkingMap == CGGameUI::m_iCurrentMapID;
}

void RequestMacroTimewalking() {
    const auto* character = ClientServices::GetCharacterInfo();
    if (!character || character->m_guid != ClntObjMgrGetActivePlayer() || !character->m_name[0] ||
        !ClientServices::s_currentConnection) {
        return;
    }
    CDataStore packet;
    packet.Put(static_cast<uint32_t>(CMSG_MESSAGECHAT));
    packet.Put(static_cast<uint32_t>(CHAT_MSG_WHISPER));
    packet.Put(0xFFFFFFFFu);
    packet.PutString(character->m_name);
    packet.PutString("DinkleTW\tmacro");
    packet.Finalize();
    ClientServices::Send2(&packet);
}

void ResetMacroTimewalking() {
    timewalkingMap = UINT32_MAX;
    timewalkingActive = false;
}

// gm is ignored on purpose: a character with GM chat on gets its own whispers in the GM message form (see BonusLootClient).
void HandleMacroTimewalking(const char* data, uint32_t length, uint8_t type, bool /*gm*/, WOWGUID from, WOWGUID to) {
    constexpr std::string_view prefix = "DinkleTW\tMACRO|";
    const auto self = ClntObjMgrGetActivePlayer();
    if (!data || length <= prefix.size() + 2 || length > 32 || type != CHAT_MSG_WHISPER ||
        !self || from != self || to != self || data[length - 1] != '\0') {
        return;
    }
    std::string_view message(data, length - 1);
    if (message.substr(0, prefix.size()) != prefix) {
        return;
    }
    message.remove_prefix(prefix.size());
    const auto split = message.find('|');
    uint32_t map = 0;
    if (split == std::string_view::npos || !ExtendedMacros::ReadUnsigned(message.substr(0, split), map) ||
        map != CGGameUI::m_iCurrentMapID) {
        return;
    }
    const auto status = message.substr(split + 1);
    if (status == "0" || status == "1") {
        timewalkingMap = map;
        timewalkingActive = status == "1";
        if (CGGameUI::s_inWorld) {
            CGUIMacros::UpdateDynamicMacros();
        }
    }
}
