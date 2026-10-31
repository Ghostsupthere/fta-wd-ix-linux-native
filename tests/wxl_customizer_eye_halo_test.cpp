#include "Engine/Source/ModernAssets/WxlCustomizer/EyeHaloChoice.hpp"
#include <cassert>

int main() {
    using namespace wxl::customizer::halo;

    // Keys are hex of the lowercased UTF-16 text; case and empty parts are handled.
    assert(Key("A", "R", "N") == "0061" "007c" "0072" "007c" "006e");
    assert(Key("ACC", "Realm", "Nuit") == Key("acc", "realm", "nuit"));
    assert(Key("a", "r", "\xC3\x89lan") == Key("a", "r", "\xC3\xA9lan"));   // É and é
    assert(Key("", "r", "n").empty() && Key("a", "", "n").empty() && Key("a", "r", "").empty());
    assert(Key("a", "r", "\xFF").empty() && Key("a", "r", "\xC0\x80").empty() && Key("a", "r", "\xE2\x82").empty());
    assert(Key("a", "r", "\xF0\x9F\x98\x80") == "0061007c0072007cd83dde00");   // surrogate pair

    // Entries: exact shape, supported race/theme/sex, not from the future, at most a week old.
    unsigned theme = 99, race = 99, sex = 99;
    const uint64_t now = 2000000000;
    assert(ParseEntry(FormatEntry(3, now - 10, 23, 1), theme, race, sex, now) && theme == 3 && race == 23 && sex == 1);
    assert(ParseEntry(FormatEntry(0, now, 10, 0), theme, race, sex, now));
    assert(ParseEntry(FormatEntry(5, now - kMaxAgeSeconds, 25, 1), theme, race, sex, now));
    assert(!ParseEntry(FormatEntry(5, now - kMaxAgeSeconds - 1, 25, 1), theme, race, sex, now));
    assert(!ParseEntry(FormatEntry(1, now + 1, 25, 1), theme, race, sex, now));
    assert(!ParseEntry(FormatEntry(6, now, 25, 1), theme, race, sex, now));
    assert(!ParseEntry(FormatEntry(1, now, 11, 1), theme, race, sex, now));
    assert(!ParseEntry(FormatEntry(1, now, 25, 2), theme, race, sex, now));
    assert(!ParseEntry("2|1|1999999999|23|1", theme, race, sex, now));
    assert(!ParseEntry("1|1|1999999999|23|1x", theme, race, sex, now));
    assert(!ParseEntry("1|1|1999999999|23", theme, race, sex, now));
    assert(!ParseEntry("1|1|1999999999|23|1|7", theme, race, sex, now));
    assert(!ParseEntry("1||1999999999|23|1", theme, race, sex, now));
    assert(!ParseEntry("1|1|99999999999999999999|23|1", theme, race, sex, now));
    assert(!ParseEntry("", theme, race, sex, now));

    // INI editing: other sections and keys survive, a missing section is created, null deletes.
    std::string ini = "[Other]\r\nkeep=1\r\n";
    ini = IniSet(ini, "CreatedCharacters", "aa", "v1");
    assert(IniGet(ini, "CreatedCharacters", "aa") == "v1" && IniGet(ini, "Other", "keep") == "1");
    ini = IniSet(ini, "CreatedCharacters", "bb", "v2");
    ini = IniSet(ini, "CreatedCharacters", "aa", "v3");
    assert(IniGet(ini, "CreatedCharacters", "aa") == "v3" && IniGet(ini, "CreatedCharacters", "bb") == "v2");
    ini = IniSet(ini, "CreatedCharacters", "aa", nullptr);
    assert(!IniGet(ini, "CreatedCharacters", "aa") && IniGet(ini, "CreatedCharacters", "bb") == "v2");
    assert(IniGet(ini, "Other", "keep") == "1" && !IniGet(ini, "Other", "bb"));
    assert(IniSet("", "S", "k", nullptr).empty());
    assert(IniSet("[S]\nk=1\n", "S", "k", nullptr) == "[S]\n");
    return 0;
}
