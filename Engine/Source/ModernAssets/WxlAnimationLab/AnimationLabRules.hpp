#pragma once
// Pure rules for the Character Animation Studio port (WarcraftXL wxl-character-animation-lab,
// GPL-3.0-or-later, Copyright (C) 2026 WarcraftXL): which model paths are playable race/gender
// models, which animation requests are idle-ready or protected motion, and the client's
// quadratic-probe sequence lookup. No engine types, so it is covered by tests/wxl_animation_lab_rules_test.cpp.
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>

namespace wxl::animlab {

struct PlayableModel {
    uint32_t raceId;
    uint32_t genderId;
    const char* path;
    const char* raceName;
    const char* genderName;
};

inline constexpr PlayableModel kPlayableModels[] = {
    {1, 0, "Character\\Human\\Male\\HumanMale", "Human", "Male"},
    {1, 1, "Character\\Human\\Female\\HumanFemale", "Human", "Female"},
    {2, 0, "Character\\Orc\\Male\\OrcMale", "Orc", "Male"},
    {2, 1, "Character\\Orc\\Female\\OrcFemale", "Orc", "Female"},
    {3, 0, "Character\\Dwarf\\Male\\DwarfMale", "Dwarf", "Male"},
    {3, 1, "Character\\Dwarf\\Female\\DwarfFemale", "Dwarf", "Female"},
    {4, 0, "Character\\NightElf\\Male\\NightElfMale", "Night Elf", "Male"},
    {4, 1, "Character\\NightElf\\Female\\NightElfFemale", "Night Elf", "Female"},
    {5, 0, "Character\\Scourge\\Male\\ScourgeMale", "Undead", "Male"},
    {5, 1, "Character\\Scourge\\Female\\ScourgeFemale", "Undead", "Female"},
    {6, 0, "Character\\Tauren\\Male\\TaurenMale", "Tauren", "Male"},
    {6, 1, "Character\\Tauren\\Female\\TaurenFemale", "Tauren", "Female"},
    {7, 0, "Character\\Gnome\\Male\\GnomeMale", "Gnome", "Male"},
    {7, 1, "Character\\Gnome\\Female\\GnomeFemale", "Gnome", "Female"},
    {8, 0, "Character\\Troll\\Male\\TrollMale", "Troll", "Male"},
    {8, 1, "Character\\Troll\\Female\\TrollFemale", "Troll", "Female"},
    {9, 0, "Character\\GoblinPlayer\\Male\\GoblinM", "Goblin", "Male"},
    {9, 1, "Character\\GoblinPlayer\\Female\\GoblinFM", "Goblin", "Female"},
    {10, 0, "Character\\BloodElf\\Male\\BloodElfMale", "Blood Elf", "Male"},
    {10, 1, "Character\\BloodElf\\Female\\BloodElfFemale", "Blood Elf", "Female"},
    {11, 0, "Character\\Draenei\\Male\\DraeneiMale", "Draenei", "Male"},
    {11, 1, "Character\\Draenei\\Female\\DraeneiFemale", "Draenei", "Female"},
    {22, 0, "Character\\WorgenPlayer\\Male\\WorgenMale", "Worgen", "Male"},
    {22, 1, "Character\\WorgenPlayer\\Female\\WorgenFemale", "Worgen", "Female"},
    {23, 0, "Character\\VoidElfPlayer\\Male\\VoidElfMale", "Void Elf", "Male"},
    {23, 1, "Character\\VoidElfPlayer\\Female\\VoidElfFemale", "Void Elf", "Female"},
    {24, 0, "Character\\DarkIronDwarfPlayer\\Male\\DarkIronDwarfMale", "Dark Iron Dwarf", "Male"},
    {24, 1, "Character\\DarkIronDwarfPlayer\\Female\\DarkIronDwarfFemale", "Dark Iron Dwarf", "Female"},
    {25, 0, "Character\\HighElfPlayer\\Male\\HighElfMale", "High Elf", "Male"},
    {25, 1, "Character\\HighElfPlayer\\Female\\HighElfFemale", "High Elf", "Female"},
    {26, 0, "Character\\EredarPlayer\\Male\\EredarMale", "Eredar", "Male"},
    {26, 1, "Character\\EredarPlayer\\Female\\EredarFemale", "Eredar", "Female"},
};
static_assert(sizeof(kPlayableModels) / sizeof(kPlayableModels[0]) == 32,
              "Playable race/gender classifier must remain exhaustive");

// These older models take part only in the Levitate / Wraith Walk hover presentation.
inline constexpr PlayableModel kHoverFallbackModels[] = {
    {14, 0, "Character\\Broken\\Male\\BrokenMale", "Broken", "Male"},
    {14, 1, "Character\\Broken\\Female\\BrokenFemale", "Broken", "Female"},
    {17, 0, "Character\\Tuskarr\\Male\\TuskarrMale", "Tuskarr", "Male"},
};

inline constexpr size_t kModelPathCapacity = 276;

inline int CompareNoCase(const char* a, const char* b, size_t limit = static_cast<size_t>(-1)) {
    for (size_t i = 0; i < limit; ++i) {
        const int ca = std::tolower(static_cast<unsigned char>(a[i]));
        const int cb = std::tolower(static_cast<unsigned char>(b[i]));
        if (ca != cb) return ca < cb ? -1 : 1;
        if (!ca) return 0;
    }
    return 0;
}

// Forward slashes become backslashes and a trailing .m2/.mdx/.mdl is dropped.
inline bool NormalizeModelPath(const char* path, char (&normalized)[kModelPathCapacity]) {
    if (!path || !*path) return false;
    size_t length = 0;
    while (length + 1 < sizeof(normalized) && path[length]) {
        normalized[length] = path[length] == '/' ? '\\' : path[length];
        ++length;
    }
    normalized[length] = '\0';
    for (const char* extension : {".m2", ".mdx", ".mdl"}) {
        const size_t extensionLength = std::strlen(extension);
        if (length >= extensionLength && CompareNoCase(normalized + length - extensionLength, extension) == 0) {
            normalized[length - extensionLength] = '\0';
            break;
        }
    }
    return normalized[0] != '\0';
}

// Exact canonical path, or the same path with an "_HD" suffix.
inline const PlayableModel* ClassifyPlayablePath(const char* path) {
    char normalized[kModelPathCapacity] = {};
    if (!NormalizeModelPath(path, normalized)) return nullptr;
    for (const PlayableModel& playable : kPlayableModels) {
        if (CompareNoCase(normalized, playable.path) == 0) return &playable;
        const size_t baseLength = std::strlen(playable.path);
        if (CompareNoCase(normalized, playable.path, baseLength) == 0 &&
            CompareNoCase(normalized + baseLength, "_HD") == 0) {
            return &playable;
        }
    }
    return nullptr;
}

inline const PlayableModel* ClassifyHoverFallbackPath(const char* path) {
    char normalized[kModelPathCapacity] = {};
    if (!NormalizeModelPath(path, normalized)) return nullptr;
    for (const PlayableModel& model : kHoverFallbackModels) {
        if (CompareNoCase(normalized, model.path) == 0) return &model;
    }
    return nullptr;
}

inline bool IsPlayableClass(uint32_t classId) {
    return classId >= 1 && classId <= 11 && classId != 10;
}

inline bool IsIdleReadyAnimation(uint32_t animationId) {
    switch (animationId) {
        case 0: case 8: case 15:
        case 25: case 26: case 27: case 28: case 29:
        case 48: case 51: case 52: case 56:
        case 108: case 109: case 110: case 111:
        case 120: case 158: case 190:
        case 229: case 237: case 244:
        case 254: case 255: case 256: case 257: case 258:
        case 277: case 280: case 281: case 285:
        case 337: case 338: case 339: case 340:
        case 349: case 387: case 419:
        case 476: case 478: case 479: case 481:
            return true;
        default:
            return false;
    }
}

inline bool IsProtectedMotion(uint32_t animationId) {
    switch (animationId) {
        case 1: case 3: case 4: case 5: case 6: case 7:
        case 11: case 12: case 13: case 14:
        case 37: case 38: case 39: case 40:
        case 41: case 42: case 43: case 44: case 45:
        case 91: case 92: case 93: case 94:
        case 119: case 121: case 127: case 131: case 132:
        case 135: case 137: case 143: case 144: case 145: case 157:
        case 183: case 184: case 187: case 192: case 193: case 196:
        case 200: case 201: case 202: case 223: case 224:
        case 230: case 232: case 233: case 234: case 235: case 236:
        case 240: case 241: case 242: case 243:
        case 266: case 267: case 268: case 269:
        case 270: case 271: case 272: case 273: case 274:
        case 320: case 321: case 322: case 323:
        case 348: case 350: case 356: case 360: case 361: case 364:
        case 366: case 372: case 373: case 374: case 386:
        case 416: case 421: case 422: case 425:
        case 429: case 430: case 431: case 452: case 453:
        case 458: case 459: case 460: case 461: case 462: case 463:
        case 464: case 465:
        case 466: case 467: case 468: case 469: case 470: case 471:
        case 472: case 473: case 474: case 475:
        case 484: case 485: case 488: case 489:
        case 498: case 499: case 500: case 501: case 505:
            return true;
        default:
            return false;
    }
}

inline constexpr uint32_t kRunAnimationId = 5;
inline constexpr uint32_t kWalkBackwardsAnimationId = 13;
inline constexpr uint32_t kSprintAnimationId = 143;
inline constexpr uint32_t kHoverAnimationId = 193;
inline constexpr uint32_t kLevitateSpellId = 1706;
inline constexpr uint32_t kWraithWalkSpellId = 979600;

inline bool IsLevitateMovement(uint32_t animationId) {
    return animationId == kRunAnimationId || animationId == kWalkBackwardsAnimationId ||
           animationId == kSprintAnimationId;
}

inline bool IsHoverRequest(uint32_t animationId, bool wraith) {
    return IsLevitateMovement(animationId) || animationId == 4 || animationId == 0 ||
           (wraith && animationId == kHoverAnimationId);
}

enum class SequenceWeaponPolicy : uint32_t {
    Inherit = 0,
    LegacyNoAction = 1,
    LegacyFlag4 = 4,
    Sheath = 16,
    Draw = 32,
};

inline bool IsSequenceWeaponPolicy(uint32_t policy) {
    switch (static_cast<SequenceWeaponPolicy>(policy)) {
        case SequenceWeaponPolicy::Inherit:
        case SequenceWeaponPolicy::LegacyNoAction:
        case SequenceWeaponPolicy::LegacyFlag4:
        case SequenceWeaponPolicy::Sheath:
        case SequenceWeaponPolicy::Draw:
            return true;
        default:
            return false;
    }
}

// Policies that need a weapon call after the stock request has applied its own weapon flags.
inline bool RequiresPostWeaponPolicy(uint32_t policy) {
    return policy == static_cast<uint32_t>(SequenceWeaponPolicy::LegacyFlag4) ||
           policy == static_cast<uint32_t>(SequenceWeaponPolicy::Sheath) ||
           policy == static_cast<uint32_t>(SequenceWeaponPolicy::Draw);
}

// Millisecond deadlines on a wrapping 32-bit clock.
inline bool DeadlineReached(uint32_t now, uint32_t deadline) {
    return static_cast<int32_t>(now - deadline) >= 0;
}

// The model stores sequenceLookup as a quadratic-probe hash of AnimationData ids. This follows the
// client's cumulative probe order but bounds the walk and validates every index it reads, so a
// malformed custom model fails closed instead of looping or reading out of range.
// idOf(index) returns the AnimationData id of sequence `index`. Returns the sequence index or -1.
template <typename IdOf>
inline int32_t FindSequenceByAnimationId(const uint16_t* lookup, uint32_t lookupCount, uint32_t sequenceCount,
                                         uint32_t animationId, IdOf&& idOf) {
    if (animationId > std::numeric_limits<uint16_t>::max() || !lookup || !lookupCount) return -1;
    uint32_t slot = animationId % lookupCount;
    for (uint32_t probe = 0; probe < lookupCount; ++probe) {
        const uint16_t raw = lookup[slot];
        if (raw == std::numeric_limits<uint16_t>::max() || raw >= sequenceCount) return -1;
        if (idOf(raw) == animationId) return raw;
        const uint64_t step = uint64_t(probe + 1u) * uint64_t(probe + 1u);
        slot = static_cast<uint32_t>((uint64_t(slot) + step) % lookupCount);
    }
    return -1;
}

}  // namespace wxl::animlab
