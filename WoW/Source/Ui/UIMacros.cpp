#include "UIMacros.h"
#include "ExtendedMacroFormat.h"
#include "WoW/Source/Ui/ActionBarFrame.h"
#include "Engine/Source/Storm/H/storm.h"
#include "WoW/Source/DB/DBClient/DBCacheInstances.h"
#include "WoW/Source/DB/DBClient/DBClient.h"
#include "WoW/Source/DB/Rec/SpellRec.h"
#include "WoW/Source/Object/ItemStats.h"
#include "WoW/Source/Object/ObjectClient/Item_C.h"
#include "WoW/Source/Object/ObjectClient/Unit_C.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/Ui/PetInfo.h"
#include "WoW/Source/Ui/UIMacroOptions.h"
#include "WoW/Source/WowSvcs/WowSvcsClient/AccountData.h"
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include "Engine/Source/BlizzardCore/SFile2-Core.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Os/OsFile.h"
#include "Engine/Source/Os/TimeManager.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "WoW/Source/Client.h"
#include "WoW/Source/Ui/GameUI.h"
#include "WoW/Source/Ui/ScriptEvents.h"
#include "Engine/Source/Base/ConvertUTF.h"
#include "WoW/Source/Ui/ContainerFrame.h"
#include "WoW/Source/Ui/SpellBookFrame.h"
#include "WoW/Source/Object/ObjectClient/Bag_C.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "WoW/Source/Ui/MacroConditions.h"

namespace {

uint32_t pendingCollections = 0;
uint32_t unreadableCollections = 0;

const char* CollectionError(uint32_t scope) {
    if (pendingCollections & (1u << scope)) {
        return "Your macros are still loading.";
    }
    if (unreadableCollections & (1u << scope)) {
        return "The macro collection could not be read. Your saved macro file has been kept.";
    }
    return nullptr;
}

} // namespace

// STATIC: 0x00ACE2A0
static TSExplicitList<MACRONODE, offsetof(MACRONODE, m_dynamicLink)> s_dynamicMacros;

// STATIC: 0x00BEAE20
uint32_t CGUIMacros::m_sortedMacros[72];

// STATIC: 0x00BEAF68
static TSHashTable<MACRONODE, HASHKEY_NONE> s_macros;

// STATIC: 0x00ACE1E8
const char* s_macroIconPath = "Interface\\Icons\\";

// STATIC: 0x00BEAF40
int32_t CGUIMacros::m_saveMacros;

// STATIC: 0x00BEAF44
uint32_t CGUIMacros::m_state;

// STATIC: 0x00BEAF48
const char* CGUIMacros::m_macroRunning;

// STATIC: 0x00BEAF4C
uint32_t CGUIMacros::m_runningMacroID;
bool CGUIMacros::m_executing;

// STATIC: 0x00BEAF50
uint32_t CGUIMacros::m_numMacros[2];

// STATIC: 0x00BEAF58
static uint32_t s_maxMacroID[2];

// STATIC: 0x00A0F82C
static constexpr uint32_t s_maxMacros[2] = { ExtendedMacros::SlotCount(0), ExtendedMacros::SlotCount(1) };

// STATIC: 0x00A0F834
static constexpr uint32_t s_macroIDBase[2] = { ExtendedMacros::IDBase(0), ExtendedMacros::IDBase(1) };

// STATIC: 0x00BEAF90
TSGrowableArray<char*> s_macroIcons;

// STATIC: 0x00BEAFA0
TSGrowableArray<char*> s_macroItemIcons;

// VERIFY: 395f9dc1 auto
// OFFSET: 0x00564AB0
int32_t CGUIMacros::GetMacroIndexByID(uint32_t id) {
    for (int32_t i = 0; i < static_cast<int32_t>(sizeof(CGUIMacros::m_sortedMacros) / sizeof(CGUIMacros::m_sortedMacros[0])); i++) {
        if (CGUIMacros::m_sortedMacros[i] == id) {
            return i;
        }
    }

    return -1;
}

// VERIFY: 3cc88c82 auto
// OFFSET: 0x005660E0
void MACRONODE::IncrementActionBarCount() {
    if (this->m_dynamic && !this->m_actionBarCount) {
        s_dynamicMacros.LinkToTail(this);
    }

    this->m_actionBarCount++;
}

// VERIFY: 462f140c manual
// OFFSET: 0x00566110
void MACRONODE::DecrementActionBarCount() {
    this->m_actionBarCount--;

    if (this->m_dynamic && !this->m_actionBarCount) {
        s_dynamicMacros.UnlinkNode(this);
    }
}

// VERIFY: 83d22513 auto
// OFFSET: 0x00566160
void CGUIMacros::UpdateMacroSpells() {
    for (MACRONODE* macro = s_macros.m_fulllist.Head(); macro; macro = s_macros.m_fulllist.Next(macro)) {
        if (macro->m_spellID < 0 && macro->FindSpellID()) {
            CGActionBar::UpdateMacro(macro->m_id);
        }
    }
}

// VERIFY: 27651ef3 auto
// OFFSET: 0x00566620
int32_t CGUIMacros::GetSortedIndexByName(const char* name) {
    MACRONODE* macro = CGUIMacros::FindMacro(name);

    if (!macro) {
        return -1;
    }

    for (int32_t i = 0; i < 72; i++) {
        if (CGUIMacros::m_sortedMacros[i] == macro->m_id) {
            return i;
        }
    }

    return -1;
}

