#include "Engine/Source/Model2/WxlBoneRemap.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>

static std::vector<WxlBoneDescriptor> ReadNativeBones(const char* path) {
    std::ifstream file(path, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(file)), {});
    assert(bytes.size() >= 0x34 && std::memcmp(bytes.data(), "MD20", 4) == 0);
    auto read = [&](size_t at, auto& value) {
        assert(at <= bytes.size() && sizeof(value) <= bytes.size() - at);
        std::memcpy(&value, bytes.data() + at, sizeof(value));
    };
    uint32_t version, count, offset;
    read(4, version); read(0x2c, count); read(0x30, offset);
    assert(version == 264 && offset <= bytes.size() && count <= (bytes.size() - offset) / 88);
    std::vector<WxlBoneDescriptor> result(count);
    for (uint32_t i = 0; i < count; ++i) {
        read(offset + i * 88, result[i].key);
        read(offset + i * 88 + 8, result[i].parent);
        read(offset + i * 88 + 12, result[i].crc);
    }
    return result;
}

int main(int argc, char** argv) {
    std::vector<WxlBoneDescriptor> parent{{7, 70, 0xffff}, {8, 80, 0}, {9, 90, 1}, {10, 70, 0}};
    std::vector<WxlBoneDescriptor> child{
        {8, 70, 0xffff}, // CRC wins over the conflicting key ID.
        {8, 0, 0xffff}, // Key-only match.
        {5, 0, 0xffff}, // Parent key lookup fallback.
        {-1, 0, 0},    // Unnamed accessory follows a mapped ancestor.
        {-1, 0, 0xffff},
        {-1, 0, 6}, {-1, 0, 5} // Unmatched cycle must terminate.
    };
    const uint16_t lookup[] = {0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 2};
    const auto map = WxlBuildBoneRemap(child, parent, lookup, 6);
    assert((map == std::vector<uint32_t>{0, 1, 2, 0, UINT32_MAX, UINT32_MAX, UINT32_MAX}));
    std::vector<WxlBoneDescriptor> chain(20, {-1, 0, 0xffff});
    for (uint16_t i = 0; i < 19; ++i) chain[i].parent = i + 1;
    chain.back().crc = 90;
    const auto deep = WxlBuildBoneRemap(chain, parent);
    for (uint32_t index : deep) assert(index == 2);
    parent.assign(300, {-1, 0, 0xffff});
    parent.back().crc = 1234;
    assert(WxlBuildBoneRemap({{-1, 1234, 0xffff}}, parent)[0] == 299);
    assert(WxlBuildBoneRemap(std::vector<WxlBoneDescriptor>(257), parent).empty());
    assert(WxlBuildBoneRemap(child, {}).empty());
    if (argc == 3) {
        const auto actualChild = ReadNativeBones(argv[1]);
        const auto actualParent = ReadNativeBones(argv[2]);
        const auto actual = WxlBuildBoneRemap(actualChild, actualParent);
        assert(!actual.empty());
        for (uint32_t index : actual) assert(index < actualParent.size());
    }
}
