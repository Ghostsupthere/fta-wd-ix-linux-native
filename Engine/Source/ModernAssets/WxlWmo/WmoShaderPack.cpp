#include "WmoShaderPack.h"
#include <cstdio>
#include <cstring>

namespace wxl::wmo {
namespace {
template<class T> bool Take(const uint8_t*& p, const uint8_t* end, T& out) {
    if (size_t(end - p) < sizeof(T)) return false;
    std::memcpy(&out, p, sizeof(T)); p += sizeof(T); return true;
}
template<class T> void Put(std::vector<uint8_t>& out, T value) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(T));
}
}

std::vector<uint8_t> ShaderPack::Serialize(const std::vector<PackEntry>& entries) {
    std::vector<uint8_t> out;
    Put(out, kShaderPackMagic); Put(out, kShaderPackVersion); Put(out, uint32_t(entries.size()));
    for (const auto& e : entries) {
        Put(out, e.kind); Put(out, e.layers); Put(out, e.uvUsage); Put(out, e.weightUsage);
        Put(out, e.tintRegister);
        out.insert(out.end(), e.samplerSlots.begin(), e.samplerSlots.end());
        Put(out, e.originalHash); Put(out, e.originalSize);
        Put(out, uint32_t(e.bytecode.size() * 4));
        const auto* bytes = reinterpret_cast<const uint8_t*>(e.bytecode.data());
        out.insert(out.end(), bytes, bytes + e.bytecode.size() * 4);
    }
    return out;
}

bool ShaderPack::Parse(const uint8_t* data, size_t size, std::string& error) {
    const uint8_t* p = data; const uint8_t* end = data + size;
    uint32_t magic = 0, version = 0, count = 0;
    if (!Take(p, end, magic) || !Take(p, end, version) || !Take(p, end, count) ||
        magic != kShaderPackMagic || version != kShaderPackVersion) { error = "not a WMO shader pack"; return false; }
    if (count > 65536) { error = "implausible entry count"; return false; }
    std::vector<PackEntry> entries; entries.reserve(count);
    std::unordered_map<PackKey, size_t, PackKeyHash> index;
    for (uint32_t i = 0; i < count; ++i) {
        PackEntry e; uint32_t bytes = 0;
        if (!Take(p, end, e.kind) || !Take(p, end, e.layers) || !Take(p, end, e.uvUsage) || !Take(p, end, e.weightUsage) ||
            !Take(p, end, e.tintRegister) || size_t(end - p) < 8) { error = "truncated entry"; return false; }
        std::memcpy(e.samplerSlots.data(), p, 8); p += 8;
        if (!Take(p, end, e.originalHash) || !Take(p, end, e.originalSize) || !Take(p, end, bytes)) { error = "truncated entry"; return false; }
        if (e.kind > 1 || (e.kind == 1 && e.layers != 2 && e.layers != 4) || (e.kind == 0 && e.layers != 0) ||
            bytes < 8 || bytes % 4 || bytes > kMaxShaderBytes || size_t(end - p) < bytes) { error = "invalid entry"; return false; }
        for (uint8_t slot : e.samplerSlots) if (slot > 15) { error = "invalid sampler slot"; return false; }
        e.bytecode.resize(bytes / 4); std::memcpy(e.bytecode.data(), p, bytes); p += bytes;
        const PackKey key{e.kind, e.layers, e.kind == 1 ? e.uvUsage : uint8_t(0), e.kind == 1 ? e.weightUsage : uint8_t(0), e.originalHash};
        if (!index.emplace(key, entries.size()).second) { error = "duplicate entry"; return false; }
        entries.push_back(std::move(e));
    }
    if (p != end) { error = "trailing bytes"; return false; }
    m_entries = std::move(entries); m_index = std::move(index); return true;
}

bool ShaderPack::Load(const std::string& path, std::string& error) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) { error = "missing"; return false; }
    std::vector<uint8_t> bytes; uint8_t buffer[65536]; size_t n;
    while ((n = std::fread(buffer, 1, sizeof buffer, file)) > 0) {
        bytes.insert(bytes.end(), buffer, buffer + n);
        if (bytes.size() > (64u << 20)) { std::fclose(file); error = "too large"; return false; }
    }
    std::fclose(file);
    return Parse(bytes.data(), bytes.size(), error);
}

const PackEntry* ShaderPack::FindVertex(const void* bytecode, size_t size) const {
    const auto it = m_index.find({0, 0, 0, 0, HashBytecode(bytecode, size)});
    if (it == m_index.end()) return nullptr;
    const PackEntry& e = m_entries[it->second];
    return e.originalSize == size ? &e : nullptr;
}

const PackEntry* ShaderPack::FindPixel(const void* bytecode, size_t size, uint32_t layers, const PackEntry& vertex) const {
    const uint8_t uv = layers == 4 ? vertex.uvUsage : 0, weights = layers == 4 ? vertex.weightUsage : 0;
    const auto it = m_index.find({1, uint8_t(layers), uv, weights, HashBytecode(bytecode, size)});
    if (it == m_index.end()) return nullptr;
    const PackEntry& e = m_entries[it->second];
    return e.originalSize == size ? &e : nullptr;
}

} // namespace wxl::wmo
