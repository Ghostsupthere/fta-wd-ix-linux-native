#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <unordered_set>
#include <vector>

struct WOWGUID {
    uint64_t m_guid = 0;
    bool operator==(WOWGUID rhs) const { return m_guid == rhs.m_guid; }
};
using NETMESSAGE = unsigned;
struct CDataStore {
    std::vector<uint8_t> bytes;
    uint32_t m_read = 0, m_size = 0;

    template<typename T> CDataStore& Get(T& value) {
        if (m_read > m_size || sizeof(T) > m_size - m_read) {
            m_read = m_size + 1;
        } else {
            std::memcpy(&value, bytes.data() + m_read, sizeof(T));
            m_read += sizeof(T);
        }
        return *this;
    }

    CDataStore& GetString(char* value, size_t size) {
        for (size_t i = 0; i < size && m_read < m_size; ++i) {
            value[i] = static_cast<char>(bytes[m_read++]);
            if (value[i] == '\0') return *this;
        }
        m_read = m_size + 1;
        value[0] = '\0';
        return *this;
    }

    template<typename T> void Put(T value) {
        const size_t start = bytes.size();
        bytes.resize(start + sizeof(T));
        std::memcpy(bytes.data() + start, &value, sizeof(T));
        m_size = bytes.size();
    }

    void Text(const char* value) {
        bytes.insert(bytes.end(), value, value + std::strlen(value) + 1);
        m_size = bytes.size();
    }
};

struct Scene;
struct CM2Model {
    Scene* m_scene = nullptr;
    CM2Model* m_attachParent = nullptr;
    unsigned releases = 0;
    void AttachToParent(CM2Model* parent, int, void*, int) { m_attachParent = parent; }
    void DetachFromParent() { m_attachParent = nullptr; }
    void Release() { ++releases; }
};
struct Scene {
    CM2Model* loaded = nullptr;
    unsigned attempts = 0;
    CM2Model* CreateModel(const char*, int) { ++attempts; return loaded; }
};
struct World { static inline Scene* s_m2Scene = nullptr; };
struct CCharacterComponent {
    static inline unsigned frees = 0;
    static void FreeComponent(CCharacterComponent*) { ++frees; }
};
struct SpellVisualEffectNameRec { const char* m_fileName; };
constexpr int SPECIALEFFECT_LOOTART = 1;
const SpellVisualEffectNameRec* lootData = nullptr;
const SpellVisualEffectNameRec* GetSpecialSpellVisualEffectNameRec(int) { return lootData; }
WOWGUID ClntObjMgrGetActivePlayer() { return {1}; }
struct CGGameUI {
    static inline WOWGUID corpse;
    static void SetActiveCorpse(WOWGUID guid) { corpse = guid; }
};
struct CGCorpse_C {
    CM2Model* m_worldModel = nullptr;
    CM2Model* m_lootEffect = nullptr;
    CCharacterComponent* m_component = nullptr;
    unsigned m_corpseFlags = 1, fades = 0, additions = 0, initializations = 0;
    struct {
        uint32_t m_corpseFieldDynamicFlags = 1, m_corpseFieldFlags = 0;
        WOWGUID m_corpseFieldOwner{1};
    } corpse;
    struct { WOWGUID m_guid{5}; } object;
    decltype(object)* m_obj = &object;
    decltype(corpse)* m_corpse = &corpse;
    bool hasFilename = true;
    bool GetModelFileName(const char** name) { *name = "corpse.m2"; return hasFilename; }
    void Fadeout() { ++fades; }
    void SetModel(CM2Model* model) { m_worldModel = model; }
    void ShowLootEffect();
    void AddWorldObject() { ++additions; }
    void OnFlagChanged(uint32_t);
    void InitializeModel() {
#include "corpse-init.inc"
        assert(model);
        ++initializations;
    }
};
#include "corpse-loot.inc"
#include "corpse-replace.inc"

