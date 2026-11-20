#include "MacroConditions.h"
#include <cstring>
#include <string>
#include <string_view>
#include <iterator>
#include "Engine/Source/Event/Event.h"
#include "WoW/Source/Ui/MacroSubstring.h"
#include "WoW/Source/Ui/UIMacros.h"

#include "WoW/Source/Ui/ScriptEvents.h"
#include "WoW/Source/Ui/GameUI.h"
#include "Engine/Source/Storm/H/storm.h"
#include "WoW/Source/Ui/SpellBookFrame.h"
#include "WoW/Source/Ui/UIBindings.h"

// STATIC: 0x00C2438C
static TSHashTable<MacroConditions, HASHKEY_STR> s_macroConditions;
static size_t s_conditionCacheBytes;
static uint32_t s_conditionCacheEntries;

void ClearMacroConditionCache() {
    s_macroConditions.Clear();
    s_conditionCacheBytes = 0;
    s_conditionCacheEntries = 0;
}

struct MacroConditionDefinition {
    const char* name;
    MACROCONDITIONFUNC evaluate;
};

static const MacroConditionDefinition s_conditions[] = {
    {"combat", EvaluateCombat}, {"exists", EvaluateExists}, {"help", EvaluateHelp},
    {"harm", EvaluateHarm}, {"party", EvaluateParty}, {"raid", EvaluateRaid}, {"dead", EvaluateDead},
    {"flyable", EvaluateFlyable}, {"indoors", EvaluateIndoors}, {"outdoors", EvaluateOutdoors},
    {"swimming", EvaluateSwimming}, {"flying", EvaluateFlying}, {"mounted", EvaluateMounted},
    {"stealth", EvaluateStealth}, {"group", EvaluateGroup}, {"stance", EvaluateStance}, {"form", EvaluateStance},
    {"pet", EvaluatePet}, {"modifier", EvaluateModifier}, {"mod", EvaluateModifier}, {"button", EvaluateButton},
    {"btn", EvaluateButton}, {"actionbar", EvaluateActionBar}, {"bar", EvaluateActionBar}, {"bonusbar", EvaluateBonusBar},
    {"equipped", EvaluateEquipped}, {"worn", EvaluateEquipped}, {"channeling", EvaluateChanneling}, {"spec", EvaluateSpec},
    {"vehicleui", EvaluateVehicleUI}, {"unithasvehicleui", EvaluateUnitHasVehicleUI}, {"cursor", EvaluateCursor},
    {"known", EvaluateKnown}, {"wtalent", EvaluateWrathTalent}, {"tree", EvaluateTalentTree}, {"glyph", EvaluateGlyph},
    {"resting", EvaluateResting}, {"overridebar", EvaluateOverrideBar}, {"possessbar", EvaluatePossessBar},
    {"timewalking", EvaluateTimewalking},
};

int32_t GetMacroConditionType(std::string_view name) {
    for (size_t index = 0; index < std::size(s_conditions); ++index) {
        if (name == s_conditions[index].name) {
            return static_cast<int32_t>(index);
        }
    }
    return -1;
}

static bool MacroSpace(char c) {
    return c == ' ' || c == '\t';
}

static bool EvaluateMacroGroup(const char* start, const char* end, MacroSubstring* target) {
    const size_t length = end - start;
    char shortKey[128];
    std::string longKey;
    const char* key;
    if (length < sizeof(shortKey)) {
        memcpy(shortKey, start, length);
        shortKey[length] = '\0';
        key = shortKey;
    } else {
        longKey.assign(start, length);
        key = longKey.c_str();
    }

    MacroConditions temporary;
    MacroConditions* parsed = s_macroConditions.Ptr(key);
    if (!parsed) {
        if (s_conditionCacheEntries < 512 && s_conditionCacheBytes + length <= 64 * 1024) {
            ++s_conditionCacheEntries;
            s_conditionCacheBytes += length;
            parsed = s_macroConditions.New(key, 0, 0);
        } else {
            temporary.m_key = key;
            parsed = &temporary;
        }
        parsed->Parse();
    }

    MacroSubstring selected{};
    if (!parsed->Evaluate(&selected)) {
        return false;
    }
    const char* base = parsed->m_key.m_str;
    *target = selected.m_start
        ? MacroSubstring{start + (selected.m_start - base), start + (selected.m_end - base)} : MacroSubstring{};
    return true;
}

