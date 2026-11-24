#include <array>
#include <bitset>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>

struct UiRoot {
    float scale;
    void SetFrameScale(float value, bool) { scale = value; }
};
struct CLayoutFrame { static void ResizePending() {} };
inline constexpr int SCRIPT_EVENT_UPDATE_FLOATING_CHAT_WINDOWS = 1;
void FrameScript_SignalEvent(int, void*) {}
struct CGGameUI { static inline UiRoot* m_UISimpleParent; static void SetUIScale(float); };

struct M2SequenceFallback { uint32_t m_id, m_mode; };
struct AnimationDataRec { uint32_t m_fallback; uint32_t m_flags; };
enum : uint32_t { ANIM_STAND = 0, ANIM_CLOSED = 1, ANIM_COUNT = 506 };
struct AnimationDB {
    std::array<AnimationDataRec, 4> records{{{0, 0}, {1, 0}, {3, 0x10}, {0, 0}}};
    AnimationDataRec* GetRecord(uint32_t id) {
        assert(id < ANIM_COUNT);
        return id < records.size() ? &records[id] : nullptr;
    }
} g_animationDataDB;
struct M2Data {
    struct {
        struct { uint32_t m_id; } first{0};
        unsigned m_count = 1;
        auto& operator[](int) { return first; }
    } m_sequences;
};
struct CM2Model {
    struct { M2Data* m_data; } shared;
    decltype(shared)* m_shared = &shared;
    bool HasSequence(M2Data*, uint32_t id) { return id == 0 || id == 3000; }
    bool HasLoadFailed() const { return false; }
    M2SequenceFallback* SequenceFallbackById(M2SequenceFallback* result, uint32_t id);
};

enum COMPONENT_SECTIONS : int32_t { SECTION_FOOT = 7, SECTION_HEAD = 8 };
struct ItemDisplayInfoRec { const char* m_texture[8]{}; int m_id{}; };
struct ItemDB { bool GetRecord(int, ItemDisplayInfoRec*) { return false; } } g_itemDisplayInfoDB;
void TextureCacheDestroyTexture(void*) {}
struct CharacterSection {
    void* m_layerTex[7]{};
    int m_layerItemDisplayId[7]{};
    int m_layerMask = 0;
};
struct CCharacterComponent {
    CharacterSection m_section[10];
    struct { unsigned m_flags = 0; } m_data;
    int32_t UpdateTextureSlot(COMPONENT_SECTIONS, const ItemDisplayInfoRec*, int32_t);
};

struct Chunk {
    struct { int32_t x, y; } m_aIndex;
};
struct Area {
    Chunk* m_mapChunks[256]{};
    struct ChunkInfo { uint32_t m_flags; };
    ChunkInfo* m_chunkInfo{};
    void* m_fileBuffer{};
    void Drop(Chunk* chunk) {
#include "terrain_teardown.inc"
    }
    Chunk* Prepare(int32_t chunkX, int32_t chunkY) {
#include "terrain_prepare.inc"
        return m_mapChunks[chunkY * 16 + chunkX];
    }
};
struct Lightning {
    struct Bolt { uint32_t m_source, m_target; };
    Bolt bolt;
    struct { unsigned Count() const { return 2; } } m_targets;
    bool Valid() {
        for (unsigned i = 0; i < 1; ++i) {
#include "lightning_guard.inc"
            return true;
        }
        return false;
    }
};
#include "hardening.inc"

int main() {
    UiRoot root{};
    CGGameUI::m_UISimpleParent = &root;
    CGGameUI::SetUIScale(0.85f);
    assert(root.scale == 0.85f);
    CGGameUI::SetUIScale(std::numeric_limits<float>::infinity());
    assert(root.scale == 0.64f);

    M2Data data;
    CM2Model model;
    model.shared.m_data = &data;
    M2SequenceFallback fallback;
    model.SequenceFallbackById(&fallback, 3000);
    assert(fallback.m_id == 3000 && fallback.m_mode == 0);
    model.SequenceFallbackById(&fallback, 2);
    assert(fallback.m_id == 0 && fallback.m_mode == 1);
    model.SequenceFallbackById(&fallback, 0xffffffff);
    assert(fallback.m_id == 0 && fallback.m_mode == 0);
    g_animationDataDB.records[3].m_fallback = 2;
    model.SequenceFallbackById(&fallback, 2);
    assert(fallback.m_id == 0 && fallback.m_mode == 0);

    CCharacterComponent component;
    ItemDisplayInfoRec record;
    record.m_id = 42;
    assert(component.UpdateTextureSlot(SECTION_FOOT, &record, 6) == 1);
    assert(component.m_section[7].m_layerItemDisplayId[6] == 42);
    assert(!component.UpdateTextureSlot(SECTION_HEAD, &record, 0));
    assert(!component.UpdateTextureSlot(SECTION_FOOT, &record, -1));
    assert(!component.UpdateTextureSlot(SECTION_FOOT, nullptr, 0));

    Area area;
    Chunk chunk{{15, 15}};
    std::array<Area::ChunkInfo, 256> info{};
    area.m_chunkInfo = info.data();
    area.m_fileBuffer = info.data();
    area.m_mapChunks[255] = &chunk;
    info[255].m_flags = 1;
    assert(area.Prepare(15, 15) && !area.Prepare(16, 0) && !area.Prepare(0, -1));
    area.Drop(&chunk);
    assert(!area.m_mapChunks[255] && info[255].m_flags == 0);
    area.m_fileBuffer = nullptr;
    assert(!area.Prepare(15, 15));
    area.Drop(&chunk);
    chunk.m_aIndex.x = -1;
    area.Drop(&chunk);

    Lightning light{{0, 1}};
    assert(light.Valid());
    light.bolt.m_source = 2;
    assert(!light.Valid());
    light.bolt = {0, 0xffff};
    assert(!light.Valid());
}
