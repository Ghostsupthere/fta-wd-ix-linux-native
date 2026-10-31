// Persistence rules for the creation-screen eye-halo choice, from the supplied
// wxl-character-customizer EyeHaloCreationChoice.cpp (Copyright (C) 2026 WarcraftXL,
// GPL-3.0-or-later). The Windows module keeps one INI entry per created character until the server
// acknowledges it; the Win32 INI calls and CharLowerBuffW are replaced by the portable helpers
// below. Keys are fixed-width hex of the lowercased "account|realm|name", so any Unicode name and
// any INI metacharacter stays unambiguous.
#pragma once
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

namespace wxl::customizer::halo {

constexpr uint64_t kMaxAgeSeconds = 604800;  // a pending choice expires after a week

// Strict UTF-8 to UTF-16; false on any malformed or overlong sequence or lone surrogate.
inline bool Utf16(std::string_view text, std::u16string& out) {
    out.clear();
    for (size_t i = 0; i < text.size();) {
        const uint8_t c = static_cast<uint8_t>(text[i]);
        uint32_t cp;
        size_t extra;
        if (c < 0x80) { cp = c; extra = 0; }
        else if (c >= 0xC2 && c <= 0xDF) { cp = c & 0x1F; extra = 1; }
        else if (c >= 0xE0 && c <= 0xEF) { cp = c & 0x0F; extra = 2; }
        else if (c >= 0xF0 && c <= 0xF4) { cp = c & 0x07; extra = 3; }
        else return false;
        if (extra && i + extra >= text.size()) return false;
        for (size_t k = 1; k <= extra; ++k) {
            const uint8_t d = static_cast<uint8_t>(text[i + k]);
            if ((d & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (d & 0x3F);
        }
        if ((extra == 2 && cp < 0x800) || (extra == 3 && cp < 0x10000) || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(cp));
        }
        i += extra + 1;
    }
    return true;
}

// Case folding for ASCII, Latin-1 and Latin Extended-A: enough to keep "Élan" and "élan" together.
inline char16_t Lower(char16_t c) {
    if (c >= u'A' && c <= u'Z') return static_cast<char16_t>(c + 32);
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return static_cast<char16_t>(c + 32);
    if (c >= 0x100 && c <= 0x137 && !(c & 1) && c != 0x130) return static_cast<char16_t>(c + 1);
    if (c >= 0x139 && c <= 0x148 && (c & 1)) return static_cast<char16_t>(c + 1);
    if (c >= 0x14A && c <= 0x177 && !(c & 1)) return static_cast<char16_t>(c + 1);
    return c;
}

inline std::string Key(const std::string& account, const std::string& realm, const std::string& name) {
    if (account.empty() || realm.empty() || name.empty()) return {};
    std::u16string units;
    if (!Utf16(account + "|" + realm + "|" + name, units)) return {};
    std::string key;
    constexpr char digits[] = "0123456789abcdef";
    for (char16_t unit : units) {
        const char16_t lowered = Lower(unit);
        for (int shift = 12; shift >= 0; shift -= 4) key.push_back(digits[(lowered >> shift) & 15]);
    }
    return key;
}

inline bool IsHaloRace(unsigned race) { return race == 10 || race == 23 || race == 25; }

inline std::string FormatEntry(unsigned theme, uint64_t created, unsigned race, unsigned sex) {
    char value[96];
    std::snprintf(value, sizeof(value), "1|%u|%llu|%u|%u", theme, static_cast<unsigned long long>(created), race, sex);
    return value;
}

// Exactly "1|theme|created|race|sex", fresh enough, with a supported race and theme.
inline bool ParseEntry(std::string_view value, unsigned& theme, unsigned& race, unsigned& sex, uint64_t now) {
    unsigned long long fields[5];
    size_t at = 0;
    for (int index = 0; index < 5; ++index) {
        if (at >= value.size() || value[at] < '0' || value[at] > '9') return false;
        unsigned long long number = 0;
        size_t digits = 0;
        while (at < value.size() && value[at] >= '0' && value[at] <= '9') {
            if (++digits > 19) return false;
            number = number * 10 + static_cast<unsigned>(value[at++] - '0');
        }
        fields[index] = number;
        if (index < 4) {
            if (at >= value.size() || value[at] != '|') return false;
            ++at;
        }
    }
    if (at != value.size()) return false;
    theme = static_cast<unsigned>(fields[1]);
    race = static_cast<unsigned>(fields[3]);
    sex = static_cast<unsigned>(fields[4]);
    return fields[0] == 1 && fields[1] <= 5 && IsHaloRace(race) && fields[4] <= 1 && fields[3] < 256 &&
           fields[2] <= now && now - fields[2] <= kMaxAgeSeconds;
}

// --- minimal INI text editing (sections and key=value lines; everything else is preserved) ---

inline bool SectionLine(const std::string& line, std::string& name) {
    size_t a = line.find_first_not_of(" \t");
    size_t b = line.find_last_not_of(" \t\r");
    if (a == std::string::npos || line[a] != '[' || line[b] != ']' || b <= a) return false;
    name = line.substr(a + 1, b - a - 1);
    return true;
}

inline std::optional<std::string> IniGet(const std::string& text, const std::string& section, const std::string& key) {
    bool inside = false;
    for (size_t at = 0; at < text.size();) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        std::string name;
        if (SectionLine(line, name)) { inside = name == section; continue; }
        if (!inside) continue;
        const size_t equals = line.find('=');
        if (equals == std::string::npos || line.substr(0, equals) != key) continue;
        std::string value = line.substr(equals + 1);
        while (!value.empty() && (value.back() == '\r' || value.back() == ' ')) value.pop_back();
        return value;
    }
    return std::nullopt;
}

// Sets (or, with a null value, removes) one key; creates the section when needed.
inline std::string IniSet(const std::string& text, const std::string& section, const std::string& key, const char* value) {
    std::string out;
    bool inside = false, seenSection = false, written = false;
    auto flush = [&] {
        if (inside && !written && value) { out += key + "=" + value + "\n"; written = true; }
    };
    for (size_t at = 0; at < text.size();) {
        size_t end = text.find('\n', at);
        const bool last = end == std::string::npos;
        if (last) end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        std::string name;
        if (SectionLine(line, name)) {
            flush();
            inside = name == section;
            seenSection = seenSection || inside;
            out += line + "\n";
            continue;
        }
        if (inside) {
            const size_t equals = line.find('=');
            if (equals != std::string::npos && line.substr(0, equals) == key) {
                if (value && !written) { out += key + "=" + value + "\n"; written = true; }
                continue;
            }
        }
        if (!(last && line.empty())) out += line + "\n";
    }
    flush();
    if (!seenSection && value) out += "[" + section + "]\n" + key + "=" + value + "\n";
    return out;
}

}  // namespace wxl::customizer::halo
