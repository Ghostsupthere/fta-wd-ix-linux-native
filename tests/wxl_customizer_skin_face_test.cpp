#include "Engine/Source/ModernAssets/WxlCustomizer/AppearanceSelection.hpp"

#include <cassert>
#include <cstdio>
#include <initializer_list>

int main()
{
    static_assert(wxl::customizer::HasVariableFaceSets(9));
    static_assert(wxl::customizer::HasVariableFaceSets(22));
    static_assert(wxl::customizer::HasVariableFaceSets(23));
    static_assert(wxl::customizer::HasVariableFaceSets(26));
    static_assert(!wxl::customizer::HasVariableFaceSets(11));
    static_assert(!wxl::customizer::HasVariableFaceSets(24));
    static_assert(!wxl::customizer::HasVariableFaceSets(10));
    static_assert(!wxl::customizer::HasVariableFaceSets(256));
    uint32_t selected = 99;
    // Modern Goblin male: skin 12 has ten faces; skin 0 has five. DK skin 9 has three.
    auto male = [](uint32_t section, uint32_t face, uint32_t skin)
    {
        if (skin > 14)
            return false;
        if (section == 0 || section == 4)
            return true;
        return section == 1 && face < (skin >= 12 ? 10U : skin >= 9 ? 3U : 5U);
    };
    assert(wxl::customizer::ResolveSkinFace(9, 0, 9, male, selected) && selected == 0);
    assert(wxl::customizer::ResolveSkinFace(9, 12, 4, male, selected) && selected == 4);
    assert(wxl::customizer::ResolveSkinFace(9, 9, 4, male, selected) && selected == 0);

    // Female DK textures have IDs 0, 1, 6; enumeration must not assume a dense face range.
    auto female = [](uint32_t section, uint32_t face, uint32_t skin)
    {
        return skin == 9 && (section == 0 || section == 4 ||
            (section == 1 && (face == 0 || face == 1 || face == 6)));
    };
    assert(wxl::customizer::ResolveSkinFace(9, 9, 6, female, selected) && selected == 6);
    assert(wxl::customizer::ResolveSkinFace(9, 9, 5, female, selected) && selected == 0);

    // The callback includes native class/source flags: do not offer DK-only or incomplete skins.
    auto noDk = [&](uint32_t section, uint32_t face, uint32_t skin)
    {
        return (skin < 9 || skin > 11) && male(section, face, skin);
    };
    assert(!wxl::customizer::ResolveSkinFace(9, 9, 0, noDk, selected));
    for (uint32_t missingSection : { 0U, 1U, 4U })
    {
        auto incomplete = [&](uint32_t section, uint32_t face, uint32_t skin)
        {
            return section != missingSection && male(section, face, skin);
        };
        selected = 99;
        assert(!wxl::customizer::ResolveSkinFace(9, 0, 9, incomplete, selected));
        assert(selected == 99);
    }

    // Existing races still require the preferred face; no broad customization behavior change.
    for (uint32_t race = 1; race < 256; ++race)
        if (!wxl::customizer::HasVariableFaceSets(race))
        {
            assert(!wxl::customizer::ResolveSkinFace(race, 0, 9, male, selected));
            assert(wxl::customizer::ResolveSkinFace(race, 0, 4, male, selected) && selected == 4);
        }

    // Void Elf special skin 100 has only face zero; regular skins retain their selected face.
    auto voidElf = [](uint32_t section, uint32_t face, uint32_t skin)
    {
        return (skin < 12 || skin == 100) && (section == 0 || section == 4 ||
            (section == 1 && face < (skin == 100 ? 1U : 8U)));
    };
    assert(wxl::customizer::ResolveSkinFace(23, 100, 7, voidElf, selected) && selected == 0);
    assert(wxl::customizer::ResolveSkinFace(23, 11, 7, voidElf, selected) && selected == 7);
    assert(!wxl::customizer::ResolveSkinFace(23, 99, 0, voidElf, selected));

    // Plain Eredar retains ten face choices; marked tones currently have authored face zero.
    auto eredar = [](uint32_t section, uint32_t face, uint32_t skin)
    {
        return skin < 72 && (section == 0 || section == 4 ||
            (section == 1 && face < (skin < 24 ? 10U : 1U)));
    };
    for (uint32_t skin = 24; skin < 72; ++skin)
        assert(wxl::customizer::ResolveSkinFace(26, skin, 9, eredar, selected) && selected == 0);
    assert(wxl::customizer::ResolveSkinFace(26, 23, 9, eredar, selected) && selected == 9);
    assert(!wxl::customizer::ResolveSkinFace(26, 72, 0, eredar, selected));

    // Worgen export coats have distinct face families. Every claw mode must retain the
    // current face when supported, or resolve an authored face before setting the skin.
    auto worgen = [](uint32_t section, uint32_t face, uint32_t skin)
    {
        if (skin >= 92)
            return false;
        uint32_t const coat = skin % 23;
        return section == 0 || section == 4 || (section == 1 &&
            (face == 0 || (coat < 9 ? face < 16 || face == 45 :
                coat < 15 ? face < 16 : coat < 18 ? face == 23 || face == 25 || face == 29 :
                face >= 17 && face <= 31)));
    };
    for (uint32_t mode = 0; mode < 4; ++mode)
    {
        assert(wxl::customizer::ResolveSkinFace(22, 21 + mode * 23, 45, worgen, selected) && selected == 0);
        assert(wxl::customizer::ResolveSkinFace(22, 5 + mode * 23, 45, worgen, selected) && selected == 45);
        assert(wxl::customizer::ResolveSkinFace(22, 15 + mode * 23, 29, worgen, selected) && selected == 29);
    }
    assert(!wxl::customizer::ResolveSkinFace(22, 92, 0, worgen, selected));

    // Enumerate with uint32_t so face 255 terminates rather than wrapping a byte counter.
    uint32_t faceCalls = 0;
    auto lastFace = [&](uint32_t section, uint32_t face, uint32_t)
    {
        if (section == 1)
            ++faceCalls;
        return section != 1 || face == 255;
    };
    assert(wxl::customizer::ResolveSkinFace(9, 255, 0, lastFace, selected) && selected == 255);
    assert(faceCalls == 257);
    assert(!wxl::customizer::ResolveSkinFace(9, 256, 0, lastFace, selected));
    assert(!wxl::customizer::ResolveSkinFace(9, 0, 256, lastFace, selected));
    assert(!wxl::customizer::ResolveSkinFace(256, 0, 0, lastFace, selected));
    assert(!wxl::customizer::ResolveSkinFace(9, UINT32_MAX, 0, lastFace, selected));
    assert(faceCalls == 257);
    std::puts("PASS: Goblin skin/face transitions, sparse DK faces, source/asset gates, other races, byte boundaries");
}
