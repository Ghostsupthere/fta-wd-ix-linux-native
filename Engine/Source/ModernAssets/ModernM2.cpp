#include "Engine/Source/ModernAssets/ModernM2.h"
#include "Engine/Source/ModernAssets/FileDataIdResolver.h"

#include "models/m2/Contract.hpp"
#include "models/m2/Downport.hpp"
#include "models/m2/Md21.hpp"
#include "models/m2/Skel.hpp"
#include "models/m2/Textures.hpp"
#include "structure/m2/M2Format.hpp"
#include "common/Config.hpp"
#include "common/Text.hpp"

#include <cstring>
#include <limits>
#include <string_view>

namespace {
bool ApplyM2Repairs(std::string_view name, std::vector<uint8_t>& model) {
    namespace fmt = wxl::structure::m2;
    namespace textures = wxl::modern::assets::m2::textures;

    if (model.size() < sizeof(fmt::M2Header) ||
        model.size() > std::numeric_limits<uint32_t>::max()) {
        return false;
    }

    bool changed = false;
    auto* header = reinterpret_cast<fmt::M2Header*>(model.data());
    if (header->magic != fmt::kMagicMD20 || header->version != fmt::kClientVersion) {
        return false;
    }

    if (textures::AllowsWeaponBladeRemap(name)) {
        changed = textures::FixWeaponBladeTextureTypes(header, static_cast<uint32_t>(model.size())).Total() != 0;
    }

    const uint32_t originalSize = static_cast<uint32_t>(model.size());
    if (wxl::modern::assets::common::text::StartsWithCI(name, "item\\objectcomponents\\") &&
        wxl::config::Env("WXL_COLLECTION_TEXTURE_ANIMATION_LOOPS", true) &&
        originalSize <= 64u * 1024u * 1024u) {
        const uint32_t extra = textures::TextureTransformLoopRepairExtraBytes(
            header, originalSize, originalSize, name);
        if (extra > 64u * 1024u * 1024u - originalSize) return changed;
        model.resize(static_cast<size_t>(originalSize) + extra, 0);
        header = reinterpret_cast<fmt::M2Header*>(model.data());
        changed |= textures::RepairTextureTransformLoops(
            header, originalSize, originalSize, static_cast<uint32_t>(model.size()), name) != 0;
    }
    return changed;
}
} // namespace

