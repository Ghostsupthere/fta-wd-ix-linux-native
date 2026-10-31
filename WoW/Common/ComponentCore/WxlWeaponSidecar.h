#ifndef WOW_COMMON_COMPONENTCORE_WXLWEAPONSIDECAR_H
#define WOW_COMMON_COMPONENTCORE_WXLWEAPONSIDECAR_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// Sidecar-driven weapon models and textures. The tables and the mainhand/offhand rules follow the
// upstream WeaponExtension (WXL-Equip-Creature-Extension-CSV-WXL11, GPL-3.0-or-later): the
// WarcraftXL DLL bakes a virtual model per display ID and points the hand-item record at it; the
// native client has no virtual files, so Resolve() returns the same decision (model, geoset filter,
// texture replacements) for the attach code to apply directly.
namespace WxlWeaponSidecar {
struct GeosetSpec {
    bool filter = false;           // false: every geoset the model has
    std::vector<uint16_t> ids;     // when filter: always starts with 0, then the listed ids
};
struct Weapon {
    uint32_t display = 0;
    std::string model[2];          // Model1Path, Model2Path
    std::string offhandModel[2];   // Model1OffhandPath, Model2OffhandPath
    GeosetSpec geoset[2];
    GeosetSpec offhandGeoset[2];
    bool mirrorOffhand = false;    // "NoAutoMirror" column: opt-in Y-mirror of the offhand model
};
struct Texture {
    uint32_t display = 0;
    uint32_t column = 0;           // 0: Model1Path, 1: Model2Path
    uint32_t type = 0;             // M2 texture type the replacement is keyed on
    std::string path;
};
// What the attach code applies to one hand item.
struct Resolution {
    std::string model;             // archive path replacing the stock hand model
    GeosetSpec geoset;
    std::vector<Texture> textures;
    bool mirror = false;           // Y-mirror requested by the sidecar
};

// Both parsers return false for a malformed CSV or missing required columns without modifying the
// result. Rows are first-seen-wins per display ID (per display/column/type for textures).
bool ParseWeapons(std::string_view csv, std::vector<Weapon>& result);
bool ParseTextures(std::string_view csv, std::vector<Texture>& result);
GeosetSpec ParseGeosets(std::string_view cell);

// Mainhand and ranged use the column-0 bake only when it changes something (a texture row or a
// geoset filter). The offhand prefers its own model, then the mirrored mainhand model, then the
// mainhand result (shields have no separate offhand model).
bool Resolve(const std::vector<Weapon>& weapons, const std::vector<Texture>& textures,
             uint32_t display, bool offhand, Resolution& out);

void LoadLooseSidecars(const std::filesystem::path& dataDirectory,
                       std::vector<Weapon>& weapons, std::vector<Texture>& textures);
bool Resolve(uint32_t display, bool offhand, Resolution& out);
}
#endif
