#ifndef WOW_SOURCE_GLUE_WXLCHARACTERCREATIONCACHE_H
#define WOW_SOURCE_GLUE_WXLCHARACTERCREATIONCACHE_H

#include <cstddef>
#include <cstdint>

constexpr uint32_t WxlCharacterRaceCapacity = 256;
constexpr uint32_t WxlCharacterSexCapacity = 3;

template<class T, size_t Races, size_t Sexes>
T** WxlCharacterPreferenceSlot(T* (&cache)[Races][Sexes], uint32_t race, uint32_t sex) {
    return race < Races && sex < Sexes ? &cache[race][sex] : nullptr;
}

template<class T, size_t Races, size_t Sexes>
void WxlClearCharacterPreferences(T* (&cache)[Races][Sexes]) {
    for (auto& race : cache)
        for (auto& entry : race) { delete entry; entry = nullptr; }
}

#endif
