#include "Engine/Source/ModernAssets/ModernM3.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// A minimal MD34: one bone, three vertices, one triangle, one region, one batch, one material with a
// diffuse image. Layout offsets follow Md34.cpp.
struct Md34Builder {
    std::vector<uint8_t> bytes = std::vector<uint8_t>(32, 0);
    struct Entry { char tag[4]; uint32_t offset, n, version; };
    std::vector<Entry> entries;

    uint32_t Block(const char* tag, uint32_t version, const std::vector<uint8_t>& data, uint32_t n) {
        while (bytes.size() % 16) bytes.push_back(0);
        Entry e{};
        std::memcpy(e.tag, tag, 4);
        e.offset = static_cast<uint32_t>(bytes.size());
        e.n = n;
        e.version = version;
        bytes.insert(bytes.end(), data.begin(), data.end());
        entries.push_back(e);
        return static_cast<uint32_t>(entries.size() - 1);
    }
    uint32_t Text(const std::string& text) {
        std::vector<uint8_t> data(text.begin(), text.end());
        data.push_back(0);
        return Block("RAHC", 0, data, static_cast<uint32_t>(data.size()));
    }
};

static void Put32(std::vector<uint8_t>& v, size_t at, uint32_t x) { std::memcpy(v.data() + at, &x, 4); }
static void PutF(std::vector<uint8_t>& v, size_t at, float x) { std::memcpy(v.data() + at, &x, 4); }
static void PutRef(std::vector<uint8_t>& v, size_t at, uint32_t n, uint32_t index) {
    Put32(v, at, n); Put32(v, at + 4, index); Put32(v, at + 8, 0);
}

