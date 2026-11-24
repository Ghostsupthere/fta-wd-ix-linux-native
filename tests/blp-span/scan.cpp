// Reads records ([u32 name length][name][u32 data length][data]) from stdin and runs the production BLP header check
// on each one. Prints a summary and the files that are truncated or rejected.
#include "Engine/Source/BLPFile/SourceBounds.h"
#include <cstdio>
#include <string>
#include <vector>

static bool Read(void* out, size_t n) { return std::fread(out, 1, n, stdin) == n; }

int main() {
    size_t total = 0, accepted = 0, truncatedCount = 0, rejected = 0, outside = 0, empty = 0, notBlp2 = 0, other = 0;
    std::vector<std::string> truncatedNames, rejectedNames;
    for (;;) {
        uint32_t nameLen;
        if (!Read(&nameLen, 4)) break;
        std::string name(nameLen, '\0');
        uint32_t dataLen;
        if (!Read(name.data(), nameLen) || !Read(&dataLen, 4)) return 2;
        std::vector<char> data(dataLen);
        if (dataLen && !Read(data.data(), dataLen)) return 2;
        ++total;
        BLPHeader header{};
        bool truncated = false;
        if (!BlpSourceBounds::ReadHeader(data.data(), data.size(), header, &truncated)) {
            ++rejected;
            if (data.size() < BlpSourceBounds::FixedHeaderBytes) ++empty;          // missing, deleted or empty entry
            else if (*reinterpret_cast<const uint32_t*>(data.data()) != BLP_MAGIC_BLP2) ++notBlp2; // old BLP1 or not a BLP
            else {
                ++other;
                if (rejectedNames.size() < 40) rejectedNames.push_back(name);
            }
            continue;
        }
        ++accepted;
        if (truncated) {
            ++truncatedCount;
            if (truncatedNames.size() < 15) truncatedNames.push_back(name);
        }
        // Every slot that remains must lie inside the buffer.
        const bool palette = header.m_colorEncoding == COLOR_PAL || header.m_colorEncoding == COLOR_JPEG;
        const size_t headerBytes = palette ? sizeof(header) : BlpSourceBounds::FixedHeaderBytes;
        for (uint32_t i = 0; i < BLP_MIP_SLOTS; ++i) {
            const size_t bytes = header.m_mipSizes[i] ? BlpSourceBounds::ExpectedLevelBytes(header, i) : 0, offset = header.m_mipOffsets[i];
            if (bytes && (offset < headerBytes || offset > data.size() || bytes > data.size() - offset)) ++outside;
        }
    }
    std::printf("files %zu, accepted %zu (of which truncated %zu), rejected %zu (empty or short %zu, not BLP2 %zu, other %zu), slots outside buffer after check %zu\n",
                total, accepted, truncatedCount, rejected, empty, notBlp2, other, outside);
    for (auto& n : truncatedNames) std::printf("  truncated: %s\n", n.c_str());
    for (auto& n : rejectedNames) std::printf("  rejected: %s\n", n.c_str());
    return outside ? 1 : 0;
}
