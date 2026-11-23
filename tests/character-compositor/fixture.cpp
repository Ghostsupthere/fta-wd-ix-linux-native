#include "Engine/Source/ModernAssets/ModernBlp.h"
#include "Engine/Source/ModernAssets/NativeDiagnostic.h"
#include "WoW/Common/ComponentCore/CharacterComponent.h"
#include "WoW/Common/ComponentCore/CharacterCompositor.hpp"

#include <array>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

static uint32_t s_textureSize;
uint32_t CCharacterComponent::s_mipLevels;
static CompSectionInfo s_sectionInfo[NUM_COMPONENT_SECTIONS];

CACHEENTRY::CACHEENTRY() = default;
CACHEENTRY::~CACHEENTRY() = default;

void* TextureCacheGetMip(HTEXTURECACHE texture, uint32_t mip) {
    if (mip >= texture->m_info.m_mipCount) return nullptr;
    return reinterpret_cast<uint8_t*>(texture->m_data) + texture->m_data->m_mipOffsets[mip];
}

BlpPalPixel* TextureCacheGetPal(HTEXTURECACHE texture) {
    return texture->m_data->m_colorEncoding == COLOR_PAL ? texture->m_data->m_extended.m_palette : nullptr;
}

int32_t TextureCacheGetInfo(HTEXTURECACHE texture, TCTEXTUREINFO& out, int32_t) {
    out = texture->m_info;
    return 1;
}

int32_t TextureCacheHasMips(HTEXTURECACHE texture) {
    return texture && texture->m_data && !texture->m_loadFailed;
}

#include "paste.inc"

int main(int argc, char** argv) {
    assert(argc == 3);
    const unsigned level = std::stoul(argv[1]);
    s_textureSize = 1u << level;
    CCharacterComponent::s_mipLevels = level;
    for (int i = 0; i < NUM_COMPONENT_SECTIONS; ++i) {
        const auto raw = CCharacterComponent::s_sectionInfoRaw[i];
        const auto scale = [=](int value) { return wxl::compositor::ScaleCoordinate(value, level); };
        s_sectionInfo[i] = {{scale(raw.m_pos.x), scale(raw.m_pos.y)}, {scale(raw.m_size.x), scale(raw.m_size.y)}};
    }
    constexpr uint32_t sentinel = 0x7D897563;
    C4Pixel initial{};
    std::memcpy(&initial, &sentinel, sizeof(initial));
    std::array<std::vector<C4Pixel>, 11> storage;
    std::array<C4Pixel*, 11> pointers;
    for (unsigned mip = 0; mip <= level; ++mip) {
        const unsigned edge = s_textureSize >> mip;
        storage[mip].resize(edge * edge);
        pointers[mip] = storage[mip].data();
    }
    auto* atlas = reinterpret_cast<MipBits*>(pointers.data());

    const std::filesystem::path manifestPath = argv[2];
    std::ifstream manifest(manifestPath);
    assert(manifest);
    uint64_t hash = 1469598103934665603ull;
    unsigned assets = 0, pastes = 0, missed = 0;
    for (std::string line; std::getline(manifest, line);) {
        const auto tab = line.find('\t');
        assert(tab != std::string::npos);
        const auto kind = line.substr(0, tab);
        const auto path = manifestPath.parent_path() / line.substr(tab + 1);
        std::ifstream file(path, std::ios::binary);
        assert(file);
        std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(file), {});
        std::vector<uint8_t> converted;
        const auto result = TransformModernBlp(path.c_str(), bytes.data(), bytes.size(), converted, true);
        assert(result != ModernBlpTransformResult::Rejected);
        if (result == ModernBlpTransformResult::Transformed) bytes.swap(converted);
        assert(bytes.size() >= sizeof(BLPHeader));

        CACHEENTRY entry{};
        entry.m_loadFailed = 0;
        entry.m_size = bytes.size();
        entry.m_data = reinterpret_cast<BLPHeader*>(bytes.data());
        path.filename().string().copy(entry.m_fileName, sizeof(entry.m_fileName) - 1);
        entry.m_info.m_width = entry.m_data->m_width;
        entry.m_info.m_height = entry.m_data->m_height;
        entry.m_info.m_alphaSize = entry.m_data->m_alphaSize;
        entry.m_info.m_opaque = entry.m_info.m_alphaSize == 0;
        entry.m_info.m_mipCount = TextureCalcMipCount(entry.m_info.m_width, entry.m_info.m_height);

        const int first = kind == "base" ? 0 : kind == "lower" ? SECTION_HEAD_LOWER : SECTION_HEAD_UPPER;
        const int last = kind == "base" ? NUM_COMPONENT_SECTIONS - 1 : first;
        ++assets;
        for (int index = first; index <= last; ++index) {
            const auto section = static_cast<COMPONENT_SECTIONS>(index);
            const auto layout = s_sectionInfo[index];
            for (unsigned mip = 0; mip <= level; ++mip)
                std::fill(storage[mip].begin(), storage[mip].end(), initial);
            if (kind == "base") CCharacterComponent::PasteFromSkin(section, &entry, atlas);
            else CCharacterComponent::PasteToSection(section, &entry, atlas);
            ++pastes;
            bool touched = false;
            for (int y = 0; y < layout.m_size.y; ++y)
                for (int x = 0; x < layout.m_size.x; ++x) {
                    const auto& pixel = storage[0][(layout.m_pos.y + y) * s_textureSize + layout.m_pos.x + x];
                    touched |= std::memcmp(&pixel, &initial, sizeof(pixel)) != 0;
                }
            if (!touched) {
                if (++missed <= 8) std::cerr << path << " left section " << section << " unwritten\n";
            }
            for (unsigned mip = 0; mip <= level; ++mip)
                for (const auto& pixel : storage[mip])
                    for (auto byte : {pixel.m_b, pixel.m_g, pixel.m_r, pixel.m_a}) {
                        hash ^= byte;
                        hash *= 1099511628211ull;
                    }
        }
    }
    std::cout << "Level " << level << ": " << assets << " BLPs, " << pastes << " section pastes, "
              << missed << " blank sections, atlas digest " << std::hex << hash << std::endl;
    return assets == 0 || missed > 0;
}
