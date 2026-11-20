#include "Engine/Source/ModernAssets/ModernM2.h"
#include "Engine/Source/ModernAssets/WxlM2/models/m2/Downport.hpp"
#include "Engine/Source/ModernAssets/WxlM2/structure/m2/M2Format.hpp"

#include <cassert>
#include <cstring>
#include <string>
#include <vector>

#ifdef NDEBUG
#error Particle BlendAdd tests require enabled assertions
#endif

bool ResolveModernAssetFileDataId(uint32_t, std::string&) { return false; }

namespace {
namespace fmt = wxl::structure::m2;
namespace downport = wxl::modern::assets::m2::downport;

constexpr uint32_t sourceStride = 0x1EC;
constexpr uint32_t clientStride = 0x1DC;
constexpr uint32_t blendOffset = 0x28;
constexpr uint32_t emitterCount = 7;
constexpr uint32_t textureOffset = sizeof(fmt::M2Header);
constexpr uint32_t emitterOffset = textureOffset + sizeof(fmt::M2Texture);
constexpr uint32_t exp2Tag = 0x32505845;

template<class T>
void Write(std::vector<uint8_t>& bytes, size_t offset, T value) {
    assert(offset + sizeof(T) <= bytes.size());
    std::memcpy(bytes.data() + offset, &value, sizeof(T));
}

std::vector<uint8_t> Model() {
    std::vector<uint8_t> model(emitterOffset + emitterCount * sourceStride);
    fmt::M2Header header{};
    header.magic = fmt::kMagicMD20;
    header.version = 274;
    header.textures = {1, textureOffset};
    header.particleEmitters = {emitterCount, emitterOffset};
    Write(model, 0, header);
    constexpr uint32_t flags[emitterCount] = {1, 0, 0x10000001, 0x100001, 1, 1, 1};
    for (uint32_t i = 0; i < emitterCount; ++i) {
        const auto start = emitterOffset + i * sourceStride;
        Write(model, start + 4, flags[i]);
        Write<uint16_t>(model, start + 0x30, 1);
        Write<uint16_t>(model, start + 0x32, 1);
        model[start + blendOffset] = 7;
    }
    Write<uint32_t>(model, emitterOffset + 4 * sourceStride + 0x18, 2);
    Write<uint16_t>(model, emitterOffset + 5 * sourceStride + 0x16, 9);
    model[emitterOffset + 6 * sourceStride + blendOffset] = 4;
    return model;
}

void AppendChunk(std::vector<uint8_t>& container, uint32_t tag, const std::vector<uint8_t>& bytes) {
    const auto header = container.size();
    container.resize(header + 8);
    Write(container, header, tag);
    Write(container, header + 4, static_cast<uint32_t>(bytes.size()));
    container.insert(container.end(), bytes.begin(), bytes.end());
}

uint8_t Blend(const std::vector<uint8_t>& model, uint32_t index) {
    return model.at(emitterOffset + index * clientStride + blendOffset);
}
}

int main() {
    const auto source = Model();
    std::vector<uint8_t> output;
    assert(TransformModernM2("Spells\\Fixture.m2", source.data(), source.size(), output) ==
        ModernM2TransformResult::Transformed);
    assert(Blend(output, 0) == 7);
    for (uint32_t i = 1; i < 6; ++i) {
        assert(Blend(output, i) == 3);
    }
    assert(Blend(output, 6) == 4);
    assert(source.at(emitterOffset + blendOffset) == 7);

    auto exported = source;
    const auto workSize = downport::WorkSize(source.data(), source.size());
    assert(workSize >= source.size());
    exported.resize(workSize);
    assert(downport::ProcessInPlace(exported.data(), source.size(), workSize));
    assert(Blend(exported, 0) == 3);

    std::vector<uint8_t> wrapped;
    AppendChunk(wrapped, fmt::kMagicMD21, source);
    std::vector<uint8_t> exp2(16 + emitterCount * 28);
    Write(exp2, 0, emitterCount);
    Write<uint32_t>(exp2, 4, 16);
    for (uint32_t i = 0; i < emitterCount; ++i) {
        Write(exp2, 16 + i * 28 + 4, 1.0f);
        Write(exp2, 16 + i * 28 + 8, 1.0f);
    }

    auto identity = wrapped;
    AppendChunk(identity, exp2Tag, exp2);
    assert(TransformModernM2("Spells\\Fixture.m2", identity.data(), identity.size(), output) ==
        ModernM2TransformResult::Transformed);
    assert(Blend(output, 0) == 7);

    auto duplicate = identity;
    AppendChunk(duplicate, exp2Tag, exp2);
    assert(TransformModernM2("Spells\\Fixture.m2", duplicate.data(), duplicate.size(), output) ==
        ModernM2TransformResult::Transformed);
    assert(Blend(output, 0) == 3);

    auto modified = wrapped;
    Write(exp2, 16 + 4, 2.0f);
    AppendChunk(modified, exp2Tag, exp2);
    assert(TransformModernM2("Spells\\Fixture.m2", modified.data(), modified.size(), output) ==
        ModernM2TransformResult::Transformed);
    assert(Blend(output, 0) == 3);

    AppendChunk(wrapped, exp2Tag, {0});
    assert(TransformModernM2("Spells\\Fixture.m2", wrapped.data(), wrapped.size(), output) ==
        ModernM2TransformResult::Transformed);
    assert(Blend(output, 0) == 3);
}
