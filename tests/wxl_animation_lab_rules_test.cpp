#include "Engine/Source/ModernAssets/WxlAnimationLab/AnimationLabRules.hpp"
#include <cassert>
#include <cstring>
#include <iterator>
#include <vector>

int main() {
    using namespace wxl::animlab;

    // Canonical paths, extensions, slashes, case and the _HD suffix classify; look-alikes do not.
    const PlayableModel* m = ClassifyPlayablePath("Character\\BloodElf\\Female\\BloodElfFemale.m2");
    assert(m && m->raceId == 10 && m->genderId == 1);
    assert(ClassifyPlayablePath("character/bloodelf/female/bloodelffemale_hd.M2") == m);
    assert(ClassifyPlayablePath("Character\\Human\\Male\\HumanMale_HD") && ClassifyPlayablePath("Character\\Human\\Male\\HumanMale_HD")->raceId == 1);
    assert(!ClassifyPlayablePath("Character\\Human\\Male\\HumanMale_SD"));
    assert(!ClassifyPlayablePath("Character\\Human\\Male\\HumanMaleX"));
    assert(!ClassifyPlayablePath("Character\\Tuskarr\\Male\\TuskarrMale"));
    assert(!ClassifyPlayablePath("") && !ClassifyPlayablePath(nullptr));
    assert(ClassifyPlayablePath("Character\\EredarPlayer\\Female\\EredarFemale")->raceId == 26);
    assert(ClassifyHoverFallbackPath("Character\\Tuskarr\\Male\\TuskarrMale.m2")->raceId == 17);
    assert(ClassifyHoverFallbackPath("Character\\Broken\\Female\\BrokenFemale")->genderId == 1);
    assert(!ClassifyHoverFallbackPath("Character\\Human\\Male\\HumanMale"));
    // No two table rows share a (race, gender) pair.
    for (size_t i = 0; i < std::size(kPlayableModels); ++i)
        for (size_t j = i + 1; j < std::size(kPlayableModels); ++j)
            assert(kPlayableModels[i].raceId != kPlayableModels[j].raceId || kPlayableModels[i].genderId != kPlayableModels[j].genderId);

    // Over-long paths are truncated, never overrun.
    std::vector<char> big(1000, 'a'); big.back() = 0;
    assert(!ClassifyPlayablePath(big.data()));

    assert(IsPlayableClass(1) && IsPlayableClass(9) && !IsPlayableClass(10) && IsPlayableClass(11) && !IsPlayableClass(0) && !IsPlayableClass(12));
    assert(IsIdleReadyAnimation(0) && IsIdleReadyAnimation(481) && !IsIdleReadyAnimation(5));
    assert(IsProtectedMotion(5) && IsProtectedMotion(505) && !IsProtectedMotion(0) && !IsProtectedMotion(2));
    assert(IsLevitateMovement(5) && IsLevitateMovement(13) && IsLevitateMovement(143) && !IsLevitateMovement(4));
    assert(IsHoverRequest(4, false) && IsHoverRequest(0, false) && !IsHoverRequest(193, false) && IsHoverRequest(193, true));
    assert(IsSequenceWeaponPolicy(0) && IsSequenceWeaponPolicy(1) && IsSequenceWeaponPolicy(4) && IsSequenceWeaponPolicy(16) &&
           IsSequenceWeaponPolicy(32) && !IsSequenceWeaponPolicy(2) && !IsSequenceWeaponPolicy(8));
    assert(!RequiresPostWeaponPolicy(0) && !RequiresPostWeaponPolicy(1) && RequiresPostWeaponPolicy(4) && RequiresPostWeaponPolicy(16) && RequiresPostWeaponPolicy(32));

    // Wrapping deadlines.
    assert(DeadlineReached(100, 100) && DeadlineReached(101, 100) && !DeadlineReached(99, 100));
    assert(!DeadlineReached(0xFFFFFFF0u, 0x10u) && DeadlineReached(0x20u, 0xFFFFFFF0u));

    // Quadratic-probe lookup: ids 3 and 6 collide in a 3-slot table (both hash to slot 0).
    const std::vector<uint32_t> ids = {3, 6, 9};
    uint16_t lookup[3] = {0, 1, 2};   // slot 0 -> seq 0 (id 3); id 6: slot 0 then 1 -> seq 1
    auto idOf = [&](uint32_t i) { return ids[i]; };
    assert(FindSequenceByAnimationId(lookup, 3, 3, 3, idOf) == 0);
    assert(FindSequenceByAnimationId(lookup, 3, 3, 6, idOf) == 1);
    assert(FindSequenceByAnimationId(lookup, 3, 3, 9, idOf) == 2);
    // A missing id walks the table and fails; an empty slot ends the walk; bad indices fail closed.
    assert(FindSequenceByAnimationId(lookup, 3, 3, 12, idOf) == -1);
    uint16_t holes[3] = {0xFFFF, 1, 2};
    assert(FindSequenceByAnimationId(holes, 3, 3, 3, idOf) == -1);
    uint16_t bad[3] = {7, 1, 2};
    assert(FindSequenceByAnimationId(bad, 3, 3, 3, idOf) == -1);
    assert(FindSequenceByAnimationId(lookup, 3, 3, 70000, idOf) == -1);
    assert(FindSequenceByAnimationId(nullptr, 3, 3, 3, idOf) == -1 && FindSequenceByAnimationId(lookup, 0, 3, 3, idOf) == -1);
    return 0;
}
