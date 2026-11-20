#include "ExtendedMacroFormat.h"
#include "MacroConditions.h"
#include "ScriptEvents.h"
#include "SpellBookFrame.h"
#include "WoW/Source/DB/DBClient/DBClient.h"
#include "WoW/Source/DB/Rec/SpellRec.h"
#include "WoW/Source/Magic/MagicClient/Spell_C.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/Storm/H/storm.h"
#include <string>

namespace {

std::string_view Trim(std::string_view value) {
    const auto first = value.find_first_not_of(" \t");
    return first == value.npos ? std::string_view{} : value.substr(first, value.find_last_not_of(" \t") - first + 1);
}

std::string_view Take(std::string_view& value, char separator) {
    const auto end = value.find(separator);
    const auto field = value.substr(0, end);
    value.remove_prefix(end == value.npos ? value.size() : end + 1);
    return field;
}

bool IsMacroTarget(std::string_view value) {
    if (value == "cursor") {
        return true;
    }
    if (value.empty() || value.size() >= 96) {
        return false;
    }
    char token[96]{};
    value.copy(token, value.size());
    WOWGUID guid{};
    return Script_GetGUIDFromToken(token, guid, false);
}

struct Diagnostics {
    lua_State* lua;
    int count = 0;
    int line = 0;

    void Add(const char* code, const char* message, bool warning = false) {
        if (count >= 48) {
            return;
        }
        lua_createtable(lua, 0, 4);
        lua_pushinteger(lua, line);
        lua_setfield(lua, -2, "line");
        lua_pushstring(lua, code);
        lua_setfield(lua, -2, "code");
        lua_pushstring(lua, message);
        lua_setfield(lua, -2, "message");
        lua_pushstring(lua, warning ? "warning" : "error");
        lua_setfield(lua, -2, "severity");
        lua_rawseti(lua, -2, ++count);
    }
};

void ValidateArguments(std::string_view condition, std::string_view args, Diagnostics& result) {
    if (condition != "known" && condition != "wtalent" && condition != "glyph" && condition != "tree") {
        return;
    }
    do {
        const auto argument = Trim(Take(args, '/'));
        if (argument.empty()) {
            result.Add("missing-argument", "This condition needs a value.");
            return;
        }
        auto number = argument;
        if (condition == "wtalent") {
            number = argument.substr(0, argument.find(':'));
            if (number.size() < argument.size()) {
                uint32_t rank = 0;
                if (!ExtendedMacros::ReadUnsigned(argument.substr(number.size() + 1), rank) || rank < 1 || rank > 9) {
                    result.Add("bad-rank", "Use a talent rank from 1 to 9.");
                }
            }
        }
        uint32_t id = 0;
        if (!ExtendedMacros::ReadUnsigned(number, id) || !id || id > INT32_MAX) {
            if (condition != "known" || number.find_first_not_of("0123456789") == number.npos) {
                result.Add("bad-number", "Use a valid positive number in this condition.");
            }
        } else if (condition == "tree") {
            if (id > 3) {
                result.Add("bad-tree", "Choose talent tree 1, 2, or 3.");
            }
        } else {
            SpellRec spell;
            if (!g_spellDB.GetRecord(static_cast<int32_t>(id), &spell)) {
                result.Add("unknown-spell-id", "That spell ID is absent from the client spell data.");
            }
        }
    } while (!args.empty());
}

void ValidateGroup(std::string_view group, std::string_view& target, Diagnostics& result) {
    if (Trim(group).empty()) {
        return;
    }
    do {
        auto option = Trim(Take(group, ','));
        if (!option.empty() && (option.front() == '@' || option.substr(0, 7) == "target=")) {
            target = Trim(option.substr(option.front() == '@' ? 1 : 7));
            if (!IsMacroTarget(target)) {
                result.Add("bad-target", "Unknown macro target.");
            }
            continue;
        }

        auto name = Trim(Take(option, ':'));
        if (name.substr(0, 2) == "no" && name.size() > 2) {
            name.remove_prefix(2);
        }
        if (GetMacroConditionType(name) < 0) {
            result.Add("unknown-option", "Unknown or unsupported macro condition.");
            continue;
        }
        ValidateArguments(name, option, result);
    } while (!group.empty());
}

void ValidateCast(std::string_view argument, bool cursor, Diagnostics& result) {
    argument = Trim(argument);
    if (argument.empty()) {
        result.Add("missing-action", "Choose a spell or item for this branch.");
        return;
    }
    // Numeric arguments in /cast and /use may be inventory slots or items.
    uint32_t id = 0;
    if (!cursor || ExtendedMacros::ReadUnsigned(argument, id) || argument.size() >= 256 ||
        argument.find('|') != argument.npos) {
        return;
    }
    char name[256]{};
    argument.copy(name, argument.size());
    int32_t pet = 0;
    const auto spell = GetSpellIdByName(name, &pet);
    if (spell && (pet || !Spell_C_CanPlaceMacroGroundTarget(spell))) {
        result.Add("not-ground-spell", "That spell does not accept a cursor ground position.");
    }
}

}

