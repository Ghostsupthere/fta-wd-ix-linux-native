#include "Engine/Source/ModernAssets/WxlNpcBot/NpcBotTargetRules.hpp"
#include <cassert>
#include <initializer_list>

int main() {
    using namespace wxl::npcbot;
    assert(IsCreatureGuid(0xF130000000001234ull) && IsCreatureGuid(0xF130FFFFFFFFFFFFull));
    assert(!IsCreatureGuid(0xF140000000001234ull));   // pet
    assert(!IsCreatureGuid(0xF150000000001234ull));   // vehicle
    assert(!IsCreatureGuid(0x0000000000000042ull));   // player
    assert(!IsCreatureGuid(0));

    unsigned count = 0;
    for (uint32_t s = 0; s < 400000; ++s) count += IsResurrectionSpell(s);
    assert(count == 36);   // 35 class ranks + Intercession
    for (uint32_t s : {2006u, 7328u, 20484u, 50763u, 300331u, 48477u, 20777u}) assert(IsResurrectionSpell(s));
    for (uint32_t s : {0u, 61999u, 46619u, 20707u /* soulstone */, 2007u}) assert(!IsResurrectionSpell(s));

    // Friendly corpse becomes dead ally; other target bits are untouched.
    assert(DeadBotTargetMask(kFriendlyCorpse) == kDeadAlly);
    assert(DeadBotTargetMask(kFriendlyCorpse | 0x2) == (kDeadAlly | 0x2));
    assert(DeadBotTargetMask(0x2) == (kDeadAlly | 0x2));
    assert((DeadBotTargetMask(0xFFFF) & kFriendlyCorpse) == 0);
    return 0;
}
