#pragma once

#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace EquipmentFit {

enum class Slot { Any, Helm, Shoulders };
enum class IdKind { None, Item, Display };

inline char Lower(char c) { return c >= 'A' && c <= 'Z' ? char(c + ('a' - 'A')) : c; }
inline bool Equal(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) if (Lower(a[i]) != Lower(b[i])) return false;
    return true;
}
inline std::string_view Trim(std::string_view s) {
    auto first = s.find_first_not_of(" \t\r\n");
    if (first == s.npos) return {};
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}
inline bool Separator(char c) { return c == ' ' || c == '\t' || c == '_' || c == '-'; }
inline bool TokenEqual(std::string_view a, std::string_view b) {
    std::size_t i = 0, j = 0;
    for (;;) {
        while (i < a.size() && Separator(a[i])) ++i;
        while (j < b.size() && Separator(b[j])) ++j;
        if (i == a.size() || j == b.size()) return i == a.size() && j == b.size();
        if (Lower(a[i++]) != Lower(b[j++])) return false;
    }
}
inline bool Any(std::string_view value) {
    return value == "*" || Equal(value, "any") || Equal(value, "all") || Equal(value, "both");
}

struct BodyName {
    std::string_view race;
    unsigned sex = 0;
    bool valid = false;
};
inline BodyName ParseBodyName(std::string_view path) {
    auto slash = path.find_last_of("/\\");
    auto name = path.substr(slash == path.npos ? 0 : slash + 1);
    auto dot = name.find_last_of('.');
    if (dot == name.npos || (!Equal(name.substr(dot), ".m2") && !Equal(name.substr(dot), ".mdx"))) return {};
    name = name.substr(0, dot);
    if (name.size() >= 3 && Equal(name.substr(name.size() - 3), "_hd")) name.remove_suffix(3);
    if (name.size() > 6 && Equal(name.substr(name.size() - 6), "female"))
        return {name.substr(0, name.size() - 6), 1, true};
    if (name.size() > 4 && Equal(name.substr(name.size() - 4), "male"))
        return {name.substr(0, name.size() - 4), 0, true};
    return {};
}

struct Context {
    Slot slot = Slot::Helm;
    std::uint32_t race = 0;
    std::string_view racePrefix;
    std::string_view raceName;
    unsigned sex = 0;
    std::uint32_t item = 0;
    std::uint32_t display = 0;
    bool modern = false;
};

struct Rule {
    Slot slot = Slot::Any;
    std::uint32_t race = 0;
    std::string raceToken;
    int sex = -1;
    int era = -1;
    IdKind idKind = IdKind::None;
    std::uint32_t first = 0, last = 0;
    float scale = 1.0f;

    bool Matches(Context const& c) const {
        if (slot != Slot::Any && slot != c.slot) return false;
        if (race && race != c.race) return false;
        if (!raceToken.empty() && !TokenEqual(raceToken, c.racePrefix) && !TokenEqual(raceToken, c.raceName)) return false;
        if (sex >= 0 && unsigned(sex) != c.sex) return false;
        if (era >= 0 && bool(era) != c.modern) return false;
        if (idKind != IdKind::None) {
            auto id = idKind == IdKind::Item ? c.item : c.display;
            if (!id || id < first || id > last) return false;
        }
        return true;
    }
};

struct Configuration {
    bool enabled = true;
    bool useBuiltInDefaults = true;
    std::vector<Rule> rules;
    std::vector<std::size_t> general;
    std::unordered_map<std::uint32_t, std::vector<std::size_t>> items, displays;

