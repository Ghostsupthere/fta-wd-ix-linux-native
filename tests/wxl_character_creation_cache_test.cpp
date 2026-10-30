#include "WoW/Source/Glue/WxlCharacterCreationCache.h"
#include <cassert>

struct Preference {
    static unsigned alive;
    uint32_t appearance;
    explicit Preference(uint32_t value) : appearance(value) { ++alive; }
    ~Preference() { --alive; }
};
unsigned Preference::alive = 0;

int main() {
    struct Guarded {
        uint64_t before = 0x123456789ABCDEF0ull;
        Preference* cache[WxlCharacterRaceCapacity][WxlCharacterSexCapacity]{};
        uint64_t after = 0xFEDCBA9876543210ull;
    } guarded;
    // Every packet race and native gender, including Worgen (22), the packet
    // maximum (255), and sentinel gender 2, has an independent owned slot.
    for (uint32_t race = 0; race < 256; ++race)
        for (uint32_t sex = 0; sex < 3; ++sex) {
            auto** slot = WxlCharacterPreferenceSlot(guarded.cache, race, sex);
            assert(slot && !*slot);
            *slot = new Preference(race * 3 + sex);
        }
    for (uint32_t race = 0; race < 256; ++race)
        for (uint32_t sex = 0; sex < 3; ++sex)
            assert((*WxlCharacterPreferenceSlot(guarded.cache, race, sex))->appearance == race * 3 + sex);
    assert(Preference::alive == 768);
    assert(!WxlCharacterPreferenceSlot(guarded.cache, 256, 0));
    assert(!WxlCharacterPreferenceSlot(guarded.cache, UINT32_MAX, 0));
    assert(!WxlCharacterPreferenceSlot(guarded.cache, 22, 3));
    assert(!WxlCharacterPreferenceSlot(guarded.cache, 22, UINT32_MAX));
    WxlClearCharacterPreferences(guarded.cache);
    assert(Preference::alive == 0);
    WxlClearCharacterPreferences(guarded.cache); // Reinitialize/close remains idempotent.
    assert(Preference::alive == 0);
    for (const auto& race : guarded.cache) for (const auto* entry : race) assert(!entry);
    assert(guarded.before == 0x123456789ABCDEF0ull && guarded.after == 0xFEDCBA9876543210ull);
}