// VERIFY: e9bab52e manual
// OFFSET: 0x00566660
void CGUIMacros::UpdateDynamicMacros() {
    for (MACRONODE* macro = s_dynamicMacros.Head(); macro; macro = s_dynamicMacros.Next(macro)) {
        if (macro->FindSpellID()) {
            CGActionBar::UpdateMacro(macro->m_id);
        }
    }
}

// VERIFY: 67f7b9f9 manual
// OFFSET: 0x00566A70
MACRONODE* CGUIMacros::GetMacroByIndex(uint32_t index) {
    if (index < sizeof(CGUIMacros::m_sortedMacros) / sizeof(CGUIMacros::m_sortedMacros[0])) {
        return s_macros.Ptr(CGUIMacros::m_sortedMacros[index], HASHKEY_NONE());
    }

    return nullptr;
}

// VERIFY: 01debf71 auto
// OFFSET: 0x00566AA0
MACRONODE* CGUIMacros::GetMacroByID(uint32_t id) {
    return s_macros.Ptr(id, HASHKEY_NONE());
}

// VERIFY: 7fa1863e auto
// OFFSET: 0x00566DC0
void CGUIMacros::RunMacro(int32_t id, const char* button) {
    CGUIMacros::RunMacro(s_macros.Ptr(id, HASHKEY_NONE()), button);
}

// VERIFY: 1c1a9d37 manual
// OFFSET: 0x00566AC0
void CGUIMacros::GetFullIconPath(uint32_t id, char* buffer, uint32_t size) {
    *buffer = 0;
    MACRONODE* macro = s_macros.Ptr(id, HASHKEY_NONE());

    if (!macro) {
        return;
    }

    if (macro->m_dynamic && !macro->m_actionBarCount) {
        macro->FindSpellID();
    }

    if (!SStrCmp(macro->m_icon, "INV_Misc_QuestionMark", STORM_MAX_STR)) {
        if (macro->m_itemID) {
            const char* path = ClientDBStringLookup(SLOOKUP_INVENTORYICONPATH);
            WOWGUID none = WOWGUID();
            ItemStats_C* stats = g_itemDBCache.GetRecord(macro->m_itemID, none, nullptr, nullptr, false);
            int32_t displayId = stats ? stats->m_displayId : 0;
            SStrPrintf(buffer, size, "%s%s%s", path, *path ? "\\" : "", CGItem_C::GetInventoryArt(displayId));
            return;
        }

        if (macro->m_spellID) {
            SpellRec spell;

            if (g_spellDB.GetRecord(macro->m_spellID, &spell)) {
                int32_t iconId = spell.m_spellIconID;

                if (spell.m_activeIconID) {
                    CGUnit_C* unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(macro->m_spellFlags ? CGPetInfo::GetPet(0) : ClntObjMgrGetActivePlayer(), TYPE_UNIT, __FILE__, __LINE__));

                    if (unit) {
                        uint32_t count = unit->GetAuraCount();

                        for (uint32_t i = 0; i < count; i++) {
                            if (unit->GetAuraSpellId(i) == spell.m_id && (unit->GetAuraFlags(i) & 0x10)) {
                                iconId = spell.m_activeIconID;
                                break;
                            }
                        }
                    }
                }

                SpellIconRec* icon = g_spellIconDB.GetRecord(iconId);

                if (icon) {
                    SStrCopy(buffer, icon->m_textureFilename, size);
                    return;
                }
            }
        }
    }

    SStrPrintf(buffer, size, "%s%s", s_macroIconPath, macro->m_icon);
}

// VERIFY: ee9d285c manual
// OFFSET: 0x00566330
MACRONODE* CGUIMacros::FindMacro(const char* name) {
    if (!name || !*name) {
        return nullptr;
    }

    for (MACRONODE* macro = s_macros.m_fulllist.Head(); macro; macro = s_macros.m_fulllist.Next(macro)) {
        if (!SStrCmpI(name, macro->m_name, STORM_MAX_STR)) {
            return macro;
        }
    }

    return nullptr;
}

// VERIFY: ebb01234 auto
// OFFSET: 0x0050F4D0
bool CGUIMacros::IsActionAllowed(uint32_t action) {
    return !CGUIMacros::m_executing || (action < 32 && ((1u << action) & CGUIMacros::m_state));
}

// VERIFY: 2f0aa8c8 auto
// OFFSET: 0x00565AD0
void CGUIMacros::RemoveDynamicMacro(MACRONODE* macro) {
    s_dynamicMacros.UnlinkNode(macro);
}

// VERIFY: 4406f3d1 auto
// OFFSET: 0x00566140
void CGUIMacros::Initialize() {
    InitializeMacroOptions();
    CGUIMacros::LoadIcons();
}

// VERIFY: 7d331447 auto
// OFFSET: 0x00567FD0
void CGUIMacros::LoadMacros() {
    pendingCollections = 3;
    unreadableCollections = 0;
    LoadMacroAccountData(0, CGUIMacros::LoadMacroCollection);
    LoadMacroAccountData(1, CGUIMacros::LoadMacroCollection);
}

// VERIFY: 1a2a9c35 auto
// OFFSET: 0x00564A20
static int32_t QSortIcons(const void* a, const void* b) {
    const char* nameB = *static_cast<char* const*>(b);
    const char* nameA = *static_cast<char* const*>(a);

    if (!SStrCmp(nameA, "INV_Misc_QuestionMark", STORM_MAX_STR)) {
        return -1;
    }

    if (!SStrCmp(nameB, "INV_Misc_QuestionMark", STORM_MAX_STR)) {
        return 1;
    }

    return SStrCmpI(nameA, nameB, STORM_MAX_STR);
}