    void Compile() {
        general.clear(); items.clear(); displays.clear();
        for (std::size_t i = 0; i < rules.size(); ++i) {
            auto const& r = rules[i];
            if (r.idKind != IdKind::None && r.first == r.last)
                (r.idKind == IdKind::Item ? items : displays)[r.first].push_back(i);
            else general.push_back(i);
        }
    }
    float Resolve(Context const& context, float builtIn) const {
        if (!enabled) return 1.0f;
        float value = useBuiltInDefaults ? builtIn : 1.0f;
        std::size_t winner = 0;
        bool matched = false;
        auto consider = [&](std::vector<std::size_t> const& candidates) {
            for (auto i : candidates)
                if ((!matched || i > winner) && rules[i].Matches(context)) {
                    winner = i; matched = true; value = rules[i].scale;
                }
        };
        consider(general);
        if (auto it = items.find(context.item); it != items.end()) consider(it->second);
        if (auto it = displays.find(context.display); it != displays.end()) consider(it->second);
        return value;
    }
};

struct ParseResult {
    Configuration configuration;
    bool valid = false;
    std::size_t line = 0;
    std::string error;
};

inline bool Number(std::string_view text, std::uint32_t& value) {
    text = Trim(text);
    if (text.empty()) return false;
    auto p = std::from_chars(text.data(), text.data() + text.size(), value);
    return p.ec == std::errc{} && p.ptr == text.data() + text.size();
}
inline bool Boolean(std::string_view text, bool& value) {
    if (text == "1" || Equal(text, "true")) { value = true; return true; }
    if (text == "0" || Equal(text, "false")) { value = false; return true; }
    return false;
}
inline bool Band(std::string_view text, std::uint32_t& first, std::uint32_t& last) {
    auto dash = text.find('-');
    if (dash == text.npos) {
        if (!Number(text, first)) return false;
        last = first;
    } else if (!Number(text.substr(0, dash), first) || !Number(text.substr(dash + 1), last)) return false;
    return first > 0 && first <= last && last <= std::uint32_t(std::numeric_limits<std::int32_t>::max());
}
inline bool Scale(std::string_view text, float& scale) {
    bool percent = !text.empty() && text.back() == '%';
    if (percent) text = Trim(text.substr(0, text.size() - 1));
    if (text.empty()) return false;
    auto p = std::from_chars(text.data(), text.data() + text.size(), scale);
    if (p.ec != std::errc{} || p.ptr != text.data() + text.size() || !std::isfinite(scale)) return false;
    if (percent) scale /= 100.0f;
    return scale >= .1f && scale <= 4.0f;
}

