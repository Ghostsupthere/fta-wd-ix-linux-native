// Rigid ground-detail classification, adapted from wxl-render-modern
// `src/grass/DetailDoodad.hpp` (IsRigidDetailDoodadName). Copyright (C) 2026 WarcraftXL, GPLv3.
// The terminal-word rules and the Badlands aliases are unchanged; registry reads are not imported
// (the native client reads the record's model path directly).
#pragma once
#include <cstddef>
#include <cstring>

namespace wxl::grass
{
// Terminal rigid-prop words (original IsRigidDetailDoodadName).
inline bool EndsWith(const char* value, size_t length, const char* suffix) {
    const size_t suffixLength = std::strlen(suffix);
    return length >= suffixLength && std::memcmp(value + length - suffixLength, suffix, suffixLength) == 0;
}
inline bool IsRigidDetailDoodadName(const char* name) {
    size_t base = 0;
    for (size_t i = 0; name[i]; ++i) if (name[i] == '\\' || name[i] == '/') base = i + 1;
    char stem[128]; size_t length = 0;
    for (size_t i = base; name[i] && name[i] != '.' && length < sizeof(stem) - 1; ++i) {
        char c = name[i]; if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a'); stem[length++] = c;
    }
    stem[length] = '\0';
    // The installed HD detail pack reuses these Badlands bush slots for rigid clutter.
    static const char* const aliases[] = {"badbus01", "badbus03", "badbus04", "badbus06"};
    for (const char* alias : aliases) if (std::strcmp(stem, alias) == 0) return true;
    while (length && stem[length - 1] >= '0' && stem[length - 1] <= '9') --length;
    while (length && (stem[length - 1] == '_' || stem[length - 1] == '-' || stem[length - 1] == ' ')) --length;
    stem[length] = '\0';
    static const char* const suffixes[] = {"roc", "bon", "stn", "rock", "rocks", "stone", "stones", "boulder", "boulders",
                                            "bone", "bones", "skull", "skulls", "skeleton", "skeletons"};
    for (const char* suffix : suffixes) if (EndsWith(stem, length, suffix)) return true;
    return false;
}
}