void TestCorpse() {
    CGCorpse_C corpse;
    corpse.InitializeModel();
    assert(corpse.initializations == 0 && CGGameUI::corpse.m_guid == 5);
    corpse.ShowLootEffect();
    assert(!corpse.m_lootEffect);
    CM2Model original, replacement, loot, newLoot;
    corpse.m_worldModel = &original;
    corpse.InitializeModel();
    assert(corpse.initializations == 1);
    corpse.ShowLootEffect();
    assert(!corpse.m_lootEffect);
    loot.m_attachParent = &original;
    corpse.m_lootEffect = &loot;
    CCharacterComponent component;
    corpse.m_component = &component;

    corpse.OnFlagChanged(0);
    Scene scene;
    World::s_m2Scene = &scene;
    corpse.hasFilename = false;
    corpse.OnFlagChanged(0);
    assert(scene.attempts == 0);
    corpse.hasFilename = true;
    corpse.OnFlagChanged(0);
    assert(scene.attempts == 1 && corpse.m_worldModel == &original && corpse.m_corpseFlags == 1);
    assert(corpse.m_lootEffect == &loot && loot.releases == 0 && corpse.m_component == &component && corpse.fades == 0);
    scene.loaded = &replacement;
    Scene lootScene;
    lootScene.loaded = &newLoot;
    replacement.m_scene = &lootScene;
    const SpellVisualEffectNameRec effect{"loot.m2"};
    lootData = &effect;
    corpse.OnFlagChanged(0);
    assert(corpse.m_worldModel == &replacement && corpse.m_corpseFlags == 0);
    assert(corpse.fades == 1 && corpse.additions == 1);
    assert(corpse.m_lootEffect == &newLoot && newLoot.m_attachParent == &replacement);
    assert(!loot.m_attachParent && loot.releases == 1 && replacement.releases == 1 && CCharacterComponent::frees == 1);
    World::s_m2Scene = nullptr;
    lootData = nullptr;
}

struct CGUnit_C {
    struct Object { WOWGUID m_guid; uint32_t m_entryID = 0; } obj;
    Object* m_obj = &obj;
    struct Unit { uint32_t m_unitCreatedBySpell = 0; } unit;
    Unit* m_unit = &unit;
    uint32_t m_clientFlags = 0;
    static inline WOWGUID s_activeMover{1};
};
CGUnit_C* active = nullptr;
CGUnit_C* remote = nullptr;
constexpr int TYPE_UNIT = 1;
CGUnit_C* ClntObjMgrObjectPtr(WOWGUID guid, int, const char*, int) {
    return guid.m_guid == 1 ? active : guid.m_guid == 2 ? remote : nullptr;
}
struct CPassenger {
    WOWGUID* m_ownerGuid;
    CPassenger* next = nullptr;
};
struct CMovement_C : CPassenger {
    unsigned falls = 0, otherFalls = 0, transportChanges = 0;
    void OnFallLocal(uint32_t time) { assert(time == 42); ++falls; }
    void TryStartFallingLocal() { ++otherFalls; }
    void ForceSetTransport(WOWGUID guid, unsigned seat, int) {
        assert(guid.m_guid == 0 && seat == 0xFF);
        ++transportChanges;
    }
};
struct CGCamera {
    WOWGUID m_targetGUID;
    unsigned detachments = 0;
    void MakeRelativeTo(WOWGUID guid) { assert(guid.m_guid == 0); ++detachments; }
};
struct CGWorldFrame {
    static inline CGCamera* camera = nullptr;
    static CGCamera* GetActiveCamera() { return camera; }
};
struct FrameTime { static inline uint32_t s_curTimeMs = 42; };
struct CVehicle_C {
    struct {
        CPassenger* first;
        CPassenger* Head() { return first; }
        static CPassenger* Next(CPassenger* passenger) { return passenger->next; }
    } m_passengers;
    void RemoveAllPassengersLocally();
};
#include "vehicle-remove.inc"

void TestVehicle() {
    WOWGUID missing{3}, local{1}, other{2};
    CGUnit_C localUnit, remoteUnit;
    active = &localUnit;
    remote = &remoteUnit;
    localUnit.obj.m_guid = local;
    remoteUnit.obj.m_guid = other;
    CMovement_C third{{&other}}, second{{&local, &third}}, first{{&missing, &second}};
    CVehicle_C vehicle{{&first}};
    vehicle.RemoveAllPassengersLocally();
    assert(first.transportChanges == 0 && second.falls == 1 && third.otherFalls == 1);
    assert(second.transportChanges == 1 && third.transportChanges == 1 && active->m_clientFlags == 0);
    CGCamera camera{missing};
    CGWorldFrame::camera = &camera;
    vehicle.RemoveAllPassengersLocally();
    assert(camera.detachments == 1 && third.transportChanges == 2);
    active = nullptr;
    remote = nullptr;
}