// Only leading bracket groups are options. A literal name or link can contain brackets.
int32_t EvaluateMacroOptions(const char* start, const char* end, MacroSubstring* result, MacroSubstring* target) {
    *target = {};
    if (!start) {
        return 1;
    }
    const char* cursor = start;
    while (true) {
        while (cursor < end && MacroSpace(*cursor)) {
            ++cursor;
        }
        bool matched = cursor == end || *cursor != '[';
        MacroSubstring selected{};
        while (cursor < end && *cursor == '[') {
            const char* open = ++cursor;
            while (cursor < end && *cursor != ']' && *cursor != ';' && *cursor != '[') {
                ++cursor;
            }
            if (cursor == end || *cursor != ']') {
                return 0;
            }
            if (!matched) {
                matched = EvaluateMacroGroup(open, cursor, &selected);
            }
            ++cursor;
            while (cursor < end && MacroSpace(*cursor)) {
                ++cursor;
            }
        }
        const char* argument = cursor;
        while (cursor < end && *cursor != ';') {
            ++cursor;
        }
        if (matched) {
            const char* tail = cursor;
            while (tail > argument && MacroSpace(tail[-1])) {
                --tail;
            }
            *target = selected;
            *result = {argument, tail};
            return 1;
        }
        if (cursor == end) {
            return 0;
        }
        ++cursor;
    }
}

// VERIFY: 77048a72 manual
// OFFSET: 0x005F0BB0
void MacroConditions::Parse() {
    const char* text = this->m_key.m_str;
    const char* end = text + SStrLen(text);

    while (text < end && MacroSpace(*text)) {
        ++text;
    }
    if (text == end) {
        return;
    }

    uint32_t count = 0;

    for (int32_t pass = 0; pass < 2; pass++) {
        if (pass == 1) {
            this->m_conditions.SetCount(count);
            count = 0;
        }

        const char* cursor = text;

        while (cursor < end) {
            while (cursor < end && MacroSpace(*cursor)) {
                cursor++;
            }

            const char* tokenEnd = cursor;

            while (tokenEnd < end && *tokenEnd != ',') {
                tokenEnd++;
            }

            const char* next = tokenEnd < end ? tokenEnd + 1 : end;

            while (tokenEnd > cursor && MacroSpace(tokenEnd[-1])) {
                tokenEnd--;
            }

            if (tokenEnd - cursor >= 7 && !SStrCmp(cursor, "target=", 7)) {
                if (pass == 1) {
                    this->m_target.m_start = cursor + 7;

                    while (this->m_target.m_start < tokenEnd && *this->m_target.m_start == ' ') {
                        this->m_target.m_start++;
                    }

                    this->m_target.m_end = tokenEnd;

                    while (this->m_target.m_end > this->m_target.m_start && this->m_target.m_end[-1] == ' ') {
                        this->m_target.m_end--;
                    }
                }
            } else if (*cursor == '@') {
                if (pass == 1) {
                    this->m_target.m_start = cursor + 1;

                    while (this->m_target.m_start < tokenEnd && *this->m_target.m_start == ' ') {
                        this->m_target.m_start++;
                    }

                    this->m_target.m_end = tokenEnd;

                    while (this->m_target.m_end > this->m_target.m_start && this->m_target.m_end[-1] == ' ') {
                        this->m_target.m_end--;
                    }
                }
            } else {
                if (pass == 1) {
                    this->m_conditions.m_data[count].Parse(cursor, tokenEnd);
                }

                count++;
            }

            cursor = next;
        }
    }
}

// VERIFY: 32005717 auto
// OFFSET: 0x005EF5C0
int32_t MacroConditions::Evaluate(MacroSubstring* target) {
    uint32_t count = this->m_conditions.m_count;

    if (count) {
        WOWGUID unit{};
        const auto token = this->m_target.m_start
            ? std::string(this->m_target.m_start, this->m_target.m_end) : std::string{};
        Script_GetGUIDFromToken(token.c_str(), unit, true);

        for (uint32_t i = 0; i < count; i++) {
            MacroCondition* condition = &this->m_conditions.m_data[i];
            if (condition->m_type < 0) {
                return 0;
            }
            int32_t result = s_conditions[condition->m_type].evaluate(&unit, &condition->m_args);

            if (condition->m_negate) {
                result = !result;
            }

            if (!result) {
                return 0;
            }
        }
    }

    *target = this->m_target;
    return 1;
}