int32_t Script_ValidateMacroBody(lua_State* L) {
    size_t bytes = 0;
    const char* text = luaL_checklstring(L, 1, &bytes);
    std::string_view body(text, bytes);
    lua_newtable(L);
    Diagnostics result{L};
    if (bytes > ExtendedMacros::SupportedCharacters * 4) {
        result.Add("length", "The macro is over the character limit.");
        return 1;
    }
    const auto characters = ExtendedMacros::CharacterCount(body);
    if (characters < 0) {
        result.Add("encoding", "Macro text must be valid UTF-8 without NUL bytes.");
        return 1;
    }
    if (static_cast<uint32_t>(characters) > ExtendedMacros::DefaultCharacters) {
        result.Add("length", "The macro is over the character limit.");
    }
    if (!ExtendedMacros::WithinCommandBudget(body)) {
        result.Add("command-count", "A single macro can contain up to 256 command lines.");
    }

    while (!body.empty() && result.count < 48) {
        result.line++;
        auto line = Trim(ExtendedMacros::NextLine(body));
        if (line.empty()) {
            continue;
        }
        const size_t split = line.find_first_of(" \t");
        const auto command = line.substr(0, split);
        const bool cast = command == "/cast" || command == "/use";
        const bool sequence = command == "/castsequence" || command == "/castrandom" || command == "/userandom";
        const bool options = cast || sequence || command == "#show" || command == "#showtooltip" ||
            command == "/stopmacro" || command == "/stopcasting" || command == "/target" ||
            command == "/focus" || command == "/assist" || command == "/petattack" ||
            command == "/equip" || command == "/equipslot" || command == "/equipset" || command == "/click";
        if (!options) {
            continue;
        }
        if (split == line.npos) {
            if (cast || sequence) {
                result.Add("missing-action", "Choose a spell or item for this command.");
            }
            continue;
        }
        line.remove_prefix(split + 1);
        do {
            auto branch = Trim(Take(line, ';'));
            bool cursor = false;
            while (!branch.empty() && branch.front() == '[') {
                const auto close = branch.find(']');
                if (close == branch.npos || branch.substr(1, close - 1).find('[') != branch.npos) {
                    result.Add("unclosed-options", "Close each condition group with a matching bracket.");
                    branch = {};
                    break;
                }
                std::string_view target;
                ValidateGroup(branch.substr(1, close - 1), target, result);
                cursor |= target == "cursor";
                branch = Trim(branch.substr(close + 1));
            }
            if (cast) {
                ValidateCast(branch, cursor, result);
            } else if (sequence && branch.empty()) {
                result.Add("missing-sequence", "A spell or item list is required.");
            }
        } while (!line.empty());
    }
    return 1;
}