struct CreatureStats_C { uint32_t m_type; };
struct SummonPropertiesRec { int32_t m_slot; };
constexpr uint32_t SPELL_EFFECT_SUMMON = 28;
struct SpellRec {
    uint32_t m_effect[1]{};
    uint32_t m_effectMiscValueB[1]{};
    int32_t m_effectItemType[1]{};
};
struct {
    CreatureStats_C* record = nullptr;
    CreatureStats_C* GetRecord(uint32_t, WOWGUID, void*, void*, bool) { return record; }
} g_creatureDBCache;
struct {
    bool found = false;
    uint32_t effect = SPELL_EFFECT_SUMMON;
    bool GetRecord(uint32_t, SpellRec* spell) {
        if (!found) return false;
        spell->m_effect[0] = effect;
        spell->m_effectMiscValueB[0] = 4;
        return true;
    }
} g_spellDB;
struct {
    SummonPropertiesRec record{2};
    unsigned lookups = 0;
    SummonPropertiesRec* GetRecord(uint32_t id) { assert(id == 4); ++lookups; return &record; }
} g_summonPropertiesDB;
struct CombatLogEntry { int event; };
CombatLogEntry queued{};
CombatLogEntry* GetCombatLogEntry(WOWGUID, WOWGUID, int event, void*) { queued.event = event; return &queued; }
void QueueCombatLogEntry(CombatLogEntry* entry, int) { assert(entry == &queued); }
#include "combat-death.inc"
#include "poi-objective.inc"

void TestCombatAndPoi() {
    CGUnit_C unit;
    UnitCombatLogUnitDead(&unit);
    assert(queued.event == 44 && g_summonPropertiesDB.lookups == 0);
    g_spellDB.found = true;
    UnitCombatLogUnitDead(&unit);
    assert(queued.event == 45 && g_summonPropertiesDB.lookups == 1);
    g_spellDB.effect = 0;
    UnitCombatLogUnitDead(&unit);
    assert(queued.event == 44 && g_summonPropertiesDB.lookups == 1);
    CreatureStats_C critter{13};
    g_creatureDBCache.record = &critter;
    UnitCombatLogUnitDead(&unit);
    assert(queued.event == 49);
    assert(QuestPOIObjectiveMatches(0, 1) && QuestPOIObjectiveMatches(31, 0x80000000));
    assert(!QuestPOIObjectiveMatches(2, 1) && !QuestPOIObjectiveMatches(-1, 0xFFFFFFFF));
    assert(!QuestPOIObjectiveMatches(-2, 0xFFFFFFFF) && !QuestPOIObjectiveMatches(32, 0xFFFFFFFF));
}

template<class T> struct Array {
    std::vector<T> data;
    T* m_data = nullptr;
    size_t Count() const { return data.size(); }
    Array() = default;
    explicit Array(size_t count) : data(count), m_data(data.data()) {}
    void Resize(size_t count) { data.resize(count); m_data = data.data(); }
};
struct CommentatorInstanceKey {
    uint32_t unk_0000 = 0, unk_0004 = 0, unk_0008 = 0;
    void Read(CDataStore* msg) { msg->Get(unk_0000).Get(unk_0004).Get(unk_0008); }
};
struct CGCommentator {
    struct PlayerInfo {
        WOWGUID m_guid;
        char m_name[24];
        uint8_t unk_0070, unk_0071, unk_0072;
        uint32_t unk_0008, unk_000C, unk_0010, unk_0014, m_faction;
        CommentatorInstanceKey m_key;
        uint32_t unk_0028, unk_002C, unk_0030, unk_0034, unk_0038, unk_003C;
    };
    struct InstanceInfo {
        WOWGUID m_guid{2};
        uint32_t m_mapID = 1;
        CommentatorInstanceKey m_key;
        uint32_t m_numPlayers[2]{};
        PlayerInfo m_players[2][5]{};
    };
    struct MapInfo { Array<InstanceInfo> m_instances; };
    Array<MapInfo> m_maps;
    static CGCommentator s_Commentator;
    static inline unsigned m_playerIndex = 0;
    static int32_t OnReceivePlayerInfo(void*, NETMESSAGE, uint32_t, CDataStore*);
};
CGCommentator CGCommentator::s_Commentator;
constexpr int SCRIPT_EVENT_COMMENTATOR_PLAYER_UPDATE = 1;
unsigned rosterUpdates = 0;
void FrameScript_SignalEvent(int, void*) { ++rosterUpdates; }
struct lua_State { unsigned errors = 0; };
void luaL_error(lua_State* L, const char*) { ++L->errors; }
#include "commentator-roster.inc"