// VERIFY: 2e63df8f auto
// OFFSET: 0x00564A80
static int32_t QSortMacrosByName(const void* a, const void* b) {
    return SStrCmpI((*static_cast<MACRONODE* const*>(a))->m_name, (*static_cast<MACRONODE* const*>(b))->m_name, STORM_MAX_STR);
}

// VERIFY: b423fecf manual
// OFFSET: 0x00565550
static void AddMacroIcon(const char* fileName) {
    char* ext = SStrChrR(const_cast<char*>(fileName), '.');

    if (!ext) {
        return;
    }

    if (!SStrCmpI(ext, ".blp", STORM_MAX_STR) || !SStrCmpI(ext, ".tga", STORM_MAX_STR)) {
        char* name = SStrDupA(fileName, __FILE__, __LINE__);
        name[ext - fileName] = '\0';
        *s_macroIcons.New() = name;
    }
}

// VERIFY: f420fc9d manual
// OFFSET: 0x005655C0
static void AddMacroItemIcon(const char* fileName) {
    char* ext = SStrChrR(const_cast<char*>(fileName), '.');

    if (!ext) {
        return;
    }

    if (!SStrCmpI(ext, ".blp", STORM_MAX_STR) || !SStrCmpI(ext, ".tga", STORM_MAX_STR)) {
        char* name = SStrDupA(fileName, __FILE__, __LINE__);
        name[ext - fileName] = '\0';
        *s_macroItemIcons.New() = name;
    }
}

// VERIFY: ae9495e2 auto
// OFFSET: 0x00565630
static int32_t IconListArchiveCallback(const char* fileName, void* param) {
    const char* name = fileName + SStrLen(s_macroIconPath);

    if (!SStrCmpI(name, "Ability_", SStrLen("Ability_")) || !SStrCmpI(name, "Spell_", SStrLen("Spell_"))) {
        AddMacroIcon(name);
    }

    if (!SStrCmpI(name, "INV_", SStrLen("INV_"))) {
        AddMacroItemIcon(name);
    }

    return 1;
}

// VERIFY: f321e4f2 auto
// OFFSET: 0x005656B0
static int32_t IconListDirectoryCallback(OS_FILE_DATA& data, void* param) {
    if (data.flags & OS_FILE_ATTRIBUTE_DIRECTORY) {
        return 0;
    }

    char name[260];

    if (!SStrCmpI(data.fileName, "Ability_", SStrLen("Ability_")) || !SStrCmpI(data.fileName, "Spell_", SStrLen("Spell_"))) {
        SStrCopy(name, data.fileName, sizeof(name));

        for (char* dot = SStrChr(name, '.'); dot && *dot; dot = SStrChr(dot + 1, '.')) {
            if (!SStrCmpI(dot, ".bz", STORM_MAX_STR)) {
                *dot = '\0';
                break;
            }
        }

        AddMacroIcon(name);
    }

    if (!SStrCmpI(data.fileName, "INV_", SStrLen("INV_"))) {
        SStrCopy(name, data.fileName, sizeof(name));

        for (char* dot = SStrChr(name, '.'); dot && *dot; dot = SStrChr(dot + 1, '.')) {
            if (!SStrCmpI(dot, ".bz", STORM_MAX_STR)) {
                *dot = '\0';
                break;
            }
        }

        AddMacroItemIcon(name);
    }

    return 0;
}

// VERIFY: 53ef5430 auto
// OFFSET: 0x00565810
static int32_t IconListCustomCallback(OS_FILE_DATA& data, void* param) {
    if (!(data.flags & OS_FILE_ATTRIBUTE_DIRECTORY)) {
        AddMacroIcon(data.fileName);
        AddMacroItemIcon(data.fileName);
    }

    return 0;
}

// VERIFY: d515d51e manual
// OFFSET: 0x00565840
void CGUIMacros::LoadIcons() {
    s_macroIcons.m_chunk = 256;
    s_macroItemIcons.m_chunk = 256;
    *s_macroIcons.New() = SStrDupA("INV_Misc_QuestionMark", __FILE__, __LINE__);
    *s_macroItemIcons.New() = SStrDupA("INV_Misc_QuestionMark", __FILE__, __LINE__);

    ListInterfaceArchive(s_macroIconPath, IconListArchiveCallback, nullptr);

    char path[260];
    SStrPrintf(path, sizeof(path), "%s%s", SFile::GetDataPath(), s_macroIconPath);
    OsFileList(path, "*", IconListDirectoryCallback, nullptr, 0);
    OsFileList(s_macroIconPath, "*", IconListCustomCallback, nullptr, 0);

    qsort(s_macroIcons.m_data, s_macroIcons.Count(), sizeof(char*), QSortIcons);
    qsort(s_macroItemIcons.m_data, s_macroItemIcons.Count(), sizeof(char*), QSortIcons);

    uint32_t count = s_macroIcons.Count();
    uint32_t i = count - 1;

    if (count != 1) {
        do {
            char* next = s_macroIcons.m_data[i];
            i--;

            if (!SStrCmpI(s_macroIcons.m_data[i], next, STORM_MAX_STR)) {
                SMemFree(s_macroIcons.m_data[i], __FILE__, __LINE__, 0);
                count--;

                if (i < count) {
                    memmove(&s_macroIcons.m_data[i], &s_macroIcons.m_data[i + 1], sizeof(char*) * (count - i));
                }
            }
        } while (i);

        if (count != s_macroIcons.Count()) {
            s_macroIcons.SetCount(count);
        }
    }

    count = s_macroItemIcons.Count();
    i = count - 1;

    if (count != 1) {
        do {
            char* next = s_macroItemIcons.m_data[i];
            i--;

            if (!SStrCmpI(s_macroItemIcons.m_data[i], next, STORM_MAX_STR)) {
                SMemFree(s_macroItemIcons.m_data[i], __FILE__, __LINE__, 0);
                count--;

                if (i < count) {
                    memmove(&s_macroItemIcons.m_data[i], &s_macroItemIcons.m_data[i + 1], sizeof(char*) * (count - i));
                }
            }
        } while (i);

        if (count != s_macroItemIcons.Count()) {
            s_macroItemIcons.SetCount(count);
        }
    }
}

