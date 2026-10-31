#pragma once
// Offline-generated, locally built pack of patched WMO shaders (see
// tools/build-wmo-shader-pack.py). The pack is derived from the client's own
// shader bytecode, so it is generated per install and is not committed. Entries
// are keyed by a hash of the ORIGINAL bytecode; the runtime never assembles text.
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace wxl::wmo {

constexpr uint32_t kShaderPackMagic = 0x534F4D57; // 'WMOS'
constexpr uint32_t kShaderPackVersion = 1;
constexpr size_t kMaxShaderBytes = 0x20000;

inline uint64_t HashBytecode(const void* data, size_t size) {
    uint64_t hash = 1469598103934665603ull;
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) { hash ^= bytes[i]; hash *= 1099511628211ull; }
    return hash ^ (uint64_t(size) << 40);
}

struct PackEntry {
    uint8_t kind = 0;      // 0 vertex, 1 pixel
    uint8_t layers = 0;    // pixel only: 2 or 4
    uint8_t uvUsage = 0;   // vertex: chosen output usage for UV C/D; pixel: the vertex mapping it pairs with
    uint8_t weightUsage = 0;
    int32_t tintRegister = -1;
    std::array<uint8_t, 8> samplerSlots{};
    uint64_t originalHash = 0;
    uint32_t originalSize = 0;
    std::vector<uint32_t> bytecode;
};

struct PackKey {
    uint8_t kind; uint8_t layers; uint8_t uvUsage; uint8_t weightUsage; uint64_t hash;
    bool operator==(const PackKey& o) const {
        return kind == o.kind && layers == o.layers && uvUsage == o.uvUsage && weightUsage == o.weightUsage && hash == o.hash;
    }
};
struct PackKeyHash {
    size_t operator()(const PackKey& k) const {
        return size_t(k.hash ^ (uint64_t(k.kind) << 56) ^ (uint64_t(k.layers) << 48) ^
                      (uint64_t(k.uvUsage) << 40) ^ (uint64_t(k.weightUsage) << 32));
    }
};

class ShaderPack {
public:
    // Parses and validates the whole file before publishing anything.
    bool Parse(const uint8_t* data, size_t size, std::string& error);
    bool Load(const std::string& path, std::string& error);
    const PackEntry* FindVertex(const void* bytecode, size_t size) const;
    const PackEntry* FindPixel(const void* bytecode, size_t size, uint32_t layers, const PackEntry& vertex) const;
    size_t Count() const { return m_entries.size(); }
    const std::vector<PackEntry>& Entries() const { return m_entries; }

    static std::vector<uint8_t> Serialize(const std::vector<PackEntry>& entries);
private:
    std::vector<PackEntry> m_entries;
    std::unordered_map<PackKey, size_t, PackKeyHash> m_index;
};

} // namespace wxl::wmo