// VERIFY: c63507a8 manual
// OFFSET: 0x005F0040
void MacroCondition::Parse(const char* start, const char* end) {
    MacroSubstring option;
    option.m_start = nullptr;
    option.m_end = nullptr;
    const char* cursor = start;

    if (end - start <= 2 || SStrCmp(start, "no", 2)) {
        this->m_negate = 0;
    } else {
        cursor = start + 2;
        this->m_negate = 1;
    }

    option.SetText(cursor, end, ':', &cursor);
    this->m_type = GetMacroConditionType(std::string_view(option.m_start, option.m_end - option.m_start));
    if (this->m_type < 0) {
        return;
    }

    if (cursor >= end || end - cursor <= 1) {
        if (option == "known" || option == "wtalent" || option == "glyph" || option == "tree") {
            this->m_type = -1;
        }
        return;
    }

    uint32_t count = 1;

    for (const char* c = cursor; c < end; c++) {
        if (*c == '/') {
            count++;
        }
    }

    this->m_args.SetCount(count);

    for (uint32_t i = 0; i < count; i++) {
        MacroSubstring* arg = &this->m_args.m_data[i];
        arg->m_start = cursor + 1;

        while (arg->m_start < end && *arg->m_start == ' ') {
            arg->m_start++;
        }

        arg->m_end = arg->m_start;

        while (arg->m_end < end && *arg->m_end != '/') {
            arg->m_end++;
        }

        cursor = arg->m_end;

        while (arg->m_end > arg->m_start && arg->m_end[-1] == ' ') {
            arg->m_end--;
        }
    }
}

// VERIFY: ecf7f26a manual
// OFFSET: 0x005EEFB0
int32_t EvaluateStance(const WOWGUID* unit, const TSGrowableArray<MacroSubstring>* args) {
    uint32_t form = CGSpellBook::GetShapeshiftForm(1);

    if (!args->m_count) {
        return form != 0;
    }

    for (uint32_t i = args->m_count; i; i--) {
        const MacroSubstring& arg = args->m_data[i - 1];
        uint32_t value = 0;

        for (const char* c = arg.m_start; c < arg.m_end && *c >= '0' && *c <= '9'; c++) {
            value = *c + 10 * value - '0';
        }

        if (value == form) {
            return 1;
        }
    }

    return 0;
}

// VERIFY: aa9e3f6d manual
// OFFSET: 0x005EF040
int32_t EvaluateModifier(const WOWGUID* unit, const TSGrowableArray<MacroSubstring>* args) {
    CGUIBindings* bindings = CGUIBindings::s_bindings;

    if (!args->m_count) {
        return bindings->IsModifiedClick(nullptr);
    }

    for (uint32_t i = args->m_count; i; i--) {
        const MacroSubstring& arg = args->m_data[i - 1];
        const std::string_view modifier(arg.m_start, arg.m_end - arg.m_start);
        static constexpr const char* sides[] = {"lshift", "rshift", "lctrl", "rctrl", "lalt", "ralt"};
        static constexpr KEY keys[] = {KEY_LSHIFT, KEY_RSHIFT, KEY_LCONTROL, KEY_RCONTROL, KEY_LALT, KEY_RALT};
        for (size_t side = 0; side < std::size(sides); ++side) {
            if (modifier == sides[side] && (bindings->m_keyStateOverride
                ? (bindings->m_heldModifiers & (1u << side)) != 0 : EventIsKeyDown(keys[side]) != 0)) {
                return 1;
            }
        }
        char action[1024];
        uint32_t length = arg.m_end - arg.m_start;

        if (length >= sizeof(action)) {
            continue;
        }

        memcpy(action, arg.m_start, length);
        action[length] = '\0';

        if (bindings->IsModifiedClick(action)) {
            return 1;
        }
    }

    return 0;
}