struct MacroSlashCommand {
    const char* m_token;
    size_t m_length;
    int32_t m_type;
};

// VERIFY: 899df668 manual
// OFFSET: 0x00565B30
MACRONODE::~MACRONODE() {
}

// VERIFY: 3c4c57e8 auto
// OFFSET: 0x00565BA0
MACRONODE::MACRONODE() {
    this->m_id = 0;
    this->m_type = 0;
    this->m_name[0] = '\0';
    this->m_icon[0] = '\0';
    this->m_actionBarCount = 0;
    this->m_spellID = 0;
    this->m_itemID = 0;
    this->m_spellFlags = 0;
    this->m_showTooltip = 0;
    this->m_dynamic = 0;
    this->m_target = WOWGUID();
}

// VERIFY: 31c32426 auto
// OFFSET: 0x00565C20
uint32_t GetNumMacroIcons() {
    if (!s_macroIcons.m_count) {
        CGUIMacros::LoadIcons();
    }

    return s_macroIcons.m_count;
}

// VERIFY: e04b7c4d manual
// OFFSET: 0x00565C40
int32_t MACRONODE::Parse() {
    lua_State* L = FrameScript_GetContext();
    this->m_showTooltip = 0;
    this->m_dynamic = 0;
    this->m_commands.SetCount(0);

    std::string_view body = this->m_body;
    MacroSubstring command = { nullptr, nullptr };
    MacroSubstring args = { nullptr, nullptr };

    while (!body.empty()) {
        const auto line = ExtendedMacros::NextLine(body);
        if (line.empty()) {
            continue;
        }
        const char* lineEnd = line.data() + line.size();
        int32_t type = 0;
        command.SetText(line.data(), lineEnd, ' ', &args.m_start);
        if (command.m_start == command.m_end) {
            continue;
        }

        if (*command.m_start == '/') {
            // STATIC: 0x00ACE300
            static MacroSlashCommand s_slashCommands[] = {
                { "SLASH_CAST", SStrLen("SLASH_CAST"), 2 },
                { "SLASH_CASTRANDOM", SStrLen("SLASH_CASTRANDOM"), 3 },
                { "SLASH_CASTSEQUENCE", SStrLen("SLASH_CASTSEQUENCE"), 4 },
                { "SLASH_USE", SStrLen("SLASH_USE"), 2 },
                { "SLASH_USERANDOM", SStrLen("SLASH_USERANDOM"), 3 },
                { "SLASH_EQUIP", SStrLen("SLASH_EQUIP"), 5 },
                { "SLASH_EQUIP_TO_SLOT", SStrLen("SLASH_EQUIP_TO_SLOT"), 6 },
            };

            for (uint32_t i = 0; !type && i < sizeof(s_slashCommands) / sizeof(s_slashCommands[0]); i++) {
                MacroSlashCommand& slash = s_slashCommands[i];

                for (int32_t n = 1; n <= 9; n++) {
                    char name[256];
                    memcpy(name, slash.m_token, slash.m_length);
                    name[slash.m_length] = static_cast<char>('0' + n);
                    name[slash.m_length + 1] = '\0';

                    lua_pushstring(L, name);
                    lua_rawget(L, LUA_GLOBALSINDEX);
                    const char* text = lua_tostring(L, -1);
                    lua_pop(L, 1);

                    if (!text || !*text) {
                        break;
                    }

                    if (static_cast<size_t>(command.m_end - command.m_start) == SStrLen(text) && !SStrCmpUTF8I(command.m_start, text, SStrLenUTF8(text))) {
                        type = slash.m_type;
                        break;
                    }
                }
            }

            if (!type) {
                continue;
            }
        } else {
            if (*command.m_start != '#' || static_cast<size_t>(command.m_end - command.m_start) < 5 || SStrCmp(command.m_start, "#show", 5)) {
                continue;
            }

            if (static_cast<size_t>(command.m_end - command.m_start) >= 12 && !SStrCmp(command.m_start + 5, "tooltip", 7)) {
                this->m_showTooltip = 1;
            }

            type = 1;
        }

        args.SetText(args.m_start, lineEnd, '\0', nullptr);

        if (args.m_end == args.m_start) {
            continue;
        }

        if (args == "none") {
            break;
        }

        const char* c = args.m_start;

        if (type == 4 || *c == '[') {
            this->m_dynamic = 1;
        } else {
            if (type == 6 && c < args.m_end) {
                while (*c >= '0' && *c <= '9') {
                    if (++c >= args.m_end) {
                        break;
                    }
                }

                while (c < args.m_end && *c == ' ') {
                    c++;
                }
            }

            if (*c >= '0' && *c <= '9') {
                this->m_dynamic = 1;
            }
        }

        MacroCommand* entry = this->m_commands.New();
        entry->m_type = type;
        entry->m_text = args;

        if (type == 1 || !this->m_dynamic) {
            break;
        }
    }

    if (this->m_dynamic && static_cast<int32_t>(this->m_actionBarCount) > 0) {
        s_dynamicMacros.LinkToTail(this);
    } else {
        s_dynamicMacros.UnlinkNode(this);
    }

    return this->FindSpellID();
}

