#include "Engine/Source/ModernAssets/ModernM2.h"
#include "Engine/Source/ModernAssets/WxlM2/structure/m2/M2Format.hpp"
#include <cassert>
#include <cstring>
#include <string>
#include <vector>

bool ResolveModernAssetFileDataId(uint32_t, std::string&) { return false; }

int main() {
    namespace fmt = wxl::structure::m2;
    std::vector<uint8_t> input(sizeof(fmt::M2Header) + sizeof(fmt::M2Texture), 0);
    auto* header = reinterpret_cast<fmt::M2Header*>(input.data());
    header->magic = fmt::kMagicMD20;
    header->version = fmt::kClientVersion;
    header->textures = {1, sizeof(fmt::M2Header)};
    auto* texture = reinterpret_cast<fmt::M2Texture*>(input.data() + sizeof(fmt::M2Header));
    texture->type = fmt::kTexTypeWeaponBlade;
    std::vector<uint8_t> output;
    assert(TransformModernM2("Item\\ObjectComponents\\Collections\\test.m2", input.data(), input.size(), output) ==
           ModernM2TransformResult::RepairedNative);
    auto* repaired = reinterpret_cast<const fmt::M2Texture*>(output.data() + sizeof(fmt::M2Header));
    assert(repaired->type == fmt::kTexTypeObjectSkin);
    assert(texture->type == fmt::kTexTypeWeaponBlade);
    assert(TransformModernM2("Item\\ObjectComponents\\Weapon\\test.m2", input.data(), input.size(), output) ==
           ModernM2TransformResult::NotApplicable);
    texture->type = fmt::kTexTypeObjectSkin;
    assert(TransformModernM2("Item\\ObjectComponents\\Collections\\test.m2", input.data(), input.size(), output) ==
           ModernM2TransformResult::NotApplicable);

    // A matching private global loop already exists, so this repair needs no
    // appended bytes but must still repoint the animated texture track.
    const uint32_t headerSize = sizeof(fmt::M2Header), transformOffset = headerSize;
    const uint32_t loopsOffset = transformOffset + 60, outerOffset = loopsOffset + 12, timesOffset = outerOffset + 8;
    input.assign(timesOffset + 8, 0);
    header = reinterpret_cast<fmt::M2Header*>(input.data());
    header->magic = fmt::kMagicMD20;
    header->version = fmt::kClientVersion;
    header->textureTransforms = {1, transformOffset};
    header->globalLoops = {3, loopsOffset};
    uint32_t loops[] = {1000, 1000, 1000};
    std::memcpy(input.data() + loopsOffset, loops, sizeof(loops));
    auto* track = reinterpret_cast<fmt::M2TrackHeader*>(input.data() + transformOffset);
    track->globalSequence = 0;
    track->timestamps = {1, outerOffset};
    const fmt::M2Array timesArray{2, timesOffset};
    std::memcpy(input.data() + outerOffset, &timesArray, sizeof(timesArray));
    const uint32_t times[] = {0, 1000};
    std::memcpy(input.data() + timesOffset, times, sizeof(times));
    assert(TransformModernM2("Item\\ObjectComponents\\Collections\\test.m2", input.data(), input.size(), output) ==
           ModernM2TransformResult::RepairedNative);
    const auto* repairedTrack = reinterpret_cast<const fmt::M2TrackHeader*>(output.data() + transformOffset);
    assert(output.size() == input.size() && repairedTrack->globalSequence == 2);
    assert(TransformModernM2("Item\\ObjectComponents\\Weapon\\test.m2", input.data(), input.size(), output) ==
           ModernM2TransformResult::RepairedNative);

    std::vector<uint8_t> animation(12);
    const uint32_t wrapper[] = {fmt::kMagicAFM2, 4, 0x12345678};
    std::memcpy(animation.data(), wrapper, sizeof(wrapper));
    uint32_t size = animation.size();
    assert(TransformModernExternalAnimation(animation.data(), size) == ModernAnimationTransformResult::Unwrapped);
    assert(size == 4 && std::memcmp(animation.data(), &wrapper[2], 4) == 0);
    assert(TransformModernExternalAnimation(animation.data(), size) == ModernAnimationTransformResult::NotApplicable);
    animation.resize(20, 0);
    std::memcpy(animation.data(), wrapper, sizeof(wrapper));
    const uint32_t split = fmt::kMagicAFSB;
    std::memcpy(animation.data() + 12, &split, 4);
    const auto original = animation;
    size = animation.size();
    assert(TransformModernExternalAnimation(animation.data(), size) == ModernAnimationTransformResult::Rejected);
    assert(animation == original && size == original.size());
    // The rejection log names what the file holds: the wrapper, then the split stream it could not flatten.
    assert(DescribeModernAnimationChunks(animation.data(), animation.size()) == "AFM2:4 AFSB:0");
    {
        std::vector<uint8_t> two(8 + 4 + 8 + 2, 0);
        const uint32_t head[] = {fmt::kMagicAFM2, 4, 0, fmt::kMagicAFSB, 2};
        std::memcpy(two.data(), head, sizeof(head));
        assert(DescribeModernAnimationChunks(two.data(), two.size()) == "AFM2:4 AFSB:2");
        // A chunk that claims more than the file holds, an odd tail, and empty input stay inside the buffer.
        const uint32_t overrun[] = {fmt::kMagicAFSA, 9999};
        assert(DescribeModernAnimationChunks(overrun, sizeof(overrun)) == "AFSA:9999(overruns)");
        assert(DescribeModernAnimationChunks(two.data(), 3) == "no chunks");
        assert(DescribeModernAnimationChunks(nullptr, 100) == "no chunks");
        two.push_back(1);
        assert(DescribeModernAnimationChunks(two.data(), two.size()) == "AFM2:4 AFSB:2 +1 trailing bytes");
    }
    for (uint32_t magic : {fmt::kMagicAFSA, fmt::kMagicAFSB, fmt::kMagicAFM2}) {
        size = 4;
        assert(TransformModernExternalAnimation(&magic, size) == ModernAnimationTransformResult::Rejected);
        assert(size == 4);
    }
}
