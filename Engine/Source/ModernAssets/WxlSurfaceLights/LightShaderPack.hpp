#pragma once
// Offline-generated pack of the client's own shaders with the surface-light patches applied
// (tools/build-wmo-shader-pack.py --lights). Like the WMO and grass packs it is derived from the
// install's own shader bytecode, so it is generated per install and never committed; entries are
// keyed by a hash of the ORIGINAL bytecode and the runtime never assembles text.
//
// File: 'LITS', version, entry count, then per entry: kind (0 vertex, 1 pixel), group (which family
// produced it: 1 terrain, 2 M2 model, 3 WMO), original size, original hash, patched size in bytes,
// the bytecode. Everything is bounds-checked before anything is published.
#include "Engine/Source/ModernAssets/WxlWmo/WmoShaderPack.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace wxl::surfacelights
{
    constexpr uint32_t kLightPackMagic = 0x5354494C;   // 'LITS'
    constexpr uint32_t kLightPackVersion = 1;
    constexpr uint32_t kMaxLightPackEntries = 8192;

    enum PackGroup : uint8_t { kGroupTerrain = 1, kGroupModel = 2, kGroupWmo = 3 };

    struct LightPackEntry
    {
        uint8_t kind = 0;    // 0 vertex, 1 pixel
        uint8_t group = 0;
        uint64_t originalHash = 0;
        uint32_t originalSize = 0;
        std::vector<uint32_t> bytecode;
    };

    class LightShaderPack
    {
    public:
        static std::vector<uint8_t> Serialize(const std::vector<LightPackEntry>& entries)
        {
            std::vector<uint8_t> out;
            auto put = [&](const void* p, size_t n) { const auto* b = static_cast<const uint8_t*>(p); out.insert(out.end(), b, b + n); };
            auto u32 = [&](uint32_t v) { put(&v, 4); };
            u32(kLightPackMagic);
            u32(kLightPackVersion);
            u32(static_cast<uint32_t>(entries.size()));
            for (const LightPackEntry& e : entries)
            {
                out.push_back(e.kind);
                out.push_back(e.group);
                out.push_back(0);
                out.push_back(0);
                u32(e.originalSize);
                put(&e.originalHash, 8);
                u32(static_cast<uint32_t>(e.bytecode.size() * 4));
                put(e.bytecode.data(), e.bytecode.size() * 4);
            }
            return out;
        }

        bool Parse(const uint8_t* data, size_t size, std::string& error)
        {
            if (!data || size < 12) { error = "truncated pack"; return false; }
            uint32_t magic, version, count;
            std::memcpy(&magic, data, 4); std::memcpy(&version, data + 4, 4); std::memcpy(&count, data + 8, 4);
            if (magic != kLightPackMagic || version != kLightPackVersion) { error = "not a light shader pack"; return false; }
            if (count > kMaxLightPackEntries) { error = "too many entries"; return false; }
            std::vector<LightPackEntry> parsed;
            parsed.reserve(count);
            size_t at = 12;
            for (uint32_t i = 0; i < count; ++i)
            {
                if (size - at < 4 + 4 + 8 + 4 || at > size) { error = "truncated entry"; return false; }
                LightPackEntry e;
                e.kind = data[at];
                e.group = data[at + 1];
                std::memcpy(&e.originalSize, data + at + 4, 4);
                std::memcpy(&e.originalHash, data + at + 8, 8);
                uint32_t bytes;
                std::memcpy(&bytes, data + at + 16, 4);
                at += 20;
                if (e.kind > 1 || e.group < kGroupTerrain || e.group > kGroupWmo) { error = "bad entry header"; return false; }
                if (bytes < 8 || bytes % 4 || bytes > wxl::wmo::kMaxShaderBytes || bytes > size - at) { error = "bad bytecode size"; return false; }
                e.bytecode.resize(bytes / 4);
                std::memcpy(e.bytecode.data(), data + at, bytes);
                // A D3D9 shader token stream starts with a version token (0xFFFE... vertex, 0xFFFF... pixel).
                const uint32_t version = e.bytecode[0];
                const uint32_t expected = e.kind == 0 ? 0xFFFE0000u : 0xFFFF0000u;
                if ((version & 0xFFFF0000u) != expected) { error = "bytecode version token does not match its kind"; return false; }
                at += bytes;
                parsed.push_back(std::move(e));
            }
            if (at != size) { error = "trailing bytes"; return false; }
            entries_ = std::move(parsed);
            index_.clear();
            for (size_t i = 0; i < entries_.size(); ++i)
                index_[Key(entries_[i].kind, entries_[i].originalHash)] = i;
            return true;
        }

        bool Load(const std::string& path, std::string& error)
        {
            FILE* f = std::fopen(path.c_str(), "rb");
            if (!f) { error = "cannot open " + path; return false; }
            std::vector<uint8_t> bytes;
            uint8_t buffer[65536];
            size_t n;
            while ((n = std::fread(buffer, 1, sizeof buffer, f)) > 0)
            {
                bytes.insert(bytes.end(), buffer, buffer + n);
                if (bytes.size() > 64u * 1024u * 1024u) { std::fclose(f); error = "pack too large"; return false; }
            }
            std::fclose(f);
            return Parse(bytes.data(), bytes.size(), error);
        }

        // The patched shader for the engine's own bytecode, or null.
        const LightPackEntry* Find(uint8_t kind, const void* code, size_t size) const
        {
            if (!code || size < 8 || size > wxl::wmo::kMaxShaderBytes) return nullptr;
            const auto it = index_.find(Key(kind, wxl::wmo::HashBytecode(code, size)));
            if (it == index_.end()) return nullptr;
            const LightPackEntry& e = entries_[it->second];
            return e.originalSize == size ? &e : nullptr;
        }

        size_t Count() const { return entries_.size(); }
        size_t Count(uint8_t group) const
        {
            size_t n = 0;
            for (const auto& e : entries_) n += e.group == group;
            return n;
        }
        const std::vector<LightPackEntry>& Entries() const { return entries_; }

    private:
        static uint64_t Key(uint8_t kind, uint64_t hash) { return hash ^ (uint64_t(kind) << 63) ^ (uint64_t(kind) * 0x9E3779B97F4A7C15ull); }
        std::vector<LightPackEntry> entries_;
        std::unordered_map<uint64_t, size_t> index_;
    };
}