// VERIFY: 2141c271 auto
// OFFSET: 0x005661C0
void CGUIMacros::CountActionBarSlots(MACRONODE* macro) {
    macro->m_actionBarCount = 0;
    CGUIMacros::RemoveDynamicMacro(macro);

    for (uint32_t slot = 0; slot < 144; slot++) {
        uint32_t action = CGActionBar::m_slotActions[slot];
        uint32_t id = action && (action & 0xF0000000) == 0x40000000 ? action & 0xBFFFFFFF : 0;

        if (id == macro->m_id) {
            if (macro->m_dynamic && !macro->m_actionBarCount) {
                s_dynamicMacros.LinkToTail(macro);
            }

            macro->m_actionBarCount++;
        }
    }
}

// VERIFY: f206167d manual
// OFFSET: 0x00566240
void CGUIMacros::SortMacros() {
    memset(CGUIMacros::m_sortedMacros, 0, sizeof(CGUIMacros::m_sortedMacros));
    MACRONODE** sorted = static_cast<MACRONODE**>(alloca(sizeof(MACRONODE*) * 36));

    for (uint32_t type = 0; type < 2; type++) {
        MACRONODE** out = sorted;

        for (MACRONODE* macro = s_macros.m_fulllist.Head(); macro; macro = s_macros.m_fulllist.Next(macro)) {
            if (macro->m_type == type) {
                *out++ = macro;
            }
        }

        qsort(sorted, CGUIMacros::m_numMacros[type], sizeof(MACRONODE*), QSortMacrosByName);

        for (uint32_t i = 0; i < CGUIMacros::m_numMacros[type]; i++) {
            CGUIMacros::m_sortedMacros[type * 36 + i] = sorted[i]->m_id;
        }
    }

    FrameScript_SignalEvent(SCRIPT_EVENT_UPDATE_MACROS, nullptr);
}

static bool CanStoreRecord(uint32_t scope, const ExtendedMacros::Record& candidate) {
    size_t size = ExtendedMacros::Magic.size() + ExtendedMacros::RecordBytes(candidate);
    for (auto* node = s_macros.m_fulllist.Head(); node; node = s_macros.m_fulllist.Next(node)) {
        if (node->m_type == scope && node->m_id != candidate.id) {
            size += ExtendedMacros::RecordBytes({node->m_id, node->m_name, node->m_icon, node->m_body});
        }
    }
    return size <= ExtendedMacros::CollectionBytes;
}

// VERIFY: 8cfe58b9 auto
// OFFSET: 0x00566CD0
const char* CGUIMacros::EditMacro(uint32_t id, const char* name, const char* icon,
    std::optional<std::string_view> body) {
    HASHKEY_NONE key;
    MACRONODE* macro = s_macros.Ptr(id, key);

    if (!macro) {
        return "This macro no longer exists.";
    }
    const uint32_t scope = macro->m_type;
    if (const char* error = CollectionError(scope)) {
        return error;
    }
    char newName[sizeof(macro->m_name)];
    char newIcon[sizeof(macro->m_icon)];
    SStrCopy(newName, name ? name : macro->m_name, sizeof(newName));
    SStrCopy(newIcon, icon ? icon : macro->m_icon, sizeof(newIcon));
    if (!newName[0]) {
        return "A macro name is required.";
    }
    if (!CanStoreRecord(scope, {id, newName, newIcon, body.value_or(macro->m_body)})) {
        return "This macro collection is full. Shorten or remove another macro before saving.";
    }

    SStrCopy(macro->m_name, newName, sizeof(macro->m_name));
    SStrCopy(macro->m_icon, newIcon, sizeof(macro->m_icon));
    if (body) {
        macro->m_body.assign(*body);
        macro->Parse();
    }

    CGUIMacros::m_saveMacros |= 1u << scope;
    CGUIMacros::SaveMacros();
    CGUIMacros::SortMacros();
    CGActionBar::UpdateMacro(id);
    return nullptr;
}

// VERIFY: e32ab7eb auto
// OFFSET: 0x00566D50
const char* CGUIMacros::DeleteMacro(uint32_t id) {
    HASHKEY_NONE key;
    MACRONODE* macro = s_macros.Ptr(id, key);

    if (!macro) {
        return "This macro no longer exists.";
    }

    const uint32_t scope = macro->m_type;
    if (const char* error = CollectionError(scope)) {
        return error;
    }
    CGUIMacros::m_numMacros[scope]--;
    s_macros.Delete(macro);
    CGUIMacros::m_saveMacros |= 1u << scope;
    CGUIMacros::SaveMacros();
    CGActionBar::RemoveMacroActions(id);
    CGUIMacros::SortMacros();
    return nullptr;
}

