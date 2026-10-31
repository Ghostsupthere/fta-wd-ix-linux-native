#include "WoW/Common/ComponentCore/WxlWeaponSidecar.h"
#include <cassert>

int main() {
    using namespace WxlWeaponSidecar;

    // Geoset cells: empty means no filter, anything else implies geoset 0.
    assert(!ParseGeosets("").filter && !ParseGeosets("  ").filter);
    assert(ParseGeosets("0").filter && ParseGeosets("0").ids == std::vector<uint16_t>{0});
    assert((ParseGeosets("1, 3101,3101,x,5").ids == std::vector<uint16_t>{0, 1, 3101}));
    assert(ParseGeosets("1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18").ids.size() == 16);

    // The upstream sample, including the row that drops its trailing empty cells.
    const char* models =
        "DisplayID,Model1Path,Model2Path,Geoset1,Geoset2,Model1OffhandPath,Model2OffhandPath,Geoset1Offhand,Geoset2Offhand,NoAutoMirror\r\n"
        "71028,Item\\ObjectComponents\\Shield\\shield_1h_emeralddreamraid_d_02.mdx,,,,,,,,\r\n"
        "71029,Item\\ObjectComponents\\Weapon\\glaive_1h_R.mdx,,,,Item\\ObjectComponents\\Weapon\\glaive_1h_L.mdx,,,,\r\n"
        "71017,Item\\ObjectComponents\\Weapon\\staff_2h_ulatek_d_01.mdx,,,,,,,\r\n"
        "71060,Item\\ObjectComponents\\Weapon\\sword.mdx,,\"0,7\",,,,\"0,9\",,true\r\n"
        "71061,Item\\ObjectComponents\\Weapon\\axe.mdx,,,,,,,,yes\r\n"
        "71062,Item\\ObjectComponents\\Weapon\\mace.mdx\r\n"
        "71017,Item\\ObjectComponents\\Weapon\\ignored.mdx,,,,,,,,\r\n"
        "0,Item\\x.mdx\r\n"
        "71063,\r\n";
    std::vector<Weapon> weapons;
    assert(ParseWeapons(models, weapons));
    assert(weapons.size() == 6);
    assert(weapons[2].display == 71017 && weapons[2].model[0].find("staff_2h_ulatek_d_01") != std::string::npos);
    assert(weapons[3].geoset[0].ids == std::vector<uint16_t>({0, 7}) && weapons[3].offhandGeoset[0].ids == std::vector<uint16_t>({0, 9}));
    assert(weapons[3].mirrorOffhand && weapons[4].mirrorOffhand && !weapons[0].mirrorOffhand);
    assert(!weapons[5].geoset[0].filter);

    const char* textures =
        "DisplayID,ModelColumn,TextureType,TexturePath\r\n"
        "71017,0,2,item\\objectcomponents\\Weapon\\staff_teal.blp\r\n"
        "71017,0,3,item\\objectcomponents\\Weapon\\staff_3.blp\r\n"
        "71017,0,3,item\\objectcomponents\\Weapon\\duplicate.blp\r\n"
        "71017,1,2,item\\objectcomponents\\Weapon\\second_model.blp\r\n"
        "71028,0,2,item\\objectcomponents\\Shield\\shield_blue.blp\r\n"
        "71029,0,2,item\\objectcomponents\\Weapon\\glaive_red.blp\r\n"
        "71061,0,x,bad.blp\r\n"
        "71061,2,2,bad.blp\r\n";
    std::vector<Texture> tex;
    assert(ParseTextures(textures, tex));
    assert(tex.size() == 5);

    Resolution r;
    // A plain weapon: model, base geosets, its column-0 textures only.
    assert(Resolve(weapons, tex, 71017, false, r));
    assert(r.model.find("staff_2h_ulatek_d_01") != std::string::npos && !r.geoset.filter && !r.mirror);
    assert(r.textures.size() == 2 && r.textures[1].type == 3 && r.textures[1].path.find("staff_3") != std::string::npos);
    // No separate offhand model and no mirror: the offhand renders the mainhand result.
    Resolution off;
    assert(Resolve(weapons, tex, 71017, true, off) && off.model == r.model && off.textures.size() == 2);
    // A shield is attached through the offhand and takes the mainhand bake.
    assert(Resolve(weapons, tex, 71028, true, off) && off.model.find("shield_1h") != std::string::npos);
    // An explicit offhand model wins in the offhand only, keeping the column-0 textures.
    assert(Resolve(weapons, tex, 71029, false, r) && r.model.find("glaive_1h_R") != std::string::npos);
    assert(Resolve(weapons, tex, 71029, true, off) && off.model.find("glaive_1h_L") != std::string::npos && off.textures.size() == 1);
    // Mirror without an offhand model mirrors the mainhand model, preferring the offhand geosets.
    assert(Resolve(weapons, tex, 71060, true, off));
    assert(off.mirror && off.model.find("sword") != std::string::npos && off.geoset.ids == std::vector<uint16_t>({0, 9}));
    assert(Resolve(weapons, tex, 71060, false, r) && !r.mirror && r.geoset.ids == std::vector<uint16_t>({0, 7}));
    // Mirror with no geoset filter of its own falls back to the mainhand filter (none here).
    assert(Resolve(weapons, tex, 71061, true, off) && off.mirror && !off.geoset.filter);
    // Nothing to change: the mainhand keeps the stock attach; unknown displays are untouched.
    assert(!Resolve(weapons, tex, 71062, false, r));
    assert(!Resolve(weapons, tex, 71061, false, r));
    assert(!Resolve(weapons, tex, 99999, false, r));

    // Malformed tables leave the result alone.
    std::vector<Weapon> kept = weapons;
    assert(!ParseWeapons("DisplayID,Model2Path\r\n1,a\r\n", kept) && kept.size() == weapons.size());
    assert(!ParseWeapons("DisplayID,Model1Path\r\n1,\"unterminated\r\n", kept) && kept.size() == weapons.size());
    std::vector<Texture> keptTextures = tex;
    assert(!ParseTextures("DisplayID,TexturePath\r\n1,a.blp\r\n", keptTextures) && keptTextures.size() == tex.size());
    return 0;
}