ModernM2TransformResult TransformModernM2(const char* name, const void* source, uint32_t sourceSize,
                                          std::vector<uint8_t>& output,
                                          ModernM2FileReader sidecarReader) {
    namespace downport = wxl::modern::assets::m2::downport;
    namespace textures = wxl::modern::assets::m2::textures;
    namespace fmt = wxl::structure::m2;

    output.clear();
    if (!source) {
        return ModernM2TransformResult::NotApplicable;
    }

    const std::string_view modelName = name ? std::string_view(name) : std::string_view();
    const std::span<const uint8_t> raw(static_cast<const uint8_t*>(source), sourceSize);
    std::vector<uint8_t> md20;

    const bool sourceWasMd21 = wxl::modern::assets::m2::md21::IsMd21(raw);
    if (sourceWasMd21) {
        if (!wxl::modern::assets::m2::md21::Dechunk(raw, &ResolveModernAssetFileDataId, md20)) {
            return ModernM2TransformResult::Rejected;
        }
    } else if (downport::IsConvertible(source, sourceSize) ||
               (sourceSize >= sizeof(fmt::M2Header) &&
                static_cast<const fmt::M2Header*>(source)->magic == fmt::kMagicMD20 &&
                static_cast<const fmt::M2Header*>(source)->version == fmt::kClientVersion &&
                (textures::AllowsWeaponBladeRemap(modelName) ||
                 wxl::modern::assets::common::text::StartsWithCI(modelName, "item\\objectcomponents\\")))) {
        md20.assign(raw.begin(), raw.end());
    } else {
        return ModernM2TransformResult::NotApplicable;
    }

    bool mergedSkeleton = false;
    if (sidecarReader && !modelName.empty()) {
        std::string skelName(modelName);
        const size_t extension = skelName.find_last_of('.');
        if (extension != std::string::npos) {
            skelName.resize(extension);
            skelName.append(".skel");
            std::vector<uint8_t> skel;
            if (sidecarReader(skelName.c_str(), skel)) {
                if (!wxl::modern::assets::m2::skel::Merge(skel, md20, 64u * 1024u * 1024u))
                    return ModernM2TransformResult::Rejected;
                mergedSkeleton = true;
            }
        }
    }

    if (md20.size() < sizeof(fmt::M2Header)) {
        return ModernM2TransformResult::Rejected;
    }
    const auto* inputHeader = reinterpret_cast<const fmt::M2Header*>(md20.data());
    if (inputHeader->magic != fmt::kMagicMD20) {
        return ModernM2TransformResult::Rejected;
    }
    if (inputHeader->version == fmt::kClientVersion) {
        const bool repaired = ApplyM2Repairs(modelName, md20);
        if (!sourceWasMd21 && !repaired && !mergedSkeleton) {
            return ModernM2TransformResult::NotApplicable;
        }
        output = std::move(md20);
        return sourceWasMd21 ? ModernM2TransformResult::Transformed : ModernM2TransformResult::RepairedNative;
    }

    const uint32_t modelSize = static_cast<uint32_t>(md20.size());
    const uint32_t workSize = downport::WorkSize(md20.data(), modelSize, modelName);
    if (workSize < modelSize || workSize > 64u * 1024u * 1024u) {
        return ModernM2TransformResult::Rejected;
    }

    output.resize(workSize);
    std::memcpy(output.data(), md20.data(), modelSize);
    if (!downport::ProcessInPlace(output.data(), modelSize, workSize, modelName)) {
        output.clear();
        return ModernM2TransformResult::Rejected;
    }

    auto* header = reinterpret_cast<fmt::M2Header*>(output.data());
    if (header->magic != fmt::kMagicMD20 ||
        header->version != (fmt::kClientVersion | wxl::modern::assets::m2::kStagedVersionBit)) {
        output.clear();
        return ModernM2TransformResult::Rejected;
    }

    header->version = fmt::kClientVersion;
    ApplyM2Repairs(modelName, output);
    return ModernM2TransformResult::Transformed;
}

ModernAnimationTransformResult TransformModernExternalAnimation(void* buffer, uint32_t& size) {
    namespace fmt = wxl::structure::m2;
    if (!buffer || size < 4) {
        return ModernAnimationTransformResult::Rejected;
    }

    auto* bytes = static_cast<uint8_t*>(buffer);
    uint32_t magic = 0;
    uint32_t payload = 0;
    std::memcpy(&magic, bytes, sizeof(magic));
    if (magic == fmt::kMagicAFSA || magic == fmt::kMagicAFSB) return ModernAnimationTransformResult::Rejected;
    if (magic != fmt::kMagicAFM2) return ModernAnimationTransformResult::NotApplicable;
    if (size < 8) return ModernAnimationTransformResult::Rejected;
    std::memcpy(&payload, bytes + sizeof(payload), sizeof(payload));
    if (payload != size - 8) {
        return ModernAnimationTransformResult::Rejected;
    }
    std::memmove(bytes, bytes + 8, payload);
    size = payload;
    return ModernAnimationTransformResult::Unwrapped;
}

std::string DescribeModernAnimationChunks(const void* buffer, uint32_t size) {
    const auto* bytes = static_cast<const uint8_t*>(buffer);
    std::string text;
    uint64_t at = 0;
    for (int chunks = 0; bytes && at + 8 <= size && chunks < 16; ++chunks) {
        uint32_t length = 0;
        std::memcpy(&length, bytes + at + 4, sizeof(length));
        char magic[5] = {};
        for (int i = 0; i < 4; ++i) {
            const char c = static_cast<char>(bytes[at + i]);
            magic[i] = (c >= 0x20 && c < 0x7F) ? c : '?';
        }
        if (!text.empty()) text += ' ';
        text += magic;
        text += ':';
        text += std::to_string(length);
        if (length > size - at - 8) return text + "(overruns)";
        at += 8 + static_cast<uint64_t>(length);
    }
    if (text.empty()) return "no chunks";
    if (at < size) text += " +" + std::to_string(size - at) + " trailing bytes";
    return text;
}