// VERIFY: 781ecbc4 manual
// OFFSET: 0x00567520
void CGUIMacros::CheckActionBar() {
    for (MACRONODE* macro = s_macros.m_fulllist.Head(); macro; macro = s_macros.m_fulllist.Next(macro)) {
        macro->m_actionBarCount = 0;
        s_dynamicMacros.UnlinkNode(macro);
    }

    for (uint32_t i = 0; i < 144; i++) {
        uint32_t action = CGActionBar::m_slotActions[i];

        if (action && (action & 0xF0000000) == 0x40000000) {
            uint32_t id = action & 0xBFFFFFFF;

            if (id) {
                MACRONODE* macro = s_macros.Ptr(id, HASHKEY_NONE());

                if (macro) {
                    macro->IncrementActionBarCount();
                }
            }
        }
    }
}

// Import the Wrath cache once per load. The old line format cannot distinguish a final
// separator from a final newline in the macro. Preserve its established LF representation.
static bool ImportWrathMacros(std::string_view data, uint32_t scope, std::string& encoded) {
    encoded.assign(ExtendedMacros::Magic);
    while (!data.empty()) {
        const auto line = ExtendedMacros::NextLine(data);
        if (line.empty()) {
            continue;
        }
        if (line.size() > 400 || line.find('\0') != std::string_view::npos) {
            return false;
        }
        const std::string header(line);
        const char* fields = header.c_str();
        char kind[8];
        char idText[32];
        char name[64];
        char icon[256];
        SStrTokenize(&fields, kind, sizeof(kind), " ", nullptr);
        if (SStrCmpI(kind, "MACRO", sizeof(kind))) {
            // Ignore non-record lines, including obsolete comments, as Wrath did.
            continue;
        }
        SStrTokenize(&fields, idText, sizeof(idText), " ", nullptr);
        uint32_t id = 0;
        if (!ExtendedMacros::ReadUnsigned(idText, id) || !id) {
            return false;
        }
        if (id < ExtendedMacros::IDBase(scope)) {
            id += ExtendedMacros::IDBase(scope);
        }
        SStrTokenize(&fields, name, sizeof(name), " \"\r\n", nullptr);
        SStrTokenize(&fields, icon, sizeof(icon), " \"\r\n", nullptr);

        std::string body;
        bool ended = false;
        while (!data.empty()) {
            auto bodyLine = ExtendedMacros::NextLine(data);
            if (bodyLine.size() == 3 && !SStrCmpI(bodyLine.data(), "END", 3)) {
                ended = true;
                break;
            }
            if (!bodyLine.empty()) {
                body.append(bodyLine);
                body += '\n';
            }
            if (body.size() > ExtendedMacros::DefaultCharacters * 4) {
                return false;
            }
        }
        if (!ended) {
            return false;
        }
        ExtendedMacros::AppendRecord(encoded, {id, name, icon, body});
        if (encoded.size() > ExtendedMacros::CollectionBytes) {
            return false;
        }
    }
    return true;
}

void CGUIMacros::LoadMacroCollection(uint32_t scope, std::string_view data, bool readable) {
    if (!(pendingCollections & (1u << scope))) {
        return;
    }

    std::string imported;
    std::vector<ExtendedMacros::Record> records;
    if (readable && data.substr(0, ExtendedMacros::Magic.size()) != ExtendedMacros::Magic) {
        readable = ImportWrathMacros(data, scope, imported);
        data = imported;
    }
    if (!readable || !ExtendedMacros::Decode(data, scope, records)) {
        unreadableCollections |= 1u << scope;
        pendingCollections &= ~(1u << scope);
        FrameScript_SignalEvent(SCRIPT_EVENT_UI_ERROR_MESSAGE, "%s", CollectionError(scope));
        return;
    }

    uint32_t maxId = s_macroIDBase[scope];
    for (const auto& record : records) {
        auto* macro = s_macros.New(record.id, HASHKEY_NONE(), 0, 0);
        macro->m_id = record.id;
        macro->m_type = scope;
        SStrCopy(macro->m_name, std::string(record.name).c_str(), sizeof(macro->m_name));
        SStrCopy(macro->m_icon, std::string(record.icon).c_str(), sizeof(macro->m_icon));
        macro->m_body.assign(record.body);
        macro->Parse();
        maxId = std::max(maxId, record.id);
    }
    CGUIMacros::m_numMacros[scope] = static_cast<uint32_t>(records.size());
    s_maxMacroID[scope] = maxId;
    pendingCollections &= ~(1u << scope);
    CGUIMacros::SortMacros();
    CGUIMacros::UpdateMacroSpells();
    CGUIMacros::CheckActionBar();

    if (CGGameUI::s_inWorld) {
        FrameScript_SignalEvent(SCRIPT_EVENT_ACTIONBAR_SLOT_CHANGED, "%d", 0);
    }
}

// VERIFY: 1545e138 manual
// OFFSET: 0x00564D70
const char* GetMacroIcon(uint32_t index) {
    if (index >= s_macroIcons.m_count) {
        return nullptr;
    }

    return s_macroIcons.m_data[index];
}

// VERIFY: 4487aa80 manual
// OFFSET: 0x00564D90
const char* GetMacroItemIcon(uint32_t index) {
    if (index >= s_macroItemIcons.m_count) {
        return nullptr;
    }

    return s_macroItemIcons.m_data[index];
}

void CGUIMacros::RunMacro(MACRONODE* macro, const char* button) {
    if (macro) {
        CGUIMacros::RunMacroText(macro->m_body, macro->m_id, button);
    }
}