static std::vector<uint8_t> MakeM3() {
    Md34Builder b;
    const uint32_t boneName = b.Text("Root");
    std::vector<uint8_t> bone(160, 0);
    PutRef(bone, 0x04, 5, boneName);
    bone[0x14] = 0xFF; bone[0x15] = 0xFF;                      // parent -1
    PutF(bone, 0x44, 0); PutF(bone, 0x48, 0); PutF(bone, 0x4C, 0); PutF(bone, 0x50, 1);
    PutF(bone, 0x70, 1); PutF(bone, 0x74, 1); PutF(bone, 0x78, 1);
    const uint32_t bones = b.Block("TNOB", 1, bone, 1);

    std::vector<uint8_t> verts(3 * 32, 0);
    const float pos[3][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    for (int i = 0; i < 3; ++i) {
        for (int c = 0; c < 3; ++c) PutF(verts, i * 32 + c * 4, pos[i][c]);
        verts[i * 32 + 0x0C] = 255;                            // bone weight
        verts[i * 32 + 0x14] = 128; verts[i * 32 + 0x15] = 128; verts[i * 32 + 0x16] = 255;
    }
    const uint32_t vertexBlock = b.Block("_8U_", 0, verts, static_cast<uint32_t>(verts.size()));

    std::vector<uint8_t> faces{0, 0, 1, 0, 2, 0};
    const uint32_t faceBlock = b.Block("_61U", 0, faces, 3);

    std::vector<uint8_t> region(48, 0);
    Put32(region, 0x0C, 3); Put32(region, 0x14, 3);
    region[0x18] = 1;
    PutF(region, 0x28, 1.0f);
    const uint32_t regionBlock = b.Block("NGER", 5, region, 1);

    std::vector<uint8_t> batch(14, 0);
    const uint32_t batchBlock = b.Block("_TAB", 1, batch, 1);

    std::vector<uint8_t> division(0x24, 0);
    PutRef(division, 0x00, 3, faceBlock);
    PutRef(division, 0x0C, 1, regionBlock);
    PutRef(division, 0x18, 1, batchBlock);
    const uint32_t divisionBlock = b.Block("VID_", 2, division, 1);

    const uint32_t imageName = b.Text("Creature\\Test\\Tex.dds");
    std::vector<uint8_t> layer(0xC4, 0);
    PutRef(layer, 0x04, 21, imageName);
    const uint32_t layerBlock = b.Block("REYL", 0, layer, 1);
    std::vector<uint8_t> material(340, 0);
    PutRef(material, 0x34, 1, layerBlock);
    const uint32_t materialBlock = b.Block("_TAM", 19, material, 1);

    std::vector<uint8_t> matm(8, 0);
    Put32(matm, 0, 1); Put32(matm, 4, 0);
    const uint32_t matmBlock = b.Block("MTAM", 0, matm, 1);

    const uint32_t lookupBlock = b.Block("_61U", 0, {0, 0}, 1);
    std::vector<uint8_t> inverse(64, 0);
    for (int i = 0; i < 4; ++i) PutF(inverse, i * 20, 1.0f);
    const uint32_t inverseBlock = b.Block("TAM4", 0, inverse, 1);

    std::vector<uint8_t> model(0x2A0, 0);
    PutRef(model, 0x050, 1, bones);
    PutRef(model, 0x064, 96, vertexBlock);
    PutRef(model, 0x070, 1, divisionBlock);
    PutRef(model, 0x07C, 1, lookupBlock);
    PutF(model, 0x88, -1); PutF(model, 0x8C, -1); PutF(model, 0x90, -1);
    PutF(model, 0x94, 1); PutF(model, 0x98, 1); PutF(model, 0x9C, 1);
    PutF(model, 0xA0, 2);
    PutRef(model, 0x12C, 1, matmBlock);
    PutRef(model, 0x138, 1, materialBlock);
    PutRef(model, 0x288, 1, inverseBlock);
    const uint32_t modelBlock = b.Block("LDOM", 23, model, 1);

    while (b.bytes.size() % 16) b.bytes.push_back(0);
    const uint32_t indexOffset = static_cast<uint32_t>(b.bytes.size());
    for (const auto& e : b.entries) {
        uint8_t raw[16];
        std::memcpy(raw, e.tag, 4);
        std::memcpy(raw + 4, &e.offset, 4);
        std::memcpy(raw + 8, &e.n, 4);
        std::memcpy(raw + 12, &e.version, 4);
        b.bytes.insert(b.bytes.end(), raw, raw + 16);
    }
    const uint32_t magic = 0x4D443334, count = static_cast<uint32_t>(b.entries.size());
    std::memcpy(b.bytes.data(), &magic, 4);
    std::memcpy(b.bytes.data() + 4, &indexOffset, 4);
    std::memcpy(b.bytes.data() + 8, &count, 4);
    PutRef(b.bytes, 12, 1, modelBlock);
    return b.bytes;
}

static void WriteFile(const fs::path& path, const std::vector<uint8_t>& data) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

static uint32_t Get32(const std::vector<uint8_t>& v, size_t at) { uint32_t x; std::memcpy(&x, v.data() + at, 4); return x; }

int main() {
    const fs::path exe = fs::canonical("/proc/self/exe");
    // The drop-in folder is found next to the executable, whatever the case of its name, and files
    // inside it match by lowercase name.
    const fs::path data = exe.parent_path() / "Data";
    fs::remove_all(data);
    const fs::path root = data / "Patch-ZB.mpq";
    const std::vector<uint8_t> valid = MakeM3();
    WriteFile(root / "Creature" / "Test" / "TEST.M3", valid);
    WriteFile(root / "Creature" / "Test" / "Bad.m3", {'M', 'D', '3', '4', 1, 2, 3});
    // A DDS texture (one DXT1 block) served as the matching .blp.
    std::vector<uint8_t> dds(128 + 8, 0);
    std::memcpy(dds.data(), "DDS ", 4);
    const uint32_t header[] = {124, 0, 4, 4};
    std::memcpy(dds.data() + 4, header, 4); std::memcpy(dds.data() + 12, header + 2, 4); std::memcpy(dds.data() + 16, header + 3, 4);
    const uint32_t mips = 1, pixelFormat = 32;
    std::memcpy(dds.data() + 28, &mips, 4); std::memcpy(dds.data() + 76, &pixelFormat, 4);
    std::memcpy(dds.data() + 84, "DXT1", 4);
    WriteFile(root / "Creature" / "Test" / "Tex.DDS", dds);

    std::vector<uint8_t> out;

    // The model and its skin are converted on request.
    assert(ModernM3Exists("Creature\\Test\\Test.m2") && ModernM3Exists("creature/test/test00.skin"));
    assert(ModernM3Provide("Creature\\Test\\Test.m2", out));
    assert(out.size() > 0x130 && std::memcmp(out.data(), "MD20", 4) == 0 && Get32(out, 4) == 264);
    assert(Get32(out, 0x2C) == 1 /* bones */ && Get32(out, 0x3C) == 3 /* vertices */);
    const std::vector<uint8_t> model = out;
    assert(ModernM3Provide("creature\\test\\test00.skin", out));
    assert(out.size() > 0x30 && std::memcmp(out.data(), "SKIN", 4) == 0);
    // Served again from the cache with identical bytes.
    assert(ModernM3Provide("CREATURE\\TEST\\TEST.M2", out) && out == model);

    // The texture is transcoded from the DDS next to the model.
    assert(ModernM3Exists("Creature\\Test\\Tex.blp") && ModernM3Provide("Creature\\Test\\Tex.blp", out));
    assert(std::memcmp(out.data(), "BLP2", 4) == 0);

    // Names with no drop-in are not claimed, and neither are escapes from the folder.
    assert(!ModernM3Exists("Creature\\Other\\Thing.m2") && !ModernM3Provide("Creature\\Other\\Thing.m2", out));
    assert(!ModernM3Exists("Spells\\Cube.blp") && !ModernM3Exists("Creature\\Test\\Test.mdx") && !ModernM3Exists("..\\x.m2"));
    assert(!ModernM3Provide(nullptr, out) && !ModernM3Exists(""));

    // A damaged file is refused (in the worker) and the client carries on; the failure is remembered.
    assert(!ModernM3Provide("Creature\\Test\\Bad.m2", out) && !ModernM3Provide("Creature\\Test\\Bad.m2", out));

    // Mutated copies of a valid model: every request returns, the client survives, and anything
    // served is a well-formed model.
    std::mt19937 random(12345);
    unsigned served = 0, refused = 0;
    for (int i = 0; i < 120; ++i) {
        std::vector<uint8_t> mutated = valid;
        const int flips = 1 + static_cast<int>(random() % 6);
        for (int f = 0; f < flips; ++f) mutated[random() % mutated.size()] = static_cast<uint8_t>(random());
        if (i % 7 == 0) mutated.resize(random() % mutated.size());
        char name[64];
        std::snprintf(name, sizeof(name), "Fuzz%d", i);
        WriteFile(root / "Creature" / "Test" / (std::string(name) + ".m3"), mutated);
        const std::string served_name = std::string("Creature\\Test\\") + name + ".m2";
        if (ModernM3Provide(served_name.c_str(), out)) {
            assert(out.size() >= 0x130 && std::memcmp(out.data(), "MD20", 4) == 0);
            ++served;
        } else {
            ++refused;
        }
    }
    std::printf("mutations: %u served, %u refused\n", served, refused);
    fs::remove_all(data);
    return 0;
}