int RemovePlayer(lua_State* L, CGCommentator::InstanceInfo* instance, uint32_t faction) {
#include "commentator-faction.inc"
    return 1;
}

void WriteRosterPlayer(CDataStore& msg, uint64_t id, uint32_t faction) {
    msg.Put(id);
    msg.Text("Player");
    for (int j = 0; j < 3; ++j) msg.Put(uint8_t{0});
    for (int j = 0; j < 4; ++j) msg.Put(0u);
    msg.Put(faction);
    for (int j = 0; j < 8; ++j) msg.Put(0u);
    msg.Put(uint8_t{0});
}

void TestCommentator() {
    auto& maps = CGCommentator::s_Commentator.m_maps;
    maps.Resize(1);
    maps.m_data[0].m_instances.Resize(1);
    auto& instance = maps.m_data[0].m_instances.m_data[0];
    CDataStore roster;
    roster.Put(1u);
    for (int j = 0; j < 3; ++j) roster.Put(0u);
    roster.Put(uint64_t{2});
    roster.Put(8u);
    for (uint64_t id = 1; id <= 6; ++id) WriteRosterPlayer(roster, id, 0);
    WriteRosterPlayer(roster, 20, 1);
    WriteRosterPlayer(roster, 30, 2);
    CDataStore truncated = roster;
    truncated.m_size--;
    instance.m_numPlayers[0] = 1;
    instance.m_players[0][0].m_guid = {99};
    assert(CGCommentator::OnReceivePlayerInfo(nullptr, 0, 0, &truncated) == 1);
    assert(!rosterUpdates && instance.m_numPlayers[0] == 1 && instance.m_players[0][0].m_guid.m_guid == 99);
    assert(CGCommentator::OnReceivePlayerInfo(nullptr, 0, 0, &roster) == 1);
    assert(rosterUpdates == 1 && instance.m_numPlayers[0] == 5 && instance.m_numPlayers[1] == 1);
    assert(instance.m_players[0][4].m_guid.m_guid == 5 && instance.m_players[1][0].m_guid.m_guid == 20);
    lua_State L;
    assert(RemovePlayer(&L, &instance, 1) == 1 && !L.errors);
    assert(RemovePlayer(&L, &instance, 2) == 0 && L.errors == 1);
    CGCommentator::m_playerIndex = 1;
    assert(RemovePlayer(&L, &instance, 1) == 0 && L.errors == 2);
}

struct GossipOption { char* m_text; char* m_boxText; int32_t m_index; uint8_t m_coded; uint32_t m_boxMoney; int32_t m_type; };
struct GossipQuestInfo { uint32_t m_questID; int32_t m_level; uint32_t m_flags, m_repeatable, m_icon; char m_title[512]; };
char* SStrDupA(const char* text, const char*, int) {
    const size_t size = std::strlen(text) + 1;
    char* result = new char[size];
    std::memcpy(result, text, size);
    return result;
}
struct CGGossipInfo {
    static inline GossipOption m_options[32]{};
    static inline GossipQuestInfo m_quests[32]{};
    static inline uint32_t m_gossipID = 0, m_gossipOptionsCount = 0, textId = 0;
    static inline WOWGUID guid;
    static void SetGossip(WOWGUID who) { guid = who; }
    static void SetGossipText(uint32_t text) { textId = text; }
    static void ClearOptions() {
        for (auto& option : m_options) { delete[] option.m_text; delete[] option.m_boxText; option = {}; }
        for (auto& quest : m_quests) quest = {};
        m_gossipOptionsCount = 0;
    }
};
#include "gossip.inc"