void CGUIMacros::RunMacroText(std::string_view body, uint32_t id, const char* button) {
    if (!CGGameUI::CanPerformAction(3) || body.empty()) {
        return;
    }

    static uint32_t depth = 0;
    static uint32_t commandsLeft = 0;
    static uint32_t started = 0;
    static bool extended = false;
    if (depth == 8) {
        return;
    }
    if (!depth) {
        CGUIMacros::m_state = UINT32_MAX;
        commandsLeft = ExtendedMacros::CommandBudget;
        started = OsGetAsyncTimeMs();
        extended = false;
    }
    extended = extended || body.size() > 1023 || ExtendedMacros::CharacterCount(body) > 255;

    // The existing dispatch can call back into Lua. Keep the active body independent
    // of any edit or deletion made during one of its commands.
    const std::string activeBody(body);
    std::string_view remaining = activeBody;
    std::string command;
    const auto savedID = CGUIMacros::m_runningMacroID;
    const auto savedButton = CGUIMacros::m_macroRunning;
    const bool savedExecuting = CGUIMacros::m_executing;
    ++depth;
    CGUIMacros::m_runningMacroID = id;
    CGUIMacros::m_macroRunning = button;
    CGUIMacros::m_executing = true;

    while (!remaining.empty() && CGUIMacros::m_executing && commandsLeft) {
        const auto line = ExtendedMacros::NextLine(remaining);
        if (!ExtendedMacros::IsCommand(line)) {
            continue;
        }
        // Check between commands. The command's own Lua code remains under the stock Lua policy.
        if (extended && OsGetAsyncTimeMs() - started >= 50) {
            commandsLeft = 0;
            break;
        }
        --commandsLeft;
        command.assign(line);
        FrameScript_SignalEvent(SCRIPT_EVENT_EXECUTE_CHAT_LINE, "%s", command.c_str());
    }

    --depth;
    CGUIMacros::m_runningMacroID = savedID;
    CGUIMacros::m_macroRunning = savedButton;
    CGUIMacros::m_executing = savedExecuting;
}

// VERIFY: a7a6a7f6 manual
// OFFSET: 0x005650C0
int32_t MACRONODE::FindSpellID() {
    MacroSubstring text = { nullptr, nullptr };
    MacroSubstring target = { nullptr, nullptr };
    int32_t itemId = 0;
    int32_t spellId = 0;
    uint32_t count = this->m_commands.Count();

    if (count) {
        uint32_t index;

        for (index = 0; index < count; index++) {
            MacroCommand& command = this->m_commands.m_data[index];
            text.m_start = command.m_text.m_start;
            text.m_end = command.m_text.m_end;

            if (*text.m_start != '[') {
                break;
            }

            if (EvaluateMacroOptions(text.m_start, text.m_end, &text, &target) && text.m_end != text.m_start) {
                break;
            }
        }

        if (index < count) {
            int32_t type = this->m_commands.m_data[index].m_type;

            if (type == 3) {
                text.SetText(text.m_start, text.m_end, ',', nullptr);
            } else if (type == 4) {
                MacroQueryCastSequence(&text);
            } else if (type == 6 && text.m_start < text.m_end) {
                while (*text.m_start >= '0' && *text.m_start <= '9') {
                    if (++text.m_start >= text.m_end) {
                        break;
                    }
                }

                while (text.m_start < text.m_end && *text.m_start == ' ') {
                    text.m_start++;
                }
            }

            if (*text.m_start < '0' || *text.m_start > '9') {
                char name[1024];
                text.GetText(name, sizeof(name));
                itemId = GetItemIDByName(name);

                if (itemId) {
                    spellId = 0;
                } else {
                    int32_t commandType = this->m_commands.m_data[index].m_type;

                    if (commandType != 5 && commandType != 6) {
                        spellId = GetSpellIdByName(name, &this->m_spellFlags);
                    }
                }

                if (!itemId && !spellId) {
                    spellId = -1;
                }
            } else {
                MacroSubstring first = { nullptr, nullptr };
                MacroSubstring second = { nullptr, nullptr };
                first.SetText(text.m_start, text.m_end, ' ', nullptr);
                second.SetText(first.m_end, text.m_end, '\0', nullptr);
                CGBag_C* bag = nullptr;
                int32_t slot;
                int32_t maxSlot;

                if (second.m_end == second.m_start) {
                    slot = first.ToNumber() - 1;
                    maxSlot = 22;
                    CGPlayer_C* player = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__));

                    if (player) {
                        bag = &player->m_bag;
                    }
                } else {
                    int32_t bagIndex = first.ToNumber() - 1;
                    slot = second.ToNumber() - 1;

                    if (bagIndex == -1) {
                        slot += 23;
                        maxSlot = 38;
                        CGPlayer_C* player = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__));

                        if (player) {
                            bag = &player->m_bag;
                        }
                    } else if (static_cast<uint32_t>(bagIndex) <= 3) {
                        CGObject_C* container = ClntObjMgrObjectPtr(GetBagAtIndex(bagIndex), TYPE_CONTAINER, __FILE__, __LINE__);

                        if (container) {
                            bag = container->GetBag();

                            if (bag) {
                                maxSlot = bag->m_numSlots - 1;
                            }
                        }
                    }
                }

                CGItem_C* item = (bag && slot >= 0 && slot <= maxSlot) ? bag->GetItemPointer(slot) : nullptr;

                if (item) {
                    itemId = item->m_obj->m_entryID;
                    spellId = 0;
                } else {
                    spellId = -1;
                }
            }
        }
    }

    WOWGUID guid = WOWGUID();

    if (target.m_end != target.m_start) {
        char token[256];
        target.GetText(token, sizeof(token));
        Script_GetGUIDFromToken(token, guid, false);
    }

    if (itemId != this->m_itemID || spellId != this->m_spellID || guid != this->m_target) {
        this->m_spellID = spellId;
        this->m_itemID = itemId;
        this->m_target = guid;
        return 1;
    }

    return 0;
}

