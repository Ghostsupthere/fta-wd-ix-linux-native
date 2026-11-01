#include "Engine/Source/ModernAssets/ModernBlp.h"
#include "WxlBlp/textures/dds/Dds.hpp"
#include <cassert>
#include <cstring>

// Synthetic DDS files: header (128 bytes) followed by block-compressed mip levels.
static std::vector<uint8_t> MakeDds(const char* fourcc, uint32_t width, uint32_t height, uint32_t mips,
                                    std::vector<uint8_t> payload) {
    std::vector<uint8_t> file(128, 0);
    auto put = [&](size_t at, uint32_t v) { std::memcpy(file.data() + at, &v, 4); };
    std::memcpy(file.data(), "DDS ", 4);
    put(4, 124); put(12, height); put(16, width); put(28, mips); put(76, 32);
    std::memcpy(file.data() + 84, fourcc, 4);
    file.insert(file.end(), payload.begin(), payload.end());
    return file;
}

static uint32_t Get(const std::vector<uint8_t>& v, size_t at) {
    uint32_t r; std::memcpy(&r, v.data() + at, 4); return r;
}

static std::vector<uint8_t> Pattern(size_t n, uint8_t seed) {
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = uint8_t(seed + i * 7);
    return v;
}

static ModernBlpTransformResult Run(const char* name, const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
    return TransformModernBlp(name, in.data(), uint32_t(in.size()), out);
}

int main() {
    std::vector<uint8_t> out;
    constexpr size_t kHeader = 4 + 4 + 4 + 8 + 64 + 64 + 1024;

    // DXT1 8x8 with three levels: 4 blocks, 1 block, 1 block (8 bytes each). Payload is copied verbatim.
    {
        std::vector<uint8_t> payload = Pattern(32 + 8 + 8, 3);
        auto dds = MakeDds("DXT1", 8, 8, 3, payload);
        assert(Run("Item\\Weapon\\sword.blp", dds, out) == ModernBlpTransformResult::Transformed);
        assert(std::memcmp(out.data(), "BLP2", 4) == 0 && Get(out, 4) == 1);
        assert(out[8] == 2 && out[9] == 0 && out[10] == 0 && out[11] == 1);
        assert(Get(out, 12) == 8 && Get(out, 16) == 8);
        assert(Get(out, 20) == kHeader && Get(out, 24) == kHeader + 32 && Get(out, 28) == kHeader + 40);
        assert(Get(out, 84) == 32 && Get(out, 88) == 8 && Get(out, 92) == 8 && Get(out, 96) == 0);
        assert(out.size() == kHeader + payload.size());
        assert(std::memcmp(out.data() + kHeader, payload.data(), payload.size()) == 0);
    }

    // DXT3 and DXT5 set the alpha depth and type the client expects.
    {
        auto dxt3 = MakeDds("DXT3", 4, 4, 1, Pattern(16, 1));
        assert(Run("a.blp", dxt3, out) == ModernBlpTransformResult::Transformed && out[9] == 8 && out[10] == 1);
        auto dxt5 = MakeDds("DXT5", 4, 4, 1, Pattern(16, 1));
        assert(Run("a.BLP", dxt5, out) == ModernBlpTransformResult::Transformed && out[9] == 8 && out[10] == 7);
    }

    // A mip count of zero means one level; a short file keeps the levels it fully has.
    {
        auto dds = MakeDds("DXT5", 8, 8, 0, Pattern(64, 5));
        assert(Run("a.blp", dds, out) == ModernBlpTransformResult::Transformed && Get(out, 84) == 64 && Get(out, 88) == 0);
        auto partial = MakeDds("DXT5", 8, 8, 4, Pattern(64 + 16 + 5, 5));
        assert(Run("a.blp", partial, out) == ModernBlpTransformResult::Transformed);
        assert(Get(out, 84) == 64 && Get(out, 88) == 16 && Get(out, 92) == 0 && Get(out, 28) == 0);
    }

    // Rejections: not a whole first level, unsupported format, oversize, truncated or damaged header.
    {
        assert(Run("a.blp", MakeDds("DXT5", 8, 8, 1, Pattern(63, 1)), out) == ModernBlpTransformResult::Rejected);
        assert(Run("a.blp", MakeDds("ATI2", 4, 4, 1, Pattern(16, 1)), out) == ModernBlpTransformResult::Rejected);
        assert(Run("a.blp", MakeDds("DXT1", 4096, 4096, 1, Pattern(8, 1)), out) == ModernBlpTransformResult::Rejected);
        assert(Run("a.blp", MakeDds("DXT1", 0, 4, 1, Pattern(8, 1)), out) == ModernBlpTransformResult::Rejected);
        assert(Run("a.blp", MakeDds("DXT1", 16384, 16384, 0xFFFFFFFFu, Pattern(8, 1)), out) == ModernBlpTransformResult::Rejected);
        auto cut = MakeDds("DXT1", 4, 4, 1, Pattern(8, 1));
        cut.resize(100);
        assert(Run("a.blp", cut, out) == ModernBlpTransformResult::Rejected);
        auto badSize = MakeDds("DXT1", 4, 4, 1, Pattern(8, 1));
        badSize[4] = 100;
        assert(Run("a.blp", badSize, out) == ModernBlpTransformResult::Rejected);
        std::vector<uint8_t> tiny{'D', 'D', 'S', ' '};
        assert(Run("a.blp", tiny, out) == ModernBlpTransformResult::Rejected && out.empty());
    }

    // Only .blp names are touched; a DDS under another name is left alone.
    assert(Run("a.dds", MakeDds("DXT1", 4, 4, 1, Pattern(8, 1)), out) == ModernBlpTransformResult::NotApplicable);

    // Luminance alpha: a DXT1 black block becomes DXT5 with zero alpha; sizes double per block.
    {
        std::vector<uint8_t> black(8, 0);
        auto dds = MakeDds("DXT1", 4, 4, 1, black);
        std::vector<uint8_t> blp;
        assert(wxl::modern::assets::textures::dds::DdsToBlp(dds.data(), dds.size(), blp, true));
        assert(blp[9] == 8 && blp[10] == 7 && Get(blp, 84) == 16);
        const uint8_t* block = blp.data() + kHeader;
        assert(block[0] == 255 && block[1] == 0);
        // Alpha index 1 (value 0) for every texel: bits 001 repeated.
        uint64_t idx = 0;
        for (int i = 0; i < 6; ++i) idx |= uint64_t(block[2 + i]) << (8 * i);
        for (int i = 0; i < 16; ++i) assert(((idx >> (3 * i)) & 7) == 1);
        assert(blp.size() == kHeader + 16);
    }
    return 0;
}
