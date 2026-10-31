#pragma once
// DoodadLightProperties.cdbc / DoodadLightAssignment.cdbc (the supplied wxl-living-azeroth
// features/DoodadLightTable.*): lights added to placed models by data. Plain classic WDBC files:
// 'WDBC', rows, columns, stride (= columns x 4), string-block size, rows, strings. This is the
// bounded parser and the validation of the supplied loader without its function-pointer API.
// Adapted from Morfium's Living Azeroth (GPL-3.0-or-later).
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace wxl::surfacelights
{
    enum Type : uint32_t { kPoint = 0, kSpot = 1 };
    enum Attach : uint32_t { kOrigin = 0, kAttachmentPoint = 1, kBone = 2, kParticleEmitter = 3, kAttachCount };
    enum AssignmentFlag : uint32_t { kSuppressModelLights = 0x1 };
    enum PropertiesFlag : uint32_t { kBakeOcclusion = 0x1 };
    enum FlickerMode : int32_t { kFlickerDefault = -1, kFlickerOff = 0, kFlickerSmooth = 1, kFlickerNoise = 2, kFlickerSteps = 3 };

    struct Properties
    {
        uint32_t id = 0;
        std::string name;
        uint32_t type = kPoint;
        uint32_t color = 0xFFA050;
        float intensity = 1.4f;
        float radius = -1.0f;
        float falloff = -1.0f;
        float innerAngle = 20.0f;
        float outerAngle = 35.0f;
        int32_t flickerMode = kFlickerDefault;
        float flickerSpeed = -1.0f;
        float flickerAmount = -1.0f;
        uint32_t flags = 0;
    };

    struct Assignment
    {
        uint32_t id = 0;
        std::string modelPath;
        uint32_t attachType = kOrigin;
        int32_t attachIndex = 0;
        std::string attachName;
        float offset[3] = {};
        float direction[3] = {0.0f, 0.0f, -1.0f};
        uint32_t lightId = 0;
        uint32_t flags = 0;
    };

    // A parsed WDBC: raw little-endian 32-bit cells.
    struct Wdbc
    {
        std::vector<uint8_t> bytes;
        uint32_t rows = 0, columns = 0;
        size_t strings = 0, stringSize = 0;

        uint32_t Cell(uint32_t row, uint32_t column) const
        {
            uint32_t v;
            std::memcpy(&v, bytes.data() + 20 + (static_cast<size_t>(row) * columns + column) * 4, 4);
            return v;
        }
        float Float(uint32_t row, uint32_t column) const
        {
            const uint32_t v = Cell(row, column);
            float f;
            std::memcpy(&f, &v, 4);
            return f;
        }
        // The string at a cell's offset; empty when the offset is out of range or unterminated.
        std::string String(uint32_t row, uint32_t column) const
        {
            const uint32_t offset = Cell(row, column);
            if (offset >= stringSize) return {};
            const char* start = reinterpret_cast<const char*>(bytes.data() + strings + offset);
            const void* end = std::memchr(start, 0, stringSize - offset);
            return end ? std::string(start, static_cast<const char*>(end)) : std::string();
        }
    };

    constexpr size_t kMaxTableBytes = 4u * 1024u * 1024u;

    // The supplied reader's checks: magic, size bounds, stride = columns x 4, strings present, exact
    // total size, columns in [1, 64], unique ids in column 0, and every string column of every row
    // pointing at a terminated string (a bad one rejects the whole table, like the supplied loader).
    // Columns beyond the file's are skipped: tables are append-only, so older files are shorter.
    inline bool ParseWdbc(const uint8_t* data, size_t size, Wdbc& out, std::string& reason,
                          std::initializer_list<uint32_t> stringColumns = {})
    {
        if (!data || size < 21 || size > kMaxTableBytes) { reason = "invalid table size"; return false; }
        if (std::memcmp(data, "WDBC", 4) != 0) { reason = "invalid WDBC header"; return false; }
        auto word = [&](size_t at) { uint32_t v; std::memcpy(&v, data + at, 4); return v; };
        const uint32_t rows = word(4), columns = word(8), stride = word(12), strings = word(16);
        const uint64_t recordsEnd = 20ull + uint64_t(rows) * stride;
        if (!columns || columns > 64 || stride != columns * 4 || !strings || recordsEnd + strings != size)
        {
            reason = "invalid record or string bounds";
            return false;
        }
        out.bytes.assign(data, data + size);
        out.rows = rows;
        out.columns = columns;
        out.strings = static_cast<size_t>(recordsEnd);
        out.stringSize = strings;
        std::unordered_set<uint32_t> ids;
        for (uint32_t r = 0; r < rows; ++r)
        {
            if (!ids.insert(out.Cell(r, 0)).second) { reason = "duplicate row ID"; return false; }
            for (const uint32_t column : stringColumns)
            {
                if (column >= columns) continue;
                const uint32_t offset = out.Cell(r, column);
                if (offset >= strings || !std::memchr(data + recordsEnd + offset, 0, strings - offset))
                {
                    reason = "invalid string offset";
                    return false;
                }
            }
        }
        return true;
    }

    inline std::string NormalizePath(std::string_view path)
    {
        std::string s(path);
        for (char& c : s)
        {
            if (c == '/') c = '\\';
            else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
        return s;
    }

    class LightTables
    {
    public:
        // Properties columns: ID Name Type Color Intensity Radius Falloff InnerAngle OuterAngle
        // FlickerMode FlickerSpeed FlickerAmount Flags. A file with fewer columns keeps the defaults
        // for the missing ones, as the supplied loader does (it loads the fields the file has).
        void LoadProperties(const Wdbc& table)
        {
            auto has = [&](uint32_t column) { return table.columns > column; };
            for (uint32_t r = 0; r < table.rows; ++r)
            {
                Properties p;
                p.id = table.Cell(r, 0);
                if (has(1))
                {
                    // A string offset outside the block rejects the row's text, not the table.
                    p.name = table.String(r, 1);
                }
                if (has(2)) p.type = table.Cell(r, 2);
                if (has(3)) p.color = table.Cell(r, 3);
                if (has(4)) p.intensity = table.Float(r, 4);
                p.radius = has(5) ? table.Float(r, 5) : -1.0f;
                p.falloff = has(6) ? table.Float(r, 6) : -1.0f;
                if (has(7)) p.innerAngle = table.Float(r, 7);
                if (has(8)) p.outerAngle = table.Float(r, 8);
                p.flickerMode = has(9) ? static_cast<int32_t>(table.Cell(r, 9)) : kFlickerDefault;
                p.flickerSpeed = has(10) ? table.Float(r, 10) : -1.0f;
                p.flickerAmount = has(11) ? table.Float(r, 11) : -1.0f;
                if (has(12)) p.flags = table.Cell(r, 12);
                auto finiteRange = [](float v, float lo, float hi) { return std::isfinite(v) && v >= lo && v <= hi; };
                const bool valid = p.id != 0 && p.type <= kSpot && finiteRange(p.intensity, 0, 16) &&
                    finiteRange(p.radius, -1, 128) && finiteRange(p.falloff, -1, 16) &&
                    finiteRange(p.innerAngle, 0, 180) && finiteRange(p.outerAngle, p.innerAngle, 180) &&
                    finiteRange(p.flickerSpeed, -1, 20) && finiteRange(p.flickerAmount, -1, 1) &&
                    p.flickerMode >= kFlickerDefault && p.flickerMode <= kFlickerSteps;
                if (!valid) { ++rejected_; continue; }
                properties_.push_back(p);
            }
        }

        // Assignment columns: ID ModelPath AttachType AttachIndex AttachName OffsetX/Y/Z
        // DirectionX/Y/Z LightID Flags. Call after LoadProperties: a row naming an unknown light is
        // rejected whole, including its suppression flag.
        void LoadAssignments(const Wdbc& table)
        {
            auto has = [&](uint32_t column) { return table.columns > column; };
            for (uint32_t r = 0; r < table.rows; ++r)
            {
                Assignment a;
                a.id = table.Cell(r, 0);
                if (has(1)) a.modelPath = table.String(r, 1);
                a.attachType = has(2) ? table.Cell(r, 2) : kOrigin;
                a.attachIndex = has(3) ? static_cast<int32_t>(table.Cell(r, 3)) : 0;
                if (has(4)) a.attachName = table.String(r, 4);
                for (uint32_t axis = 0; axis < 3; ++axis)
                {
                    a.offset[axis] = has(5 + axis) ? table.Float(r, 5 + axis) : 0.0f;
                    a.direction[axis] = has(8 + axis) ? table.Float(r, 8 + axis) : (axis == 2 ? -1.0f : 0.0f);
                }
                a.lightId = has(11) ? table.Cell(r, 11) : 0;
                a.flags = has(12) ? table.Cell(r, 12) : 0;
                bool valid = a.id != 0 && !a.modelPath.empty() && a.attachType < kAttachCount && a.attachIndex >= 0 &&
                    (a.lightId == 0 || FindProperties(a.lightId));
                for (unsigned axis = 0; axis < 3; ++axis)
                    valid = valid && std::isfinite(a.offset[axis]) && std::fabs(a.offset[axis]) <= 4096 &&
                        std::isfinite(a.direction[axis]);
                if (!valid) { ++rejected_; continue; }
                assignments_.push_back(a);
            }
            Reindex();
        }

        void Clear()
        {
            properties_.clear();
            assignments_.clear();
            byModel_.clear();
            rejected_ = 0;
            ++generation_;
        }

        const Properties* FindProperties(uint32_t id) const
        {
            for (const Properties& p : properties_)
                if (p.id == id) return &p;
            return nullptr;
        }

        std::vector<const Assignment*> ForModel(const std::string& normalizedPath) const
        {
            std::vector<const Assignment*> out;
            const auto it = byModel_.find(normalizedPath);
            if (it != byModel_.end())
                for (size_t k : it->second) out.push_back(&assignments_[k]);
            return out;
        }

        const std::vector<Properties>& AllProperties() const { return properties_; }
        const std::vector<Assignment>& AllAssignments() const { return assignments_; }
        uint32_t Generation() const { return generation_; }
        unsigned Rejected() const { return rejected_; }

    private:
        void Reindex()
        {
            byModel_.clear();
            for (size_t k = 0; k < assignments_.size(); ++k) byModel_[NormalizePath(assignments_[k].modelPath)].push_back(k);
            ++generation_;
        }

        std::vector<Properties> properties_;
        std::vector<Assignment> assignments_;
        std::map<std::string, std::vector<size_t>> byModel_;
        unsigned rejected_ = 0;
        uint32_t generation_ = 1;
    };
}