// VERIFY: 4415731b manual
// OFFSET: 0x00566530
void CGUIMacros::Shutdown() {
    ClearMacroAccountDataCallbacks();
    pendingCollections = unreadableCollections = 0;
    CGUIMacros::m_saveMacros = 0;
    s_dynamicMacros.UnlinkAll();
    s_macros.Clear();

    for (uint32_t i = s_macroIcons.Count(); i; i--) {
        SMemFree(s_macroIcons.m_data[i - 1], __FILE__, __LINE__, 0);
    }

    s_macroIcons.Clear();

    for (uint32_t i = s_macroItemIcons.Count(); i; i--) {
        SMemFree(s_macroItemIcons.m_data[i - 1], __FILE__, __LINE__, 0);
    }

    s_macroItemIcons.Clear();
    CGUIMacros::m_numMacros[0] = 0;
    CGUIMacros::m_numMacros[1] = 0;
    ClearMacroConditionCache();
}

void CGUIMacros::SaveMacros() {
    for (uint32_t scope = 0; scope < 2; ++scope) {
        if (!(CGUIMacros::m_saveMacros & (1u << scope))) {
            continue;
        }

        std::vector<ExtendedMacros::Record> records;
        size_t bytes = ExtendedMacros::Magic.size();
        for (auto* macro = s_macros.m_fulllist.Head(); macro; macro = s_macros.m_fulllist.Next(macro)) {
            if (macro->m_type == scope) {
                records.push_back({macro->m_id, macro->m_name, macro->m_icon, macro->m_body});
                bytes += ExtendedMacros::RecordBytes(records.back());
            }
        }
        if (bytes > ExtendedMacros::CollectionBytes) {
            continue;
        }
        std::sort(records.begin(), records.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
        std::string data(ExtendedMacros::Magic);
        data.reserve(bytes);
        for (const auto& record : records) {
            ExtendedMacros::AppendRecord(data, record);
        }
        if (SaveMacroAccountData(scope, std::move(data))) {
            CGUIMacros::m_saveMacros &= ~(1u << scope);
        }
    }
}

const char* CGUIMacros::CreateMacro(const char* name, const char* icon, std::string_view body,
    bool perCharacter, uint32_t& id) {
    if (!name || !*name) {
        return "A macro name is required.";
    }
    const uint32_t scope = perCharacter ? 1 : 0;
    if (const char* error = CollectionError(scope)) {
        return error;
    }
    if (CGUIMacros::m_numMacros[scope] >= s_maxMacros[scope]) {
        return "All macro slots in this collection are in use.";
    }

    const uint32_t next = s_maxMacroID[scope] + 1;
    if (next - s_macroIDBase[scope] >= ExtendedMacros::CharacterIDBase) {
        return "No macro IDs remain in this collection.";
    }
    char newName[sizeof(MACRONODE::m_name)];
    char newIcon[sizeof(MACRONODE::m_icon)];
    SStrCopy(newName, name, sizeof(newName));
    SStrCopy(newIcon, icon ? icon : "", sizeof(newIcon));
    if (!CanStoreRecord(scope, {next, newName, newIcon, body})) {
        return "This macro collection is full. Shorten or remove another macro before saving.";
    }

    id = next;
    s_maxMacroID[scope] = id;
    MACRONODE* macro = s_macros.New(id, HASHKEY_NONE(), 0, 0);
    macro->m_id = id;
    macro->m_type = scope;
    SStrCopy(macro->m_name, newName, sizeof(macro->m_name));
    SStrCopy(macro->m_icon, newIcon, sizeof(macro->m_icon));
    macro->m_body.assign(body);
    macro->Parse();
    CGUIMacros::m_numMacros[scope]++;
    CGUIMacros::m_saveMacros |= 1u << scope;
    CGUIMacros::SaveMacros();
    CGUIMacros::CountActionBarSlots(macro);
    if (macro->m_itemID > 0 || macro->m_spellID > 0) {
        CGActionBar::UpdateMacro(id);
    }
    CGUIMacros::SortMacros();
    return nullptr;
}

// VERIFY: 1e57ffdd auto
// OFFSET: 0x00564900
void MacroQueryCastSequence(MacroSubstring* text) {
    lua_State* L = FrameScript_GetContext();
    lua_taintexpected++;
    const char* savedTaint = lua_tainted;

    if (lua_taintexpected && !lua_taintedclosure) {
        lua_tainted = nullptr;
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, FrameScript_GetErrorHandlerReference());
    lua_pushstring(L, "QueryCastSequence");
    lua_rawget(L, LUA_GLOBALSINDEX);
    lua_pushlstring(L, text->m_start, text->m_end - text->m_start);

    if (lua_pcall(L, 1, 3, -3)) {
        lua_settop(L, -3);
    } else {
        const char* spell = lua_tolstring(L, -2, nullptr);
        const char* item = lua_tolstring(L, -1, nullptr);

        if (spell) {
            text->SetText(spell, spell + SStrLen(spell), 0, nullptr);
        } else if (item) {
            text->SetText(item, item + SStrLen(item), 0, nullptr);
        }

        lua_settop(L, -5);
    }

    if (lua_taintexpected && !lua_taintedclosure) {
        lua_tainted = savedTaint;
    }

    if (--lua_taintexpected <= 0) {
        lua_taintexpected = 0;
    }
}
