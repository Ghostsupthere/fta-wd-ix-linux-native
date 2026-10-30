// Scoped WMO visual side data. Copyright (C) 2026 WarcraftXL. GPLv3.
#include "WmoVisualData.hpp"
#include "WmoChunks.hpp"
#include "../../common/Chunk.hpp"
#include <cmath>
#include <cstring>
#include <limits>

namespace wxl::modern::assets::wmo::visual
{
    namespace iff = wxl::modern::assets::common::iff;
    namespace
    {
        constexpr uint32_t kMagic = 0x31565758; // XWV1, private transport footer, not a WMO chunk
        constexpr size_t kFooter = 16;
        constexpr uint32_t kMaxVertices = 65536, kMaxMaterials = 4096, kMaxPath = 1024;
        void Put(std::vector<uint8_t>& out, uint32_t value)
        {
            const size_t at = out.size();
            out.resize(at + 4);
            iff::Wr32(out.data() + at, value);
        }
        struct Cursor
        {
            std::span<const uint8_t> data;
            bool U32(uint32_t& value)
            {
                if (data.size() < 4) return false;
                value = iff::Rd32(data.data()); data = data.subspan(4); return true;
            }
            bool Path(std::string& value)
            {
                uint32_t size;
                if (!U32(size) || size > kMaxPath || size > data.size()) return false;
                if (size && std::memchr(data.data(), 0, size)) return false;
                value.assign(reinterpret_cast<const char*>(data.data()), size);
                data = data.subspan(size); return true;
            }
        };
        bool Envelope(std::span<const uint8_t> bytes, uint32_t kind, Cursor& cursor, uint32_t& body)
        {
            if (bytes.size() < kFooter + 20 || bytes.size() > UINT32_MAX) return false;
            const auto* p = bytes.data() + bytes.size() - kFooter;
            if (iff::Rd32(p) != kMagic || iff::Rd32(p + 4) != kind) return false;
            body = iff::Rd32(p + 8);
            const uint32_t payload = iff::Rd32(p + 12);
            if (body < 20 || body > bytes.size() - kFooter || payload != bytes.size() - kFooter - body)
                return false;
            if (iff::Rd32(bytes.data()) != kMVER || iff::Rd32(bytes.data() + 4) != 4 ||
                iff::Rd32(bytes.data() + 12) != (kind == 1 ? kMOHD : kMOGP)) return false;
            cursor.data = bytes.subspan(body, payload);
            return true;
        }
        bool Finish(std::vector<uint8_t>& body, const std::vector<uint8_t>& payload, uint32_t kind)
        {
            if (body.size() > UINT32_MAX - kFooter || payload.size() > UINT32_MAX - kFooter - body.size())
                return false;
            const auto size = static_cast<uint32_t>(body.size());
            body.insert(body.end(), payload.begin(), payload.end());
            Put(body, kMagic); Put(body, kind); Put(body, size); Put(body, static_cast<uint32_t>(payload.size()));
            return true;
        }
        uint32_t Layers(uint32_t shader)
        {
            switch (shader)
            {
                // Shader 7 already has a useful native environment fallback in this fork. Keep it:
                // replacing that family with the upstream diffuse composite would lose reflection.
                case 6: case 9: case 13: case 15: case 18: case 19: case 20: return 2;
                case 23: return 4;
                default: return 0;
            }
        }
    }
    bool AppendRoot(std::span<const uint8_t> source, const ResolveCtx& resolver, std::vector<uint8_t>& body)
    {
        iff::Chunk mats{}, names{};
        const iff::Reader reader(source);
        reader.Find(kMOTX, names);
        if (!reader.Find(kMOMT, mats) || mats.size % 64 || mats.size / 64 > kMaxMaterials) return false;
        Root root;
        constexpr uint32_t offsets[8] = { 0x0C, 0x18, 0x24, 0x28, 0x30, 0x34, 0x38, 0x3C };
        for (uint32_t i = 0; i < mats.size / 64; ++i)
        {
            const auto* rec = mats.data + i * 64;
            Material mat;
            mat.index = i; mat.layers = Layers(iff::Rd32(rec + 4));
            if (!mat.layers) continue;
            mat.flags = iff::Rd32(rec); mat.tint = iff::Rd32(rec + 0x1C);
            bool complete = true;
            const uint32_t count = mat.layers == 4 ? 8 : 2;
            for (uint32_t t = 0; t < count; ++t)
            {
                const uint32_t ref = iff::Rd32(rec + offsets[t]);
                auto& path = mat.textures[t];
                if (names.data && ref < names.size && (ref == 0 || names.data[ref - 1] == 0))
                {
                    const auto* end = static_cast<const uint8_t*>(std::memchr(names.data + ref, 0, names.size - ref));
                    if (end) path.assign(reinterpret_cast<const char*>(names.data + ref), end - names.data - ref);
                }
                else if (ref && resolver.resolve) resolver.resolve(resolver.user, ref, path);
                if (path.empty() || path.size() > kMaxPath || path.find('\0') != std::string::npos)
                    complete = false;
            }
            if (complete) root.materials.push_back(std::move(mat));
        }
        std::vector<uint8_t> payload;
        Put(payload, static_cast<uint32_t>(root.materials.size()));
        for (const auto& mat : root.materials)
        {
            Put(payload, mat.index); Put(payload, mat.layers); Put(payload, mat.flags); Put(payload, mat.tint);
            for (const auto& path : mat.textures)
            {
                Put(payload, static_cast<uint32_t>(path.size()));
                payload.insert(payload.end(), path.begin(), path.end());
            }
        }
        return Finish(body, payload, 1);
    }
    bool AppendGroup(std::span<const uint8_t> source, std::vector<uint8_t>& body)
    {
        if (source.size() < 88 || iff::Rd32(source.data()) != kMVER || iff::Rd32(source.data() + 4) != 4 ||
            iff::Rd32(source.data() + 12) != kMOGP || iff::Rd32(source.data() + 16) != source.size() - 20)
            return false;
        iff::Chunk pos{}, uv[4]{}, weights{};
        uint32_t uvCount = 0;
        if (!iff::Reader(source.subspan(88)).ForEach([&](const iff::Chunk& c)
        {
            if (c.magic == kMOVT) pos = c;
            if (c.magic == kMOTV && uvCount < 4) uv[uvCount++] = c;
            if (c.magic == 0x4D4F4332) weights = c; // MOC2 (WMO FourCC byte order)
            return true;
        })) return false;
        const uint32_t count = pos.size / 12;
        if (pos.size % 12 || count > kMaxVertices) return false;
        uint32_t validUv = 0;
        while (validUv < uvCount && uv[validUv].size == count * 8) ++validUv;
        const bool hasWeights = weights.data && weights.size == count * 4;
        std::vector<Vertex> vertices(validUv >= 2 ? count : 0);
        for (uint32_t v = 0; v < vertices.size(); ++v)
        {
            for (uint32_t s = 0; s < 4; ++s)
            {
                std::memcpy(vertices[v].uv[s], uv[s < validUv ? s : 0].data + v * 8, 8);
                if (!std::isfinite(vertices[v].uv[s][0]) || !std::isfinite(vertices[v].uv[s][1])) return false;
            }
            if (hasWeights) vertices[v].weights = iff::Rd32(weights.data + v * 4);
        }
        std::vector<uint8_t> payload;
        Put(payload, validUv); Put(payload, hasWeights ? 1 : 0); Put(payload, static_cast<uint32_t>(vertices.size()));
        const auto* bytes = reinterpret_cast<const uint8_t*>(vertices.data());
        if (!vertices.empty()) payload.insert(payload.end(), bytes, bytes + vertices.size() * sizeof(Vertex));
        return Finish(body, payload, 2);
    }
    bool ReadRoot(std::span<const uint8_t> served, Root& out, uint32_t& bodySize)
    {
        Cursor c; uint32_t body, count;
        if (!Envelope(served, 1, c, body) || !c.U32(count) || count > kMaxMaterials) return false;
        Root result;
        uint32_t previous = 0;
        for (uint32_t i = 0; i < count; ++i)
        {
            Material m;
            if (!c.U32(m.index) || !c.U32(m.layers) || !c.U32(m.flags) || !c.U32(m.tint) ||
                m.index >= kMaxMaterials || (i && m.index <= previous) || (m.layers != 2 && m.layers != 4)) return false;
            previous = m.index;
            for (auto& p : m.textures) if (!c.Path(p)) return false;
            for (uint32_t t = 0; t < (m.layers == 4 ? 8u : 2u); ++t) if (m.textures[t].empty()) return false;
            result.materials.push_back(std::move(m));
        }
        if (!c.data.empty()) return false;
        bodySize = body; out = std::move(result); return true;
    }
    bool ReadGroup(std::span<const uint8_t> served, Group& out, uint32_t& bodySize)
    {
        Cursor c; uint32_t body, count, weight;
        Group result;
        if (!Envelope(served, 2, c, body) || !c.U32(result.uvSets) || result.uvSets > 4 ||
            !c.U32(weight) || weight > 1 || !c.U32(count) || count > kMaxVertices ||
            c.data.size() != count * sizeof(Vertex) || (count && !result.uvSets)) return false;
        result.hasWeights = weight != 0;
        result.vertices.resize(count);
        if (count) std::memcpy(result.vertices.data(), c.data.data(), c.data.size());
        for (const auto& v : result.vertices)
            for (const auto& uv : v.uv) if (!std::isfinite(uv[0]) || !std::isfinite(uv[1])) return false;
        bodySize = body; out = std::move(result); return true;
    }
}
