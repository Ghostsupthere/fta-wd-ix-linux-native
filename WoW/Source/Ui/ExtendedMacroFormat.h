// This format is also used by AzerothCore's ExtendedMacroFormat.h.
#pragma once

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ExtendedMacros {

inline constexpr uint32_t DefaultCharacters = 2048;
inline constexpr uint32_t SupportedCharacters = 4096;
inline constexpr uint32_t CollectionBytes = 60 * 1024;
inline constexpr uint32_t CommandBudget = 256;
inline constexpr uint32_t CharacterIDBase = 0x01000000;
inline constexpr uint32_t AccountWireType = 8;
inline constexpr uint32_t CharacterWireType = 9;
inline constexpr uint8_t AccountDataCapability = 0x81;
inline constexpr uint32_t MaxClientPacket = 63 * 1024;
inline constexpr std::string_view Magic = "FTA MACROS 1\n";

enum class SaveResult : uint32_t { Saved, Conflict, Invalid, Busy, DatabaseError, OverCapacity };

inline constexpr bool IsWireType(uint32_t type) {
    return type == AccountWireType || type == CharacterWireType;
}

inline constexpr uint32_t Scope(uint32_t wireType) {
    return wireType - AccountWireType;
}

inline constexpr uint32_t SlotCount(uint32_t scope) {
    return scope ? 18 : 36;
}

inline constexpr uint32_t IDBase(uint32_t scope) {
    return scope ? CharacterIDBase : 0;
}

inline bool ReadUnsigned(std::string_view text, uint32_t& value) {
    if (text.empty()) {
        return false;
    }
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

// Return -1 on embedded NUL or malformed UTF-8. Each Unicode scalar counts once.
inline int32_t CharacterCount(std::string_view text) {
    if (text.size() > INT32_MAX) {
        return -1;
    }
    int32_t count = 0;
    for (size_t i = 0; i < text.size(); ++count) {
        const auto lead = static_cast<uint8_t>(text[i++]);
        if (lead && lead < 0x80) {
            continue;
        }
        if (lead < 0xC2 || lead > 0xF4) {
            return -1;
        }
        const size_t continuation = lead < 0xE0 ? 1 : lead < 0xF0 ? 2 : 3;
        if (continuation > text.size() - i) {
            return -1;
        }
        const auto first = static_cast<uint8_t>(text[i]);
        if ((lead == 0xE0 && first < 0xA0) || (lead == 0xED && first >= 0xA0) ||
            (lead == 0xF0 && first < 0x90) || (lead == 0xF4 && first >= 0x90)) {
            return -1;
        }
        for (size_t j = 0; j < continuation; ++j) {
            if ((static_cast<uint8_t>(text[i++]) & 0xC0) != 0x80) {
                return -1;
            }
        }
    }
    return count;
}

inline std::string_view NextLine(std::string_view& text) {
    const auto end = text.find_first_of("\r\n");
    if (end == std::string_view::npos) {
        const auto line = text;
        text.remove_prefix(text.size());
        return line;
    }
    const auto line = text.substr(0, end);
    const size_t separator = text[end] == '\r' && end + 1 < text.size() && text[end + 1] == '\n' ? 2 : 1;
    text.remove_prefix(end + separator);
    return line;
}

inline bool IsCommand(std::string_view line) {
    return !line.empty() && line.front() != '-' && line.front() != '#';
}

inline bool WithinCommandBudget(std::string_view body) {
    uint32_t commands = 0;
    while (!body.empty()) {
        if (IsCommand(NextLine(body)) && ++commands > CommandBudget) {
            return false;
        }
    }
    return true;
}

struct Record {
    uint32_t id;
    std::string_view name;
    std::string_view icon;
    std::string_view body;
};

inline size_t DecimalDigits(size_t value) {
    size_t digits = 1;
    while (value >= 10) {
        value /= 10;
        ++digits;
    }
    return digits;
}

inline size_t RecordBytes(const Record& record) {
    return 5 + DecimalDigits(record.id) + DecimalDigits(record.name.size()) +
        DecimalDigits(record.icon.size()) + DecimalDigits(record.body.size()) +
        record.name.size() + record.icon.size() + record.body.size();
}

inline void AppendRecord(std::string& data, const Record& record) {
    data += std::to_string(record.id);
    data += ' ';
    data += std::to_string(record.name.size());
    data += ' ';
    data += std::to_string(record.icon.size());
    data += ' ';
    data += std::to_string(record.body.size());
    data += '\n';
    data += record.name;
    data += record.icon;
    data += record.body;
    data += '\n';
}

// Record lengths frame verbatim bodies, including blank lines and the legacy "END" delimiter.
// Views borrow the input. A successful return is the only valid decoded collection.
inline bool Decode(std::string_view data, uint32_t scope, std::vector<Record>& records,
    uint32_t maxCharacters = SupportedCharacters) {
    records.clear();
    if (data.size() > CollectionBytes || data.substr(0, Magic.size()) != Magic) {
        return false;
    }
    data.remove_prefix(Magic.size());
    while (!data.empty()) {
        const size_t end = data.find('\n');
        if (end == std::string_view::npos || end > 40 || records.size() >= SlotCount(scope)) {
            return false;
        }
        auto header = data.substr(0, end);
        data.remove_prefix(end + 1);
        uint32_t values[4]{};
        for (size_t i = 0; i < 4; ++i) {
            const auto space = header.find(' ');
            if ((space == std::string_view::npos) != (i == 3) ||
                !ReadUnsigned(header.substr(0, space), values[i])) {
                return false;
            }
            if (i != 3) {
                header.remove_prefix(space + 1);
            }
        }
        const auto [id, nameSize, iconSize, bodySize] = values;
        if (id <= IDBase(scope) || id - IDBase(scope) >= CharacterIDBase ||
            nameSize == 0 || nameSize >= 64 || iconSize >= 256 || bodySize > maxCharacters * 4 ||
            std::any_of(records.begin(), records.end(), [id](Record other) { return other.id == id; })) {
            return false;
        }
        const size_t size = static_cast<size_t>(nameSize) + iconSize + bodySize;
        if (data.size() <= size || data[size] != '\n' || data.substr(0, size).find('\0') != std::string_view::npos) {
            return false;
        }
        const auto body = data.substr(nameSize + iconSize, bodySize);
        const int32_t count = CharacterCount(body);
        if (count < 0 || static_cast<uint32_t>(count) > maxCharacters || !WithinCommandBudget(body)) {
            return false;
        }
        records.push_back({ id, data.substr(0, nameSize), data.substr(nameSize, iconSize), body });
        data.remove_prefix(size + 1);
    }
    return true;
}

} // namespace ExtendedMacros
