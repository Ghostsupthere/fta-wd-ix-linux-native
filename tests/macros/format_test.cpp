// Round-trip and rejection tests for the FTA MACROS 1 collection format (WoW/Source/Ui/ExtendedMacroFormat.h).
// Build: g++ -std=c++20 -I. tests/macros/format_test.cpp -o /tmp/macro_format_test && /tmp/macro_format_test
#include "WoW/Source/Ui/ExtendedMacroFormat.h"
#include <cstdio>
using namespace ExtendedMacros;
static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static std::string Build(std::initializer_list<Record> records) {
    std::string data(Magic);
    for (const auto& r : records) AppendRecord(data, r);
    return data;
}

int main() {
    std::vector<Record> out;
    // empty collection
    CHECK(Decode(std::string(Magic), 0, out) && out.empty());
    // round trip, including blank lines, an END line and multibyte text in the body
    std::string longBody;
    for (int i = 0; i < 60; ++i) longBody += "/cast [@mouseover,help] Heal\n";
    const std::string tricky = "/say hi\n\nEND\n/say \xC3\xA9\xE2\x82\xAC\n";
    auto data = Build({{1, "Heal", "INV_Misc_QuestionMark", longBody}, {7, "Tricky", "", tricky}});
    CHECK(Decode(data, 0, out) && out.size() == 2);
    if (out.size() == 2) {
        CHECK(out[0].body == longBody && out[0].name == "Heal" && out[0].icon == "INV_Misc_QuestionMark");
        CHECK(out[1].body == tricky && out[1].icon.empty());
    }
    CHECK(CharacterCount(tricky) == 21);
    CHECK(CharacterCount(std::string_view("a\0b", 3)) == -1);
    CHECK(CharacterCount("\xC3") == -1);          // truncated sequence
    CHECK(CharacterCount("\xC0\x80") == -1);      // overlong
    CHECK(CharacterCount("\xED\xA0\x80") == -1);  // surrogate
    CHECK(CharacterCount("\xF4\x90\x80\x80") == -1); // above U+10FFFF
    // character scope uses ids at or above 0x01000000
    auto charData = Build({{CharacterIDBase + 1, "Mine", "", "/s x"}});
    CHECK(Decode(charData, 1, out) && out.size() == 1);
    CHECK(!Decode(charData, 0, out));             // account scope refuses a character id
    CHECK(!Decode(Build({{1, "Acct", "", "/s x"}}), 1, out));
    // duplicates, empty names, bad magic, truncation
    CHECK(!Decode(Build({{1, "A", "", "x"}, {1, "B", "", "y"}}), 0, out));
    CHECK(!Decode(Build({{1, "", "", "x"}}), 0, out));
    CHECK(!Decode("FTA MACROS 2\n", 0, out));
    CHECK(!Decode(data.substr(0, data.size() - 3), 0, out));
    // more records than the scope has slots
    std::string many(Magic);
    for (uint32_t i = 1; i <= 37; ++i) AppendRecord(many, {i, "M", "", "/s x"});
    CHECK(!Decode(many, 0, out));
    std::string full(Magic);
    for (uint32_t i = 1; i <= 36; ++i) AppendRecord(full, {i, "M", "", "/s x"});
    CHECK(Decode(full, 0, out) && out.size() == 36);
    // body limits: the editor limit is 2048 characters, the file reader accepts up to 4096
    CHECK(Decode(Build({{1, "L", "", std::string(2048, 'a')}}), 0, out, DefaultCharacters));
    CHECK(!Decode(Build({{1, "L", "", std::string(2049, 'a')}}), 0, out, DefaultCharacters));
    CHECK(Decode(Build({{1, "L", "", std::string(4096, 'a')}}), 0, out));
    CHECK(!Decode(Build({{1, "L", "", std::string(4097, 'a')}}), 0, out));
    // a 200-line macro (about 5800 characters) is refused even by the reader
    {
        std::string big;
        for (int i = 0; i < 200; ++i) big += "/cast [@mouseover,help] Heal\n";
        CHECK(!Decode(Build({{1, "Big", "", big}}), 0, out));
    }
    // 257 command lines do not pass the budget
    std::string lines;
    for (int i = 0; i < 256; ++i) lines += "/s a\n";
    CHECK(WithinCommandBudget(lines));
    CHECK(!WithinCommandBudget(lines + "/s a\n"));
    CHECK(WithinCommandBudget(lines + "-- comment\n# comment\n\n"));
    // NUL inside a record
    CHECK(!Decode(Build({{1, "N", "", std::string("a\0b", 3)}}), 0, out));
    // RecordBytes matches what AppendRecord writes
    std::string one; Record r{12, "Name", "Icon", "body\n"}; AppendRecord(one, r);
    CHECK(one.size() == RecordBytes(r));
    std::printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}
