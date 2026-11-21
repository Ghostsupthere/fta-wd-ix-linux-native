#include "WoW/Common/ComponentCore/EquipmentFit.h"

#include <cassert>

int main() {
    using namespace EquipmentFit;

    Context helmet;
    helmet.slot = Slot::Helm;
    helmet.race = 6;
    helmet.racePrefix = "Ta";
    helmet.raceName = "Tauren";
    helmet.display = LegacyHeadDisplays[0];
    assert(!IsModern(helmet.slot, 0, helmet.display));
    assert(!IsModern(helmet.slot, 56839, helmet.display));
    assert(IsModern(helmet.slot, 56840, helmet.display));
    assert(IsModern(helmet.slot, 1, RequestedModernHeadDisplays[0]));
    assert(helmet.display != RequestedModernHeadDisplays[0]);
    assert(DefaultScale(helmet) == 1.0f);

    helmet.modern = true;
    assert(DefaultScale(helmet) == 0.78f);
    helmet.sex = 1;
    assert(DefaultScale(helmet) == 0.85f);

    Context shoulders = helmet;
    shoulders.slot = Slot::Shoulders;
    assert(DefaultScale(shoulders) == 0.85f);
    shoulders.modern = false;
    assert(DefaultScale(shoulders) == 0.94f);

    const auto rendered = ParseBodyName("Character/NightElf/Female/NightElfFemale_HD.m2");
    assert(rendered.valid && rendered.sex == 1 && TokenEqual(rendered.race, "Night Elf"));

    const auto settings = Parse(R"(
[EquipmentScale]
Version=1
UseBuiltInDefaults=0
[rule item]
Slot=Helm
Item=100
Scale=60%
[rule display]
Display=200
Scale=0.7
[rule female tauren helms]
Race=Ta
Sex=female
Slot=helm
Era=modern
Scale=75%
)");
    assert(settings.valid);
    helmet.item = 100;
    helmet.display = 200;
    assert(settings.configuration.Resolve(helmet, DefaultScale(helmet)) == 0.75f);
    helmet.racePrefix = "Ne";
    helmet.raceName = rendered.race;
    assert(settings.configuration.Resolve(helmet, DefaultScale(helmet)) == 0.7f);
    assert(DefaultScale(helmet) == 1.0f);

    helmet.display = 0;
    assert(settings.configuration.Resolve(helmet, DefaultScale(helmet)) == 0.6f);
    helmet.item = 0;
    assert(settings.configuration.Resolve(helmet, DefaultScale(helmet)) == 1.0f);

    const auto disabled = Parse("[EquipmentScale]\nVersion=1\nEnabled=0\n");
    assert(disabled.valid && disabled.configuration.Resolve(shoulders, DefaultScale(shoulders)) == 1.0f);
    assert(!Parse("[EquipmentScale]\nVersion=1\n[rule bad]\nItem=100\nDisplay=200\nScale=nan\n").valid);
    assert(!Parse("[EquipmentScale]\nVersion=1\n[rule bad]\nScale=nan\n").valid);
}