void TestGossip() {
    CDataStore gossip;
    gossip.Put(uint64_t{14});
    gossip.Put(200u);
    gossip.Put(201u);
    gossip.Put(33u);
    for (uint32_t i = 0; i < 33; ++i) {
        gossip.Put(i);
        gossip.Put(uint8_t{2});
        gossip.Put(uint8_t{0});
        gossip.Put(7u);
        gossip.Text("Option");
        gossip.Text(i == 0 ? "Confirm" : "");
    }
    gossip.Put(33u);
    for (uint32_t i = 0; i < 33; ++i) {
        gossip.Put(i + 1);
        gossip.Put(3u);
        gossip.Put(12u);
        gossip.Put(0u);
        gossip.Put(uint8_t{0});
        gossip.Text("Quest");
    }
    CGGossipInfo::m_gossipID = 7;
    CGGossipInfo::m_quests[0].m_questID = 8;
    for (uint32_t size = 0; size < gossip.m_size; ++size) {
        CDataStore truncated = gossip;
        truncated.m_size = size;
        Packet_SMSG_GOSSIP_MESSAGE(nullptr, 0, 0, &truncated);
        assert(CGGossipInfo::m_gossipID == 7 && CGGossipInfo::m_quests[0].m_questID == 8 && CGGossipInfo::textId == 0);
    }
    Packet_SMSG_GOSSIP_MESSAGE(nullptr, 0, 0, &gossip);
    assert(gossip.m_read == gossip.m_size && CGGossipInfo::m_gossipID == 200 && CGGossipInfo::textId == 201);
    assert(CGGossipInfo::guid.m_guid == 14 && CGGossipInfo::m_gossipOptionsCount == 32);
    assert(CGGossipInfo::m_options[31].m_index == 31 && !CGGossipInfo::m_options[31].m_boxText);
    assert(std::string(CGGossipInfo::m_options[0].m_boxText) == "Confirm" && CGGossipInfo::m_quests[31].m_questID == 32);
    CGGossipInfo::ClearOptions();
}

struct CGTradeSkillInfo {
    static inline uint32_t m_itemsPending = 0;
    static inline unsigned refreshes = 0;
    static void RefreshList(int) { ++refreshes; }
};
#include "tradeskill-callback.inc"
#include "tradeskill-link.inc"
struct ItemStats_C { uint32_t m_class, m_subClass, m_itemLevel, m_inventoryType; };
using CacheCallback = void (*)(int32_t, const WOWGUID&, void*, bool);
struct {
    CacheCallback requested = nullptr;
    ItemStats_C* stats = nullptr;
    ItemStats_C* GetRecord(int32_t, WOWGUID, CacheCallback callback, void*, bool) {
        requested = callback;
        return stats;
    }
} g_itemDBCache;
constexpr uint32_t s_inventoryTypeSlots[] = {0, 4};

struct Recipe { int32_t m_itemClass = 0, m_itemSubClass = 0; uint32_t m_itemLevel = 0, m_invTypeMask = 0; int m_numAvailable = 0; };
Recipe ReadRecipe(int32_t id) {
    SpellRec spell;
    spell.m_effectItemType[0] = id;
    WOWGUID requester{123};
    Recipe output;
    Recipe* info = &output;
    int numAvailable = 3;
    for (int i = 0; i < 1; ++i) {
#include "tradeskill-metadata.inc"
    }
    return output;
}

void TestTradeskill() {
    assert(IsTradeSkillLinkPayload("Aa09+/", 6));
    assert(!IsTradeSkillLinkPayload("A=", 2) && !IsTradeSkillLinkPayload("A\xC3\xA9", 3));
    assert(!IsTradeSkillLinkPayload("A", 2) && !IsTradeSkillLinkPayload("AA", 1));
    const auto pending = ReadRecipe(11);
    assert(pending.m_numAvailable == 3 && g_itemDBCache.requested && CGTradeSkillInfo::m_itemsPending == 1);
    g_itemDBCache.requested(11, WOWGUID{}, nullptr, false);
    assert(CGTradeSkillInfo::refreshes == 1 && CGTradeSkillInfo::m_itemsPending == 0);
    const auto denied = ReadRecipe(11);
    assert(!g_itemDBCache.requested && !CGTradeSkillInfo::m_itemsPending && denied.m_itemClass == -1);
    assert(denied.m_invTypeMask == 0x4000000);
    ItemStats_C stats{7, 5, 42, 1};
    g_itemDBCache.stats = &stats;
    auto loaded = ReadRecipe(11);
    assert(loaded.m_itemClass == 7 && loaded.m_itemLevel == 42 && loaded.m_invTypeMask == 4);
    stats.m_inventoryType = 0xFFFFFFFF;
    loaded = ReadRecipe(11);
    assert(loaded.m_invTypeMask == 0x4000000);
}

int main() {
    TestCorpse();
    TestVehicle();
    TestCombatAndPoi();
    TestCommentator();
    TestGossip();
    TestTradeskill();
    std::puts("batch 3 live-path guard fixtures passed");
}
