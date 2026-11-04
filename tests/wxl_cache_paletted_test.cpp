#include "Engine/Source/ModernAssets/ModernBlp.h"
#include <cassert>
#include <cstring>

// The texture cache converts everything to palettized BLP2 (the CPU compositor reads nothing else).
// Synthetic sources: a DXT1 BLP2, an uncompressed BGRA BLP2 and a DDS, none of them paletted.
static void Put(std::vector<uint8_t>& v, size_t at, uint32_t x) { std::memcpy(v.data() + at, &x, 4); }
static uint32_t Get(const std::vector<uint8_t>& v, size_t at) {
    uint32_t r; std::memcpy(&r, v.data() + at, 4); return r;
}

static std::vector<uint8_t> MakeBlp(uint8_t encoding, uint8_t alphaDepth, uint8_t alphaType, uint32_t w, uint32_t h,
                                    const std::vector<uint8_t>& mip0) {
    std::vector<uint8_t> f(0x94 + 1024, 0);
    std::memcpy(f.data(), "BLP2", 4);
    Put(f, 4, 1);
    f[8] = encoding; f[9] = alphaDepth; f[10] = alphaType; f[11] = 1;
    Put(f, 12, w); Put(f, 16, h);
    Put(f, 0x14, uint32_t(f.size()));
    Put(f, 0x54, uint32_t(mip0.size()));
    f.insert(f.end(), mip0.begin(), mip0.end());
    return f;
}

static std::vector<uint8_t> MakeDds(const char* fourcc, uint32_t w, uint32_t h, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> f(128, 0);
    std::memcpy(f.data(), "DDS ", 4);
    Put(f, 4, 124); Put(f, 12, h); Put(f, 16, w); Put(f, 28, 1); Put(f, 76, 32);
    std::memcpy(f.data() + 84, fourcc, 4);
    f.insert(f.end(), payload.begin(), payload.end());
    return f;
}

static ModernBlpTransformResult Run(const char* name, const std::vector<uint8_t>& in, std::vector<uint8_t>& out,
                                    bool force) {
    return TransformModernBlp(name, in.data(), uint32_t(in.size()), out, force);
}

static void AssertPaletted(const std::vector<uint8_t>& out, uint32_t w, uint32_t h) {
    assert(out.size() > 0x94 + 1024);
    assert(std::memcmp(out.data(), "BLP2", 4) == 0 && Get(out, 4) == 1);
    assert(out[8] == 1);
    assert(Get(out, 12) == w && Get(out, 16) == h);
    assert(Get(out, 0x54) == w * h * 2); // index plane + 8-bit alpha plane
}

int main() {
    std::vector<uint8_t> out;
    const char* const path = "Item\\TextureComponents\\TorsoUpperTexture\\Plate_U.blp";

    // DXT1 8x8, one block per quadrant: becomes paletted with the same size.
    std::vector<uint8_t> dxt1Blocks(32);
    for (size_t i = 0; i < dxt1Blocks.size(); ++i) dxt1Blocks[i] = uint8_t(i * 29 + 5);
    auto dxt = MakeBlp(2, 0, 0, 8, 8, dxt1Blocks);
    assert(Run(path, dxt, out, true) == ModernBlpTransformResult::Transformed);
    AssertPaletted(out, 8, 8);

    // The cache converts whatever the name: only the cache's own callers need a paletted file.
    assert(Run("Textures\\Underwear\\x.blp", dxt, out, true) == ModernBlpTransformResult::Transformed);
    AssertPaletted(out, 8, 8);

    // Uncompressed BGRA 4x4 (4 bytes per pixel): opaque red/blue stripes survive as palette entries.
    std::vector<uint8_t> bgra(4 * 4 * 4);
    for (size_t i = 0; i < 16; ++i) {
        bool red = i % 2 == 0;
        bgra[i * 4 + 0] = red ? 0 : 255; bgra[i * 4 + 1] = 0; bgra[i * 4 + 2] = red ? 255 : 0; bgra[i * 4 + 3] = 255;
    }
    auto raw = MakeBlp(3, 8, 8, 4, 4, bgra);
    assert(Run(path, raw, out, true) == ModernBlpTransformResult::Transformed);
    AssertPaletted(out, 4, 4);
    {
        const uint8_t* pal = out.data() + 0x94;
        const uint8_t idxRed = out[Get(out, 0x14)];
        const uint8_t idxBlue = out[Get(out, 0x14) + 1];
        assert(idxRed != idxBlue);
        assert(pal[idxRed * 4 + 2] == 255 && pal[idxRed * 4 + 0] == 0);  // BGRA palette: red
        assert(pal[idxBlue * 4 + 0] == 255 && pal[idxBlue * 4 + 2] == 0); // blue
        assert(out[Get(out, 0x14) + 16] == 255);                           // alpha plane starts after 16 indices
    }

    // Raw BGR (3 bytes per pixel) is read as opaque.
    std::vector<uint8_t> bgr(4 * 4 * 3, 77);
    assert(Run(path, MakeBlp(3, 0, 0, 4, 4, bgr), out, true) == ModernBlpTransformResult::Transformed);
    AssertPaletted(out, 4, 4);
    assert(out[Get(out, 0x14) + 16] == 255);

    // A DDS served in place of a .blp goes straight to paletted.
    auto dds = MakeDds("DXT1", 8, 8, dxt1Blocks);
    assert(Run(path, dds, out, true) == ModernBlpTransformResult::Transformed);
    AssertPaletted(out, 8, 8);

    // Already paletted: left for the loader as is.
    auto pal = MakeBlp(1, 8, 8, 4, 4, std::vector<uint8_t>(32, 0));
    assert(Run(path, pal, out, true) == ModernBlpTransformResult::NotApplicable);

    // Damaged: refused, nothing half-written.
    auto cut = raw;
    cut.resize(0x94 + 1024 + 10);
    assert(Run(path, cut, out, true) == ModernBlpTransformResult::Rejected);
    assert(Run(path, MakeBlp(3, 8, 8, 4, 4, std::vector<uint8_t>(10)), out, true) == ModernBlpTransformResult::Rejected);

    // Without the flag nothing changes for non-component names (the GPU path keeps its own handling).
    assert(Run("Textures\\a.blp", dxt, out, false) == ModernBlpTransformResult::NotApplicable);
    return 0;
}
