#include "Engine/Source/ModernAssets/ModernWmo.h"
#include "Engine/Source/ModernAssets/WxlWmo/models/wmo/WmoVisualData.hpp"
#include <cassert>
#include <cstring>
#include <array>
#include <limits>

bool ResolveModernAssetFileDataId(uint32_t id, std::string& path) {
    if (id < 1000) return false;
    path = "Textures\\" + std::to_string(id) + ".blp"; return true;
}
template<class T> static void Put(std::vector<uint8_t>& bytes, size_t at, T value) {
    assert(at + sizeof(value) <= bytes.size()); std::memcpy(bytes.data() + at, &value, sizeof(value));
}
static void Chunk(std::vector<uint8_t>& bytes, const char* tag, const std::vector<uint8_t>& data) {
    const size_t at = bytes.size(); bytes.resize(at + 8 + data.size());
    for (uint32_t i=0; i<4; ++i) bytes[at+i] = tag[3-i];
    Put(bytes, at+4, uint32_t(data.size()));
    std::copy(data.begin(),data.end(),bytes.begin()+at+8);
}
static std::vector<uint8_t> Root(uint32_t shader, bool ids) {
    std::vector<uint8_t> bytes, version(4), header(64), material(64), names;
    Put(version,0,uint32_t(17)); Put(header,0,uint32_t(1));
    Put(material,0,uint32_t(0xC0)); Put(material,4,shader); Put(material,0x1C,uint32_t(0xFF123456));
    const uint32_t offsets[]{0x0C,0x18,0x24,0x28,0x30,0x34,0x38,0x3C};
    for (uint32_t i=0; i<8; ++i) {
        Put(material,offsets[i], ids ? 1000+i : uint32_t(names.size()));
        const std::string name = "Textures\\layer" + std::to_string(i) + ".blp";
        names.insert(names.end(),name.begin(),name.end()); names.push_back(0);
    }
    Chunk(bytes,"MVER",version); Chunk(bytes,"MOHD",header);
    Chunk(bytes,"MOTX",names); Chunk(bytes,"MOMT",material);
    Chunk(bytes,"GFID",std::vector<uint8_t>(4));
    return bytes;
}
static std::vector<uint8_t> Group(bool invalidUv = false, uint32_t sets = 4, bool hasWeights = true) {
    std::vector<uint8_t> bytes, version(4), group(68), positions(36), uv(24), weights(12);
    Put(version,0,uint32_t(17));
    Chunk(group,"MOVT",positions);
    for (uint32_t set=0; set<sets; ++set) {
        for (uint32_t vertex=0; vertex<3; ++vertex) {
            Put(uv,vertex*8,float(set+vertex)); Put(uv,vertex*8+4,float(set)*.25f);
        }
        if (invalidUv && set==3) Put(uv,0,std::numeric_limits<float>::quiet_NaN());
        Chunk(group,"MOTV",uv);
    }
    Put(weights,0,uint32_t(0x12345678));
    if (hasWeights) Chunk(group,"MOC2",weights);
    Chunk(bytes,"MVER",version); Chunk(bytes,"MOGP",group);
    return bytes;
}
int main() {
    const size_t baseline = ModernWmoVisualBytes();
    std::vector<uint8_t> output, plain;
    std::shared_ptr<ModernWmoRootVisuals> root;
    auto source = Root(6,false);
    assert(TransformModernWmoRoot(source.data(),source.size(),output,&root)==ModernWmoTransformResult::Transformed);
    assert(root && root->materials.size()==1 && root->materials[0].layers==2);
    assert(root->materials[0].textures[0]=="Textures\\layer0.blp");
    assert(root->materials[0].textures[1]=="Textures\\layer1.blp");
    assert(root->materials[0].flags==0xC0 && root->materials[0].tint==0xFF123456);
    assert(ModernWmoVisualBytes()>baseline);
    TransformModernWmoRoot(source.data(),source.size(),plain);
    assert(output==plain); // No private envelope enters the native chunk parser.
    source=Root(23,true);
    TransformModernWmoRoot(source.data(),source.size(),output,&root);
    assert(root && root->materials[0].layers==4 && root->materials[0].textures[7]=="Textures\\1007.blp");
    auto retained = root; root.reset(); assert(ModernWmoVisualBytes()>baseline);
    retained.reset(); assert(ModernWmoVisualBytes()==baseline);

    std::shared_ptr<ModernWmoGroupVisuals> group;
    source=Group(); TransformModernWmoGroup(source.data(),source.size(),output,&group);
    assert(group && group->uvSets==4 && group->vertices.size()==3 && group->hasWeights);
    assert(group->vertices[1].uv[3][0]==4 && group->vertices[0].weights==0x12345678);
    TransformModernWmoGroup(source.data(),source.size(),plain); assert(output==plain);
    group.reset(); assert(ModernWmoVisualBytes()==baseline);
    source=Group(true); TransformModernWmoGroup(source.data(),source.size(),output,&group);
    assert(!group && ModernWmoVisualBytes()==baseline);
    source=Group(false,2,false);
    assert(TransformModernWmoGroup(source.data(),source.size(),output,&group)==ModernWmoTransformResult::NotApplicable);
    assert(group && group->uvSets==2 && !group->hasWeights && group->vertices.size()==3 && output.empty());
    group.reset(); assert(ModernWmoVisualBytes()==baseline);
}
