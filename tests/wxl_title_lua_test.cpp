#include "Engine/Source/lua-5.1/src/lua.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/lua-5.1/src/lualib.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "WoW/Source/Ui/Linux/LuaStringCompatibility.h"

static lua_State* context;
lua_State* FrameScript_GetContext() { return context; }
void FrameScript_RegisterFunction(const char* name, lua_CFunction function) {
    lua_pushcfunction(context, function); lua_setglobal(context, name);
}

uint64_t OsGetAsyncTimeClocks() { return 0; }
int64_t OsGetAsyncClocksPerSecond() { return 1; }
void OsValidateFunctionPointer(const void* pointer) { assert(pointer); }

struct PlayerFields { uint64_t m_playerFieldKnownTitles[3]{}; uint8_t m_playerBytes3[4]{}; } fields;
struct CGPlayer_C { PlayerFields* m_player = &fields; } player;
static bool playerPresent = true;
constexpr int TYPE_PLAYER = 1;
static int ClntObjMgrGetActivePlayer() { return 0; }
static void* ClntObjMgrObjectPtr(int, int, const char*, int) { return playerPresent ? &player : nullptr; }
struct CharTitlesRec { int m_id; int m_maskID; const char* m_name; const char* m_name1; };
struct TitleDB {
    int m_numRecords = 2;
    std::vector<CharTitlesRec> rows{{60104, 5, "Captain %s", "Captain %s"}, {77, 191, "%s the Brave", "%s the Brave"}};
    CharTitlesRec* GetRecordByIndex(int i) { return &rows[i]; }
} g_charTitlesDB;
static size_t SStrLen(const char* text) { return std::strlen(text); }
#include "WoW/Source/Ui/Linux/WxlTitleScripts.inl"

int main(int argc, char** argv) {
    auto* L = luaL_newstate(); assert(L); luaopen_base(L); lua_settop(L, 0);
    context = L;
    lua_newtable(L); lua_setglobal(L, "string");
    WxlRegisterLuaStringCompatibility();
    lua_pushcfunction(L, Script_IsTitleKnown); lua_setglobal(L, "IsTitleKnown");
    lua_pushcfunction(L, Script_GetTitleName); lua_setglobal(L, "GetTitleName");
    lua_pushcfunction(L, Script_GetNumTitles); lua_setglobal(L, "GetNumTitles");
    fields.m_playerFieldKnownTitles[0] = 1ull << 5;
    fields.m_playerFieldKnownTitles[2] = 1ull << 63;
    const char* check = R"(
        assert(type(IsTitleKnown(1)) == 'number' and IsTitleKnown(1) == 0)
        assert(IsTitleKnown(5) == 1 and IsTitleKnown(191) == 1)
        assert(GetNumTitles() == 191) -- Sparse record ID 60104 is not the mask bound.
        assert(IsTitleKnown(0) == 0 and IsTitleKnown(192) == 0)
        assert(IsTitleKnown(10000) == 0 and IsTitleKnown(-1) == 0)
        assert(IsTitleKnown(0/0) == 0 and IsTitleKnown(1/0) == 0 and IsTitleKnown(1e40) == 0)
        assert(GetTitleName(0) == '')
        local count = 0
        -- PaperDoll's branch must skip sparse/unknown IDs before trimming.
        for id = 1, 10000 do
            if IsTitleKnown(id) ~= 0 then
                local text = strtrim(GetTitleName(id))
                assert(type(text) == 'string')
                assert(#text > 0)
                count = count + 1
            end
        end
        assert(count == 2)
        assert(not pcall(strtrim)) -- Invalid no-argument calls still raise errors.
    )";
    assert(luaL_dostring(L, check) == 0);
    // Also execute the actual shipped function up to its first layout branch.
    // This retains its enumeration/filter/trim code, rather than restating it.
    if (argc > 1) {
        std::ifstream input(argv[1]); assert(input);
        std::string source((std::istreambuf_iterator<char>(input)), {});
        const auto begin = source.find("function PlayerTitleFrame_UpdateTitles()");
        const auto end = source.find("if ( titleCount < 2 ) then", begin);
        assert(begin != std::string::npos && end != std::string::npos);
        source = source.substr(begin, end - begin) + "return playerTitles, titleCount end";
        assert(luaL_dostring(L, R"(
            PlayerTitleFrame = {}
            local text = { SetText = function() end, GetWidth = function() return 30 end }
            PlayerTitlePickerScrollFrame = { buttons = {{ text = text }} }
            function GetCurrentTitle() return 191 end
        )") == 0);
        assert(luaL_dostring(L, source.c_str()) == 0);
        assert(luaL_dostring(L, R"(
            local titles, count = PlayerTitleFrame_UpdateTitles()
            assert(count == 3 and #titles == 3)
            assert(titles[2].id == 5 and titles[3].id == 191)
            assert(titles[2].name == 'Captain' and titles[3].name == 'the Brave')
            assert(PlayerTitleFrame.selected == 191)
        )") == 0);
    }
    if (argc > 2) {
        // Installed WDBC uses sparse record IDs but dense mask IDs. Keep its
        // string block alive while production GetTitleName reads the records.
        std::ifstream input(argv[2], std::ios::binary); assert(input);
        std::string dbc((std::istreambuf_iterator<char>(input)), {});
        auto word = [&](size_t offset) {
            assert(offset + 4 <= dbc.size()); uint32_t value;
            std::memcpy(&value, dbc.data() + offset, 4); return value;
        };
        assert(dbc.substr(0,4) == "WDBC" && word(8) == 37 && word(12) == 148);
        const auto records = word(4), stride = word(12);
        const size_t strings = 20 + size_t(records) * stride;
        assert(strings + word(16) == dbc.size());
        g_charTitlesDB.rows.clear();
        fields = {};
        for (uint32_t i = 0; i < records; ++i) {
            const size_t start = 20 + size_t(i) * stride;
            const auto mask = word(start + 36 * 4);
            const auto male = word(start + 2 * 4), female = word(start + 19 * 4);
            assert(strings + male < dbc.size() && strings + female < dbc.size());
            g_charTitlesDB.rows.push_back({int(word(start)), int(mask),
                dbc.data() + strings + male, dbc.data() + strings + female});
            assert(mask < 192);
            fields.m_playerFieldKnownTitles[mask >> 6] |= 1ull << (mask & 63);
        }
        g_charTitlesDB.m_numRecords = int(records);
        assert(luaL_dostring(L, R"(
            function GetCurrentTitle() return 39 end
            assert(GetNumTitles() == 189)
            local titles, count = PlayerTitleFrame_UpdateTitles()
            assert(count == 190 and #titles == 190)
            assert(PlayerTitleFrame.selected == 39)
            for i=2,#titles do assert(#titles[i].name > 0) end
        )") == 0);
        fields = {};
        assert(luaL_dostring(L, R"(
            local titles, count = PlayerTitleFrame_UpdateTitles()
            assert(count == 1 and #titles == 1 and PlayerTitleFrame.selected == -1)
        )") == 0);
    }
    playerPresent = false;
    assert(luaL_dostring(L, "assert(IsTitleKnown(5) == 0)") == 0);
    lua_close(L);
}
