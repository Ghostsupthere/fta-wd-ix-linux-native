#include "Engine/Source/ModernAssets/ModernPhysics.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <map>
#include <fstream>
#include <iterator>
#include <cstdio>

namespace physics = wow::ec::physics;
namespace phys = wxl::scripts::itemphysics;
template<class T> void Put(std::vector<uint8_t>& bytes, size_t at, T value) {
    assert(at + sizeof(value) <= bytes.size());
    std::memcpy(bytes.data() + at, &value, sizeof(value));
}
static void Chunk(std::vector<uint8_t>& bytes, const char* tag, const std::vector<uint8_t>& payload) {
    const auto at = bytes.size();
    bytes.resize(at + 8 + payload.size());
    for (uint32_t i = 0; i < 4; ++i) bytes[at + i] = tag[3 - i];
    Put(bytes, at + 4, uint32_t(payload.size()));
    std::copy(payload.begin(), payload.end(), bytes.begin() + at + 8);
}
static void Identity(double* matrix) { std::fill(matrix, matrix + 16, 0.); for (size_t i=0; i<4; ++i) matrix[i*5]=1; }
static std::map<std::string, std::vector<uint8_t>> files;
static bool resolverReady = false;
static bool Read(const char* path, std::vector<uint8_t>& bytes) {
    const auto found = files.find(path);
    if (found == files.end()) return false;
    bytes = found->second;
    return true;
}
static bool Resolve(uint32_t id, std::string& path) {
    assert(id == 77);
    path = "physics\\resolved.phys";
    return resolverReady;
}
int main(int argc, char** argv) {
    const auto paths = phys::CompanionPaths("Item/ObjectComponents/Head/Helm_HuF.mdx");
    assert(paths.size() == 2 && paths[0].ends_with("helm_huf.phys") && paths[1].ends_with("helm_hu_f.phys"));
    std::vector<uint8_t> version(2), body(28), shape(32), sphere(16), bytes;
    Put(version, 0, uint16_t(0));
    Put(body, 0, uint16_t(1)); // Dynamic legacy BODY, bone zero, one sphere.
    Put(body, 24, int32_t(1));
    Put(shape, 0, uint16_t(2));
    Put(shape, 8, .6f); Put(shape, 16, 1.f);
    Put(sphere, 12, .1f);
    Chunk(bytes, "PHYS", version); Chunk(bytes, "BODY", body);
    Chunk(bytes, "SHP2", shape); Chunk(bytes, "SPHS", sphere);
    physics::Model model;
    std::string error;
    assert(phys::Decode(bytes, model, error));
    assert(model.bodies.size() == 1 && model.shapes.size() == 1 && model.bodies[0].bone == 0);
    assert(model.bodies[0].type == physics::kBodyDynamic && model.shapes[0].r == .1f);
    for (size_t size = 0; size < bytes.size(); ++size) {
        physics::Model rejected;
        assert(!phys::Decode(std::span(bytes).first(size), rejected, error));
    }
    std::array<physics::V3, 1> pivots{};
    std::array<int16_t, 1> parents{-1};
    assert(phys::BindSkeleton(model, pivots, parents, error));
    assert(!phys::ValidateBones(model, 0, error));
    double bones[1][16]; Identity(bones[0]);
    physics::Frame frame;
    frame.boneWorld = bones; frame.pivots = pivots.data(); frame.boneCount = 1; frame.dt = 1.f / 60;
    Identity(frame.placement);
    physics::Instance instance(&model);
    for (uint32_t tick = 0; tick < 60; ++tick) instance.Step(frame);
    double simulated[16];
    assert(instance.Output(0, simulated));
    for (double value : simulated) assert(std::isfinite(value));
    assert(simulated[11] < -.1); // Column-form world gravity moves Z downward.
    const double previous = simulated[11];
    frame.dt = 0; instance.Step(frame); assert(instance.Output(0, simulated));
    assert(std::abs(previous - simulated[11]) < 1.e-8);

    float native[16]{}, scene[16]{}, output[16]{};
    for (size_t i=0; i<4; ++i) native[i*5] = scene[i*5] = 1;
    native[12]=1; native[13]=2; native[14]=3;
    scene[5]=-1; scene[12]=10; scene[13]=20; scene[14]=30;
    double world[16], identity[16]; Identity(identity);
    physics::World(native, scene, world);
    assert(world[3] == 11 && world[7] == 18 && world[11] == 33);
    physics::FromModel(identity, native, output);
    for (size_t i=0; i<16; ++i) assert(output[i] == native[i]);
    int16_t hierarchy[3]{-1,0,1}; bool moved[3]{true,false,false};
    float before[48]{}, carried[48]{};
    for (size_t bone=0; bone<3; ++bone) {
        for (size_t i=0; i<4; ++i) before[bone*16+i*5]=1;
        before[bone*16+12]=float(bone);
    }
    std::copy(before,before+48,carried); carried[12]=5;
    physics::Carry(hierarchy,3,before,carried,moved);
    assert(carried[28] == 6 && carried[44] == 7);

    using Result = ModernPhysicsLoadResult;
    uint32_t pending = 0;
    const char* name = "Item/ObjectComponents/Head/Helm_HuF.mdx";
    files[paths[1]] = bytes;
    assert(LoadModernPhysicsAsset(name, Read, Resolve, model, error, pending) == Result::Loaded);
    files[paths[0]] = {}; // An invalid winning companion must not fall through.
    assert(LoadModernPhysicsAsset(name, Read, Resolve, model, error, pending) == Result::Rejected);
    files.clear();
    assert(LoadModernPhysicsAsset(name, Read, Resolve, model, error, pending) == Result::Missing);
    std::vector<uint8_t> md21;
    Chunk(md21, "12DM", std::vector<uint8_t>(8)); // MD21 uses non-reversed tags.
    Chunk(md21, "CDFP", bytes);
    const auto modelPath = "item\\objectcomponents\\head\\helm_huf.m2";
    files[modelPath] = md21;
    assert(LoadModernPhysicsAsset(name, Read, Resolve, model, error, pending) == Result::Loaded);
    md21.clear();
    Chunk(md21, "12DM", std::vector<uint8_t>(8));
    std::vector<uint8_t> fileId(4); Put(fileId, 0, uint32_t(77));
    Chunk(md21, "DIFP", fileId);
    files[modelPath] = md21;
    assert(LoadModernPhysicsAsset(name, Read, Resolve, model, error, pending) == Result::PendingFileDataId);
    assert(pending == 77);
    resolverReady = true; files["physics\\resolved.phys"] = bytes;
    assert(LoadModernPhysicsAsset(name, Read, Resolve, model, error, pending) == Result::Loaded && !pending);
    md21.pop_back(); files[modelPath] = md21;
    assert(LoadModernPhysicsAsset(name, Read, Resolve, model, error, pending) == Result::Rejected);
    assert(LoadModernPhysicsAsset("Creature\\NoPhysics.m2", nullptr, nullptr, model, error, pending) == Result::Missing);

    if (argc == 3) {
        auto read = [](const char* path) {
            std::ifstream stream(path, std::ios::binary);
            assert(stream);
            return std::vector<uint8_t>(std::istreambuf_iterator<char>(stream), {});
        };
        auto installedPhysics = read(argv[1]);
        auto installedModel = read(argv[2]);
        if (installedModel.size() >= 8 && !std::memcmp(installedModel.data(), "MD21", 4)) {
            uint32_t size; std::memcpy(&size, installedModel.data()+4, 4);
            assert(size <= installedModel.size()-8);
            installedModel = std::vector<uint8_t>(installedModel.begin()+8, installedModel.begin()+8+size);
        }
        assert(installedModel.size() >= 52 && !std::memcmp(installedModel.data(), "MD20", 4));
        uint32_t count, offset;
        std::memcpy(&count, installedModel.data()+44, 4); std::memcpy(&offset, installedModel.data()+48, 4);
        assert(count && count <= 256 && offset <= installedModel.size() && count <= (installedModel.size()-offset)/88);
        std::vector<physics::V3> actualPivots(count);
        std::vector<int16_t> actualParents(count);
        for (uint32_t i=0; i<count; ++i) {
            std::memcpy(&actualParents[i], installedModel.data()+offset+i*88+8, 2);
            std::memcpy(&actualPivots[i], installedModel.data()+offset+i*88+76, 12);
        }
        physics::Model actual;
        if (!phys::Decode(installedPhysics, actual, error) || !phys::BindSkeleton(actual, actualPivots, actualParents, error)) {
            std::fprintf(stderr, "installed PHYS rejected: %s\n", error.c_str());
            return 1;
        }
        physics::Instance actualSolver(&actual);
        std::vector<std::array<double,16>> actualBones(count);
        for (auto& matrix : actualBones) Identity(matrix.data());
        frame.boneWorld = reinterpret_cast<const double (*)[16]>(actualBones.data());
        frame.pivots=actualPivots.data(); frame.boneCount=count; frame.dt=1.f/60;
        for (uint32_t tick=0; tick<60; ++tick) {
            actualSolver.Step(frame);
            for (size_t i=0; i<actual.bodies.size(); ++i) if (actualSolver.Output(i, simulated))
                for (double value : simulated) assert(std::isfinite(value));
        }
        std::printf("installed PHYS: %u bones, %zu bodies, %zu joints; bind and 60 finite simulation steps passed\n",
                    count, actual.bodies.size(), actual.joints.size());
    }
}
