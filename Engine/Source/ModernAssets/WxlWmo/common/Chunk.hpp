// Generic IFF chunk layer: [magic u32 BE][size u32 LE][payload]. No format knowledge; one mechanism
// for walking, finding and emitting chunks, shared by every chunk-based container format (WMO
// today; ADT-style formats fit the same shape).
// Copyright (C) 2026 WarcraftXL
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <cstring>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace wxl::modern::assets::common::iff
{
    // Chunk magic packed big-endian to match the stored bytes.
    constexpr uint32_t FourCC(char a, char b, char c, char d)
    {
        return (static_cast<uint32_t>(static_cast<uint8_t>(a)) << 24) |
               (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 16) |
               (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 8) |
                static_cast<uint32_t>(static_cast<uint8_t>(d));
    }

    // Chunk fields are not guaranteed to be naturally aligned. memcpy keeps these helpers valid on every
    // compiler/architecture while preserving the little-endian values used by the Windows client.
    inline uint16_t Rd16(const uint8_t* p)
    {
        uint16_t v;
        std::memcpy(&v, p, sizeof(v));
        return v;
    }

    inline uint32_t Rd32(const uint8_t* p)
    {
        uint32_t v;
        std::memcpy(&v, p, sizeof(v));
        return v;
    }

    inline float Rdf(const uint8_t* p)
    {
        float v;
        std::memcpy(&v, p, sizeof(v));
        return v;
    }

    inline void Wr16(uint8_t* p, int16_t v)  { std::memcpy(p, &v, sizeof(v)); }
    inline void Wr32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, sizeof(v)); }

    // One located chunk. pos/data point PAST the 8-byte header; the full chunk is [data-8, data+size).
    struct Chunk
    {
        uint32_t magic = 0;
        uint32_t pos   = 0;        // payload offset from the span start
        uint32_t size  = 0;        // payload size
        const uint8_t* data = nullptr; // payload pointer, null when absent
    };

    // Forward chunk walker. Stops at the first malformed/truncated header.
    class Reader
    {
    public:
        explicit Reader(std::span<const uint8_t> b)
            : buf_(b.data()), size_(b.size()),
              valid_(b.size() <= std::numeric_limits<uint32_t>::max()) {}

        // Visit each chunk in order; f returns false to stop. Returns false when the callback stops the walk
        // or when any header/payload is truncated. A trailing partial header is malformed, not padding.
        template <class F>
        bool ForEach(F&& f) const
        {
            if (!valid_)
                return false;

            size_t p = 0;
            while (p < size_)
            {
                if (size_ - p < 8)
                    return false;

                const uint32_t payloadSize = Rd32(buf_ + p + 4);
                if (static_cast<size_t>(payloadSize) > size_ - p - 8)
                    return false;

                Chunk c{
                    Rd32(buf_ + p),
                    static_cast<uint32_t>(p + 8),
                    payloadSize,
                    buf_ + p + 8
                };
                if (!f(c))
                    return false;
                p += 8 + static_cast<size_t>(payloadSize);
            }
            return true;
        }

        // First chunk with this magic.
        bool Find(uint32_t magic, Chunk& out) const
        {
            bool found = false;
            const bool valid = ForEach([&](const Chunk& c) {
                if (!found && c.magic == magic)
                {
                    out = c;
                    found = true;
                }
                return true;
            });
            return valid && found;
        }

    private:
        const uint8_t* buf_;
        size_t size_;
        bool valid_;
    };

    // Append a chunk (8-byte header + payload) to out.
    inline void Emit(std::vector<uint8_t>& out, uint32_t magic, const uint8_t* payload, uint32_t len)
    {
        if (len && !payload)
            return;

        uint8_t hdr[8];
        Wr32(hdr + 0, magic);
        Wr32(hdr + 4, len);
        out.insert(out.end(), hdr, hdr + 8);
        if (len)
            out.insert(out.end(), payload, payload + len);
    }

    // Append a chunk verbatim from a located source chunk (header + payload).
    inline void EmitRaw(std::vector<uint8_t>& out, const Chunk& c)
    {
        if (!c.data)
            return;
        out.insert(out.end(), c.data - 8, c.data + c.size);
    }
}
