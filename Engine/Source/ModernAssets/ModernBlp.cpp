#include "Engine/Source/ModernAssets/ModernBlp.h"

#include "WxlBlp/textures/blp/BlpTranscode.hpp"

#include <cctype>
#include <span>
#include <string_view>

namespace {
constexpr uint32_t kMagicBlp1 = 0x31504C42;
constexpr uint32_t kMagicBlp2 = 0x32504C42;
constexpr uint32_t kSafeTextureEdge = 2048;

uint32_t ReadU32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
           (uint32_t(p[3]) << 24);
}

bool StartsWithCi(std::string_view value, std::string_view prefix) {
    if (value.size() < prefix.size()) {
        return false;
    }
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(value[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

bool EndsWithCi(std::string_view value, std::string_view suffix) {
    return value.size() >= suffix.size() &&
           StartsWithCi(value.substr(value.size() - suffix.size()), suffix);
}
} // namespace

ModernBlpTransformResult TransformModernBlp(const char* name, const void* source, uint32_t sourceSize,
                                            std::vector<uint8_t>& output) {
    output.clear();
    const std::string_view path = name ? std::string_view(name) : std::string_view();
    if (!source || !EndsWithCi(path, ".blp")) {
        return ModernBlpTransformResult::NotApplicable;
    }

    const std::span<const uint8_t> raw(static_cast<const uint8_t*>(source), sourceSize);
    if (raw.size() < 4) {
        return ModernBlpTransformResult::Rejected;
    }
    const uint32_t magic = ReadU32(raw.data());
    if (magic == kMagicBlp1) {
        return ModernBlpTransformResult::NotApplicable;
    }
    if (magic != kMagicBlp2 || raw.size() < 0x94 || ReadU32(raw.data() + 4) != 1) {
        return ModernBlpTransformResult::Rejected;
    }

    const uint8_t encoding = raw[0x08];
    const uint32_t width = ReadU32(raw.data() + 0x0c);
    const uint32_t height = ReadU32(raw.data() + 0x10);
    if (encoding < 1 || encoding > 3 || !width || !height || width > kSafeTextureEdge ||
        height > kSafeTextureEdge) {
        return ModernBlpTransformResult::Rejected;
    }

    const bool component = StartsWithCi(path, "item\\texturecomponents\\");
    if (component && encoding == 2) {
        return wxl::modern::assets::textures::blp::TextureComponentToPaletted(raw, output)
            ? ModernBlpTransformResult::Transformed
            : ModernBlpTransformResult::Rejected;
    }
    if (component && encoding == 3) {
        return ModernBlpTransformResult::Rejected;
    }
    if (encoding == 3) {
        return wxl::modern::assets::textures::blp::TranscodeBlp(raw, output)
            ? ModernBlpTransformResult::Transformed
            : ModernBlpTransformResult::Rejected;
    }
    return ModernBlpTransformResult::NotApplicable;
}
