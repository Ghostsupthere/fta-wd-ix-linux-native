#include "Engine/Source/ModernAssets/WxlCustomizer/EmoteSoundLookup.hpp"

#include <cassert>
#include <cstdio>
#include <initializer_list>

namespace emotes = wxl::customizer::emotes;

int main()
{
    emotes::SoundRow const rows[] = {
        { 1, 329, 9, 0, 991056 }, { 2, 329, 14, 0, 9720 }, { 3, 329, 22, 0, 19355 },
        { 4, 329, 22, 1, 19441 }, { 5, 328, 22, 0, 19354 }, { 6, 329, 255, 0, 42 },
        { 7, 329, 22, 0, 19356 }, { 8, 330, 22, 0, 0xFFFFFFFF },
    };
    constexpr uint32_t count = sizeof(rows) / sizeof(rows[0]);
    assert(emotes::FindExtendedSound(rows, count, 329, 22, 0) == 19356);
    assert(emotes::FindExtendedSound(rows, count, 329, 22, 1) == 19441);
    assert(emotes::FindExtendedSound(rows, count, 328, 22, 0) == 19354);
    assert(emotes::FindExtendedSound(rows, count, 329, 255, 0) == 42);
    assert(!emotes::FindExtendedSound(rows, count, 330, 22, 0));
    assert(!emotes::FindExtendedSound(rows, count, 329, 22, 2));
    assert(!emotes::FindExtendedSound(rows, count, 329, 22, 0xFFFFFFFF));
    assert(!emotes::FindExtendedSound(nullptr, count, 329, 22, 0));
    assert(!emotes::FindExtendedSound(rows, 0, 329, 22, 0));
    assert(!emotes::FindExtendedSound(rows, 0xFFFFFFFF, 329, 22, 0));
    for (uint32_t race : { 0U, 9U, 14U, 17U, 21U, 22U, 255U, 256U, 0xFFFFFFFFU })
    {
        uint32_t nativeCalls = 0;
        uint32_t extendedCalls = 0;
        auto original = [&](uint32_t emote, uint32_t selectedRace, uint32_t sex)
        {
            ++nativeCalls;
            assert(emote == 329 && selectedRace == race && sex == 0);
            return 123U;
        };
        auto extended = [&](uint32_t emote, uint32_t selectedRace, uint32_t sex)
        {
            ++extendedCalls;
            return emotes::FindExtendedSound(rows, count, emote, selectedRace, sex);
        };
        uint32_t const result = emotes::Dispatch(329, race, 0, emotes::kNoVariant, original, extended);
        assert(nativeCalls == (race < 22 ? 1U : 0U));
        assert(extendedCalls == (race >= 22 && race <= 255 ? 1U : 0U));
        if (race < 22)
            assert(result == 123);
        else if (race > 255)
            assert(result == emotes::kNoVariant);
    }
    uint32_t fileCount = 10;
    uint32_t selected = 9;
    uint32_t cacheCalls = 0;
    uint32_t selectCalls = 0;
    auto getCache = [&](uint32_t kit) -> uint32_t*
    {
        ++cacheCalls;
        assert(kit == 19355 || kit == 1);
        return kit == 1 ? nullptr : &fileCount;
    };
    auto getCount = [](uint32_t* cache) { return *cache; };
    auto select = [&](uint32_t* cache, uint32_t mode, uint32_t* output, int32_t requested, uint32_t channel)
    {
        ++selectCalls;
        assert(cache == &fileCount && mode == 2 && requested == -1 && channel == 5);
        *output = selected;
    };
    assert(emotes::PickVariant(19355, getCache, getCount, select) == 9);
    selected = 10;
    assert(emotes::PickVariant(19355, getCache, getCount, select) == emotes::kNoVariant);
    fileCount = 0;
    assert(emotes::PickVariant(19355, getCache, getCount, select) == emotes::kNoVariant);
    fileCount = 11;
    assert(emotes::PickVariant(19355, getCache, getCount, select) == emotes::kNoVariant);
    assert(emotes::PickVariant(1, getCache, getCount, select) == emotes::kNoVariant);
    assert(emotes::PickVariant(0, getCache, getCount, select) == emotes::kNoVariant);
    assert(emotes::PickVariant(0xFFFFFFFF, getCache, getCount, select) == emotes::kNoVariant);
    assert(cacheCalls == 5 && selectCalls == 2);
    std::atomic<uint32_t> lookupTraces{ 0 };
    std::atomic<uint32_t> variantTraces{ 0 };
    for (uint32_t race : { 0U, 1U, 17U, 21U, 23U, 255U, 256U, 0xFFFFFFFFU })
        assert(!emotes::ShouldTraceSilly(329, race, lookupTraces));
    for (uint32_t emote : { 0U, 1U, 328U, 330U, 0xFFFFFFFFU })
        assert(!emotes::ShouldTraceSilly(emote, 9, lookupTraces));
    assert(lookupTraces.load() == 0 && variantTraces.load() == 0);
    constexpr uint32_t traceRaces[] = { 9, 14, 22 };
    uint32_t lookupLines = 0;
    uint32_t variantLines = 0;
    for (uint32_t index = 0; index < 10000; ++index)
    {
        if (emotes::ShouldTraceSilly(329, traceRaces[index % 3], lookupTraces))
            ++lookupLines;
        if (emotes::ShouldTraceSilly(329, traceRaces[index % 3], variantTraces))
            ++variantLines;
    }
    assert(lookupLines == 12 && variantLines == 12);
    assert(lookupTraces.load() == 12 && variantTraces.load() == 12);
    lookupTraces.store(0xFFFFFFFF);
    assert(!emotes::ShouldTraceSilly(329, 9, lookupTraces));
    assert(lookupTraces.load() == 0xFFFFFFFF);
    std::puts("PASS emote lookup: original races preserved, extended bounds and native picker ABI");
    std::puts("PASS silly trace: only races9/14/22, independent 12-line path limits, no counter wrap");
}
