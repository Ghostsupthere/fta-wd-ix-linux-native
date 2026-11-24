#include "WoW/Source/WorldClient/WdlBounds.h"
#include "Engine/Source/BLPFile/SourceBounds.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <vector>

struct lua_State { double number; };
double lua_tonumber(lua_State* state, int) { return state->number; }
struct Faction {};
struct CGReputationInfo {
    static inline uint32_t m_numFactions = 0;
    static inline struct { Faction** m_data; uint32_t m_count; } m_factionList{};
};
struct CGRuneInfo {
    static inline uint32_t m_numRunes = 0, m_predictedRunes = 0, m_runeCooldowns[8]{};
    static inline int32_t m_runes[8]{}, m_baseRunes[8]{};
    static bool IsRuneIndexValid(int32_t);
    static int32_t GetRuneTypeByIndex(int32_t, bool);
    static uint32_t GetRuneRechargeTime(int32_t);
};
int ClntObjMgrGetActivePlayer() { return 1; }
bool ClntObjMgrObjectPtr(int, int, const char*, int) { return true; }
constexpr int TYPE_PLAYER = 1;

using NETMESSAGE = unsigned;
struct C2Vector { float x = 0, y = 0; };
struct WOWGUID { uint64_t m_guid = 0; };
struct CGBattlefieldInfo {
    static inline uint32_t m_numPlayerPositions = 0;
    static inline C2Vector m_playerPositions[40]{}, m_flagPositions[2]{};
    static inline WOWGUID m_playerPositionGuids[40]{}, m_flagCarriers[2]{};
};
struct CDataStore {
    std::vector<char> bytes;
    uint32_t m_size = 0, m_read = 0;
    template <typename T> void Get(T& value) {
        assert(m_read <= m_size && sizeof(T) <= m_size - m_read);
        std::memcpy(&value, bytes.data() + m_read, sizeof(T));
        m_read += sizeof(T);
    }
    template <typename T> void Put(T value) {
        bytes.resize(bytes.size() + sizeof(T));
        std::memcpy(bytes.data() + m_size, &value, sizeof(T));
        m_size += sizeof(T);
    }
    void Position(uint64_t guid, float x, float y) { Put(guid); Put(x); Put(y); }
};

struct SFileImpl {};
struct SFile {
    static bool OpenEx(void*, const char*, bool, SFileImpl** file) {
        static SFileImpl instance;
        *file = &instance;
        return true;
    }
};
int checkCalls = 0, closeCalls = 0;
uint32_t suppliedSize = 0, suppliedRead = 0;
bool readSuccess = true;
uint32_t SFileGetFileSize(SFileImpl*, void*) { return suppliedSize; }
bool SFileReadFile(SFileImpl*, void*, uint32_t, uint32_t* read, void*, int) { *read = suppliedRead; return readSuccess; }
void SFileCloseFile(SFileImpl*) { ++closeCalls; }
void DebugValidate(const char*) {}
void SErrSetLastError(int) {}
constexpr int STORM_ERROR_INVALID_PARAMETER = 1;
struct ImageBuffer {
    std::vector<char> bytes;
    void SetCount(uint32_t count) { bytes.resize(count); }
    char* Ptr() { return bytes.data(); }
} s_blpFileLoadBuffer;
struct CBLPFile {
    int32_t Open(const char*, int32_t);
    void Close() {}
    int Source(void*, size_t) { ++checkCalls; return 1; }
};
#include "batch2.inc"

