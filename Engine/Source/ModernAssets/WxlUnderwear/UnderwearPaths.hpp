// Underwear entry validation and texture paths, from the supplied wxl-underwear
// (Copyright (C) 2026 WarcraftXL, GPL-3.0-or-later). Entries are item-style ids: tops are even,
// bottoms odd; 335000-335145 are the stock underwear set, 335200-335279 the "RM" set and
// 365200-365342 the collection tops/bottoms.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

namespace wxl::underwear {

inline bool Allowed(uint32_t entry, uint32_t slot) {
    return !entry || (((entry >= 335000 && entry <= 335145) || (entry >= 335200 && entry <= 335279) ||
                       (entry >= 365200 && entry <= 365229) || (entry >= 365240 && entry <= 365271) ||
                       (entry >= 365280 && entry <= 365287) || (entry >= 365300 && entry <= 365342 && entry % 2 == 0)) &&
                      entry % 2 == slot);
}

inline std::string TexturePath(uint32_t entry, bool top) {
    if (!entry) return {};
    char path[160]{};
    const char* directory = top ? "TorsoUpperTexture" : "LegUpperTexture";
    const char* section = top ? "TU" : "LU";
    if (entry >= 365200) {
        std::snprintf(path, sizeof(path), "Item\\TextureComponents\\%s\\WXL_CT_%u_%s_U.blp", directory, entry + 100000, section);
    } else if (entry < 335200) {
        std::snprintf(path, sizeof(path), "Item\\TextureComponents\\%s\\WXL_UW_%03u_%s_U.blp", directory, (entry - 335000) / 2 + 1, section);
    } else {
        std::snprintf(path, sizeof(path), "Item\\TextureComponents\\%s\\WXL_RM_%02u_%s_U.blp", directory, (entry - 335200) / 2 + 1, section);
    }
    return path;
}

// Only some collection tops carry a torso-lower band.
inline std::string LowerTexturePath(uint32_t entry) {
    if (entry % 2 || !((entry >= 365200 && entry <= 365228) || (entry >= 365300 && entry <= 365342))) return {};
    std::string path = TexturePath(entry, true);
    const size_t directory = path.find("TorsoUpperTexture");
    const size_t section = path.rfind("_TU_U.blp");
    if (directory == std::string::npos || section == std::string::npos) return {};
    path.replace(section, 3, "_TL");
    path.replace(directory, 17, "TorsoLowerTexture");
    return path;
}

}  // namespace wxl::underwear