inline ParseResult Parse(std::string_view input) {
    ParseResult result;
    if (input.size() > 1024 * 1024 || input.find('\0') != input.npos) {
        result.error = "The file exceeds 1 MiB or contains a NUL byte."; return result;
    }
    if (input.substr(0, 3) == "\xEF\xBB\xBF") input.remove_prefix(3);
    enum class Section { None, Settings, Rule } section = Section::None;
    unsigned settingsSeen = 0, ruleSeen = 0;
    bool settingsSection = false;
    Rule rule;
    std::unordered_set<std::string> names;
    auto fail = [&](std::string message) { result.error = std::move(message); return false; };
    auto finish = [&] {
        if (section != Section::Rule) return true;
        if (!(ruleSeen & 64)) return fail("Every rule requires Scale.");
        if (result.configuration.rules.size() >= 2048) return fail("The file exceeds 2048 rules.");
        result.configuration.rules.push_back(std::move(rule));
        return true;
    };
    auto seen = [&](unsigned& bits, unsigned flag) {
        if (bits & flag) return fail("A key is repeated in its section.");
        bits |= flag; return true;
    };
    for (std::size_t start = 0; start < input.size();) {
        ++result.line;
        auto end = input.find('\n', start);
        auto text = input.substr(start, end == input.npos ? input.size() - start : end - start);
        start = end == input.npos ? input.size() : end + 1;
        text = Trim(text.substr(0, text.find_first_of(";#")));
        if (text.empty()) continue;
        if (text.front() == '[') {
            if (text.back() != ']') { fail("Invalid section header."); return result; }
            if (!finish()) return result;
            auto name = Trim(text.substr(1, text.size() - 2));
            if (Equal(name, "EquipmentScale")) {
                if (settingsSection || !result.configuration.rules.empty()) { fail("EquipmentScale must be the first section and appear once."); return result; }
                settingsSection = true; section = Section::Settings;
            } else if (name.size() > 5 && Equal(name.substr(0, 5), "rule ") && settingsSection) {
                auto label = Trim(name.substr(5));
                if (label.empty() || label.size() > 96) { fail("Rule names must contain 1 to 96 characters."); return result; }
                std::string key(label);
                for (auto& c : key) c = Lower(c);
                if (!names.insert(key).second) { fail("Rule names must be unique."); return result; }
                rule = Rule{}; ruleSeen = 0; section = Section::Rule;
            } else { fail("Expected [EquipmentScale] or [rule NAME]."); return result; }
            continue;
        }
        auto equal = text.find('=');
        if (equal == text.npos || section == Section::None) { fail("Expected a key=value inside a section."); return result; }
        auto key = Trim(text.substr(0, equal)), value = Trim(text.substr(equal + 1));
        bool okay = false;
        if (section == Section::Settings) {
            if (Equal(key, "Version")) {
                std::uint32_t version = 0; okay = seen(settingsSeen, 1) && Number(value, version) && version == 1;
            } else if (Equal(key, "Enabled")) okay = seen(settingsSeen, 2) && Boolean(value, result.configuration.enabled);
            else if (Equal(key, "UseBuiltInDefaults")) okay = seen(settingsSeen, 4) && Boolean(value, result.configuration.useBuiltInDefaults);
            else { fail("Unknown EquipmentScale key: " + std::string(key)); return result; }
        } else if (Equal(key, "Slot")) {
            okay = seen(ruleSeen, 1);
            if (Any(value)) rule.slot = Slot::Any;
            else if (Equal(value, "helm") || Equal(value, "helmet") || Equal(value, "head")) rule.slot = Slot::Helm;
            else if (Equal(value, "shoulders") || Equal(value, "shoulder")) rule.slot = Slot::Shoulders;
            else okay = false;
        } else if (Equal(key, "Race")) {
            okay = seen(ruleSeen, 2);
            if (!Any(value)) {
                if (Number(value, rule.race)) okay = okay && rule.race > 0 && rule.race <= 255;
                else {
                    bool letter = false;
                    for (char c : value) {
                        char l = Lower(c);
                        if (l >= 'a' && l <= 'z') letter = true;
                        else if (!(l >= '0' && l <= '9') && !Separator(c)) okay = false;
                    }
                    okay = okay && letter && value.size() <= 64; rule.raceToken.assign(value);
                }
            }
        } else if (Equal(key, "Sex")) {
            okay = seen(ruleSeen, 4);
            if (Any(value)) rule.sex = -1;
            else if (Equal(value, "male") || Equal(value, "m") || value == "0") rule.sex = 0;
            else if (Equal(value, "female") || Equal(value, "f") || value == "1") rule.sex = 1;
            else okay = false;
        } else if (Equal(key, "Era")) {
            okay = seen(ruleSeen, 8);
            if (Any(value)) rule.era = -1;
            else if (Equal(value, "modern") || Equal(value, "cata+") || Equal(value, "cataclysm+")) rule.era = 1;
            else if (Equal(value, "legacy") || Equal(value, "pre-cata")) rule.era = 0;
            else okay = false;
        } else if (Equal(key, "Item") || Equal(key, "Display")) {
            okay = seen(ruleSeen, 16) && Band(value, rule.first, rule.last);
            rule.idKind = Equal(key, "Item") ? IdKind::Item : IdKind::Display;
        } else if (Equal(key, "Scale")) okay = seen(ruleSeen, 64) && Scale(value, rule.scale);
        else { fail("Unknown rule key: " + std::string(key)); return result; }
        if (!okay) {
            if (result.error.empty()) fail("Invalid value for " + std::string(key) + ".");
            return result;
        }
    }
    if (!finish()) return result;
    if (!settingsSection || !(settingsSeen & 1)) { fail("[EquipmentScale] must declare Version=1."); return result; }
    result.configuration.Compile(); result.valid = true; result.line = 0;
    return result;
}

} // namespace EquipmentFit