static void Put32(std::vector<char>& bytes, size_t offset, uint32_t value) {
    assert(offset + 4 <= bytes.size());
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

int main(int argc, char** argv) {
    Faction f;
    Faction* list[]{&f, nullptr};
    CGReputationInfo::m_factionList = {list, 2};
    CGReputationInfo::m_numFactions = 3;
    lua_State input{1};
    assert(GetFactionListIndex(&input) == 0);
    for (const double value : {0., 1.2, 2., 3., -1., std::numeric_limits<double>::infinity(),
                              std::numeric_limits<double>::quiet_NaN()}) {
        input.number = value;
        assert(GetFactionListIndex(&input) == UINT32_MAX);
    }
    assert(!CGRuneInfo::IsRuneIndexValid(-1) && !CGRuneInfo::IsRuneIndexValid(8));
    CGRuneInfo::m_numRunes = 6;
    CGRuneInfo::m_runes[5] = 2;
    CGRuneInfo::m_runeCooldowns[5] = 1800;
    assert(CGRuneInfo::GetRuneTypeByIndex(5, false) == 2);
    assert(CGRuneInfo::GetRuneRechargeTime(5) == 1800);
    assert(!CGRuneInfo::GetRuneRechargeTime(-1) && !CGRuneInfo::GetRuneTypeByIndex(8, false));

    CDataStore message;
    message.Put(uint32_t{1});
    message.Position(77, 0.25f, 0.75f);
    message.Put(uint32_t{2});
    message.Position(100, 0.1f, 0.2f);
    message.Position(200, 0.3f, 0.4f);
    assert(Packet_MSG_BATTLEGROUND_PLAYER_POSITIONS(nullptr, 0, 0, &message));
    assert(CGBattlefieldInfo::m_playerPositions[0].x == 0.25f);
    assert(CGBattlefieldInfo::m_flagCarriers[0].m_guid == 200 && CGBattlefieldInfo::m_flagCarriers[1].m_guid == 100);
    for (uint32_t size = 0; size < message.bytes.size(); ++size) {
        message.m_size = size;
        message.m_read = 0;
        assert(!Packet_MSG_BATTLEGROUND_PLAYER_POSITIONS(nullptr, 0, 0, &message));
        assert(CGBattlefieldInfo::m_flagCarriers[0].m_guid == 200 && CGBattlefieldInfo::m_numPlayerPositions == 1);
    }
    CDataStore empty;
    empty.Put(uint32_t{0});
    empty.Put(uint32_t{0});
    assert(Packet_MSG_BATTLEGROUND_PLAYER_POSITIONS(nullptr, 0, 0, &empty));
    assert(CGBattlefieldInfo::m_numPlayerPositions == 0 && CGBattlefieldInfo::m_flagCarriers[0].m_guid == 0);

    CBLPFile blp;
    suppliedSize = 2;
    assert(!blp.Open("fixture", 0) && checkCalls == 0 && closeCalls == 1);
    suppliedSize = sizeof(BLPHeader);
    suppliedRead = suppliedSize - 1;
    assert(!blp.Open("fixture", 0) && checkCalls == 0 && closeCalls == 2);
    suppliedRead = suppliedSize;
    readSuccess = false;
    assert(!blp.Open("fixture", 0) && checkCalls == 0 && closeCalls == 3);
    readSuccess = true;
    assert(blp.Open("fixture", 0) && checkCalls == 1 && closeCalls == 4);

    BLPHeader source{};
    source.m_magic = BLP_MAGIC_BLP2;
    source.m_formatVersion = 1;
    source.m_width = source.m_height = 1;
    source.m_colorEncoding = COLOR_RAW;
    source.m_mipOffsets[0] = BlpSourceBounds::FixedHeaderBytes;
    source.m_mipSizes[0] = 4;
    std::array<char, BlpSourceBounds::FixedHeaderBytes + 4> raw{};
    std::memcpy(raw.data(), &source, BlpSourceBounds::FixedHeaderBytes);
    BLPHeader parsed{};
    assert(BlpSourceBounds::ReadHeader(raw.data(), raw.size(), parsed));
    assert(parsed.m_mipSizes[0] == 4);
    assert(!BlpSourceBounds::ReadHeader(raw.data(), raw.size() - 1, parsed));
    source.m_colorEncoding = COLOR_PAL;
    std::memcpy(raw.data(), &source, BlpSourceBounds::FixedHeaderBytes);
    assert(!BlpSourceBounds::ReadHeader(raw.data(), raw.size(), parsed));

    const size_t tileTable = 4096 * 4;
    const size_t height = 20 + tileTable;
    std::vector<char> file(height + 8 + 1090);
    Put32(file, 0, 0x4D564552);
    Put32(file, 4, 4);
    Put32(file, 12, 0x4D414F46);
    Put32(file, 16, tileTable);
    Put32(file, 20, height);
    Put32(file, height, 0x4D415245);
    Put32(file, height + 4, 1090);
    assert(WdlBounds::Validate(file.data(), file.size(), 64)); // MARE can end at EOF
    assert(!WdlBounds::Validate(file.data(), file.size() - 1, 64));
    const size_t holes = file.size();
    file.resize(holes + 8 + 32);
    Put32(file, holes, 0x4D41484F);
    Put32(file, holes + 4, 32);
    assert(WdlBounds::Validate(file.data(), file.size(), 64));
    assert(!WdlBounds::Validate(file.data(), file.size() - 1, 64));
    Put32(file, 20, UINT32_MAX);
    assert(!WdlBounds::Validate(file.data(), file.size(), 64));
    for (int i = 1; i < argc; ++i) {
        std::ifstream source(argv[i], std::ios::binary);
        assert(source);
        const std::vector<char> payload((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>{});
        assert(WdlBounds::Validate(payload.data(), payload.size(), 64));
    }
}
