#include "Engine/Source/ModernAssets/WxlWmo/WmoShaderPack.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace wxl::wmo;
static void Require(bool value, const char* label) { if (!value) { std::fprintf(stderr, "FAILED: %s\n", label); std::exit(1); } }
static std::vector<uint8_t> Original(uint8_t fill, size_t size) { return std::vector<uint8_t>(size, fill); }
static PackEntry Entry(uint8_t kind, uint8_t layers, uint8_t uv, uint8_t weights, const std::vector<uint8_t>& original, uint32_t marker) {
    PackEntry e; e.kind = kind; e.layers = layers; e.uvUsage = uv; e.weightUsage = weights;
    e.originalHash = HashBytecode(original.data(), original.size()); e.originalSize = uint32_t(original.size());
    e.bytecode = {0xFFFE0300u, marker}; e.tintRegister = layers == 4 ? 12 : -1;
    for (uint8_t i = 0; i < 8; ++i) e.samplerSlots[i] = i;
    return e;
}
int main() {
    const auto vsA = Original(1, 64), vsB = Original(2, 64), ps = Original(3, 96), ps2 = Original(4, 96);
    std::vector<PackEntry> entries = {
        Entry(0, 0, 1, 2, vsA, 10), Entry(0, 0, 4, 5, vsB, 11),
        Entry(1, 2, 0, 0, ps, 20), Entry(1, 4, 1, 2, ps, 21), Entry(1, 4, 4, 5, ps, 22), Entry(1, 4, 1, 2, ps2, 23)};
    const auto bytes = ShaderPack::Serialize(entries);
    ShaderPack pack; std::string error;
    Require(pack.Parse(bytes.data(), bytes.size(), error), "valid pack parses");
    Require(pack.Count() == entries.size(), "entry count");
    const PackEntry* a = pack.FindVertex(vsA.data(), vsA.size());
    const PackEntry* b = pack.FindVertex(vsB.data(), vsB.size());
    Require(a && b && a->bytecode[1] == 10 && b->bytecode[1] == 11, "vertex lookup by original bytecode");
    Require(!pack.FindVertex(vsA.data(), vsA.size() - 4), "size participates in the key");
    Require(!pack.FindVertex(ps.data(), ps.size()), "pixel shader is not a vertex entry");
    Require(pack.FindPixel(ps.data(), ps.size(), 2, *a)->bytecode[1] == 20, "two layers ignore the vertex mapping");
    Require(pack.FindPixel(ps.data(), ps.size(), 2, *b)->bytecode[1] == 20, "two layers shared across mappings");
    Require(pack.FindPixel(ps.data(), ps.size(), 4, *a)->bytecode[1] == 21, "four layers follow mapping 1/2");
    Require(pack.FindPixel(ps.data(), ps.size(), 4, *b)->bytecode[1] == 22, "four layers follow mapping 4/5");
    Require(pack.FindPixel(ps2.data(), ps2.size(), 4, *a)->bytecode[1] == 23, "other pixel shader");
    Require(!pack.FindPixel(ps2.data(), ps2.size(), 4, *b), "missing mapping variant falls back");
    Require(!pack.FindPixel(ps2.data(), ps2.size(), 2, *a), "missing layer variant falls back");
    Require(!pack.FindPixel(ps.data(), ps.size(), 3, *a), "unsupported layer count falls back");

    // Hostile or damaged files are rejected before anything is published.
    for (size_t cut : {size_t(0), size_t(3), size_t(11), size_t(30), bytes.size() - 1}) {
        ShaderPack damaged; std::string why;
        Require(!damaged.Parse(bytes.data(), cut, why) && damaged.Count() == 0, "truncated pack rejected");
    }
    auto corrupt = bytes; corrupt[0] ^= 0xFF;
    { ShaderPack p; Require(!p.Parse(corrupt.data(), corrupt.size(), error), "bad magic rejected"); }
    auto trailing = bytes; trailing.push_back(0);
    { ShaderPack p; Require(!p.Parse(trailing.data(), trailing.size(), error), "trailing bytes rejected"); }
    auto huge = bytes; const uint32_t many = 0x7FFFFFFF; std::memcpy(huge.data() + 8, &many, 4);
    { ShaderPack p; Require(!p.Parse(huge.data(), huge.size(), error), "implausible count rejected"); }
    { auto dup = entries; dup.push_back(dup[0]); const auto d = ShaderPack::Serialize(dup);
      ShaderPack p; Require(!p.Parse(d.data(), d.size(), error), "duplicate key rejected"); }
    { auto bad = entries; bad[2].layers = 3; const auto d = ShaderPack::Serialize(bad);
      ShaderPack p; Require(!p.Parse(d.data(), d.size(), error), "invalid pixel layer count rejected"); }
    { auto bad = entries; bad[0].samplerSlots[3] = 16; const auto d = ShaderPack::Serialize(bad);
      ShaderPack p; Require(!p.Parse(d.data(), d.size(), error), "sampler slot out of range rejected"); }
    { auto bad = entries; bad[0].bytecode = {1}; const auto d = ShaderPack::Serialize(bad);
      ShaderPack p; Require(!p.Parse(d.data(), d.size(), error), "tiny bytecode rejected"); }
    { ShaderPack p; Require(!p.Load("/nonexistent/wmo_shaders.pack", error) && error == "missing", "missing file reported"); }

    ShaderPack failed;   // a failed parse after a good one keeps the good one
    Require(failed.Parse(bytes.data(), bytes.size(), error), "parse");
    Require(!failed.Parse(corrupt.data(), corrupt.size(), error) && failed.Count() == entries.size(), "failed reparse keeps previous pack");
    std::puts("PASS: WMO shader pack format, lookup keys, fallbacks and rejection");
}
