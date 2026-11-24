#include "Engine/Source/ModernAssets/WxlSurfaceLights/DoodadLightTables.hpp"
#include "Engine/Source/ModernAssets/WxlSurfaceLights/StaticLampCatalog.hpp"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace sl = wxl::surfacelights;
struct C3Vector { float x, y, z; };
struct C44Matrix {
    float values[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};
const float* Floats(const C44Matrix& m) { return m.values; }
struct M2Light {
    int m_type = 1;
    C3Vector m_position{0, 0, 3}, m_diffuseColor{1, 0.5f, 0.25f};
    float m_diffuseIntensity = 1;
};
struct M2Data {
    struct Lights {
        uint32_t m_count = 0;
        M2Light light;
        const M2Light& operator[](size_t index) const { assert(index == 0); return light; }
    } m_lights;
};
struct CM2Shared {
    char m_fileName[260]{};
    mutable uint64_t m_surfaceLightCatalogKey = UINT64_MAX;
    mutable uint8_t m_surfaceLightPathFlags = 0;
};
struct CM2Model;
struct CM2Scene { CM2Model* m_modelList = nullptr; } scene;
struct CM2Model {
    CM2Shared* m_shared = nullptr;
    M2Data* data = nullptr;
    CM2Scene* m_scene = &scene;
    CM2Model* m_sceneNext = nullptr;
    CM2Model* m_attachParent = nullptr;
    CM2Model* m_attachList = nullptr;
    CM2Model* m_attachNext = nullptr;
    void* m_lights = nullptr;
    C44Matrix m_worldTransform;
};
struct CMapBaseObj { enum { Type_MapObjDefGroup = 1 }; int m_type = 0; };
struct Link { CMapBaseObj* m_ref = nullptr; Link* next = nullptr; };
template<class Node> struct List {
    Node* head = nullptr;
    Node* Head() const { return head; }
    Node* Next(Node* node) const { return node->next; }
};
struct Doodad {
    CM2Model* m_model = nullptr;
    C44Matrix m_mat;
    int m_flags = 0;
    List<Link> m_parentLinkList;
    Doodad* next = nullptr;
};
struct Wmo {
    bool m_isGroupLoaded = true;
    const char* m_wmoName = "fixture.wmo";
    int m_mapObjGroupCount = 1;
    struct Group { int m_unkLoadedFlag = 1; } group, *m_mapObjGroupArray[1] = {&group};
    int GetGroupFlags(int) const { return 0; }
};
struct WmoDef {
    Wmo* m_owner = nullptr;
    C44Matrix m_mat;
    int m_flags = 0;
    WmoDef* next = nullptr;
};
namespace CMap {
    struct { List<Doodad> m_fulllist; } doodadDefHash;
    struct { List<WmoDef> m_fulllist; } mapObjDefHash;
}
constexpr int MAPOBJ_FLAG_DISABLED = 2;
constexpr int MAPOBJ_FLAG_INTERIOR = 4;
constexpr int kMaxDepth = 8;
constexpr int kMaxChildren = 64;
CM2Scene* Scene() { return &scene; }
const M2Data* DataOf(const CM2Model* model) { return model->data; }
bool WorldOf(const CM2Model* m, float* matrix, bool& animated, int) {
    std::copy_n(Floats(m->m_worldTransform), 16, matrix);
    animated = false;
    return true;
}
bool AttachmentPosition(const M2Data*, int, int, const float offset[3], float out[3]) {
    std::copy_n(offset, 3, out);
    return true;
}
template<class T> bool FirstTrackValue(const M2Data*, const T& track, T& value) {
    value = track;
    return true;
}
std::filesystem::path ClientDirectory() { return {}; }
sl::Settings settings;
sl::LightTables tables;
bool staticLampsEnabled = true;
bool lampCatalogTried = true;
float staticLampRange = 100;
sl::StaticLampCatalog lampCatalog;
sl::StaticWmoLampCatalog meshLampCatalog;
namespace Profiler {
    enum { SurfaceLightScan };
    int WorkZone(int zone) { return zone; }
    struct Scope { explicit Scope(int) {} };
}
#include "scan.inc"

// The compared snapshot is the ordered input to the unchanged per-frame lighting selection.
void PrintSources() {
    std::printf("visible=%u attached=%u table=%u native=%u static=%u mesh=%u\n", stats.modelsInRange,
                stats.attached, stats.tableLights, stats.modelLights, stats.staticLamps, stats.meshLamps);
    for (const auto& s : sources) {
        if (s.model && !liveModels.count(s.model)) continue;
        std::printf("%s %d %d %d %d %.8g %.8g %.8g %.8g %.8g %.8g %.8g\n",
                    s.model ? s.model->m_shared->m_fileName : "WMO", s.table, s.staticLamp, s.follow != nullptr,
                    s.indoorOverride, s.worldPos[0], s.worldPos[1], s.worldPos[2], s.radius,
                    s.color[0], s.color[1], s.color[2]);
    }
}

int main() {
    settings.range = 70;
    const sl::StaticLamp lamp{{0, 0, 1}, {1, .8f, .4f}, 7, 2};
    lampCatalog.emplace("lamp.m2", lamp);
    lampCatalog.emplace("loading.m2", lamp);
    lampCatalog.emplace("authored.m2", lamp);
    meshLampCatalog["fixture.wmo"].push_back({0, lamp});
    CM2Shared shared[5];
    const char* names[] = {"irrelevant.m2", "LAMP.MDX", "loading.m2", "native.m2", "authored.m2"};
    for (unsigned i = 0; i < std::size(shared); ++i) std::strcpy(shared[i].m_fileName, names[i]);
    M2Data plain, native;
    native.m_lights.m_count = 1;
    std::vector<CM2Model> models(24006);
    std::vector<Doodad> placements(24000);
    for (size_t i = 0; i < models.size(); ++i) {
        models[i].m_shared = &shared[0];
        models[i].data = &plain;
        models[i].m_worldTransform.values[12] = 40.0f + (i % 50);
        if (i + 1 < models.size()) models[i].m_sceneNext = &models[i + 1];
    }
    scene.m_modelList = models.data();
    models[0].m_shared = &shared[1];
    models[1].m_shared = &shared[2];
    models[1].data = nullptr;
    models[2].m_shared = &shared[3];
    models[2].data = &native;
    models[3].m_shared = &shared[4];
    models[4].m_shared = &shared[3];
    models[4].data = &native;
    models[4].m_attachParent = &models[5];
    models[5].m_attachList = &models[4];
    for (size_t i = 0; i < placements.size(); ++i) {
        placements[i].m_model = &models[i];
        placements[i].m_mat = models[i].m_worldTransform;
        if (i + 1 < placements.size()) placements[i].next = &placements[i + 1];
    }
    CMapBaseObj group{CMapBaseObj::Type_MapObjDefGroup};
    Link link{&group};
    placements[0].m_parentLinkList.head = &link;
    placements[0].m_flags = MAPOBJ_FLAG_INTERIOR;
    placements[0].m_mat.values[12] = 17;
    placements[1].m_flags = MAPOBJ_FLAG_DISABLED;
    CMap::doodadDefHash.m_fulllist.head = placements.data();
    Wmo wmo;
    WmoDef def;
    def.m_owner = &wmo;
    def.m_mat.values[12] = 19;
    CMap::mapObjDefHash.m_fulllist.head = &def;

    const float eye[3] = {}, forward[3] = {1, 0, 0};
    ScanModels(eye, forward);
    assert(stats.modelLights == 2 && stats.meshLamps == 1 && selectedStaticLamps == 3);
    PrintSources();
    ModelDestroyed(&models[0]);
    assert(!liveModels.count(&models[0]) && liveModels.count(&models[2]));
    PrintSources();

    // Table reload invalidation, priority changes, model readiness, and equal-distance ordering.
    Builder props(13);
    props.rows.push_back({1, props.Str("Torch"), 0, 0xffa050, Builder::F(1.4f), Builder::F(12),
                          Builder::F(-1), Builder::F(20), Builder::F(35), UINT32_MAX,
                          Builder::F(-1), Builder::F(-1), 1});
    Builder assignments(13);
    assignments.rows.push_back({1, assignments.Str("authored.m2"), 0, 0, 0, 0, 0, 0, 0, 0,
                                Builder::F(-1), 1, 0});
    const auto propBytes = props.Bytes();
    const auto assignBytes = assignments.Bytes();
    sl::Wdbc propTable, assignTable;
    std::string error;
    assert(sl::ParseWdbc(propBytes.data(), propBytes.size(), propTable, error, {1}));
    assert(sl::ParseWdbc(assignBytes.data(), assignBytes.size(), assignTable, error, {1, 4}));
    tables.LoadProperties(propTable);
    tables.LoadAssignments(assignTable);
    models[1].data = &plain;
    placements[1].m_flags = 0;
    models[3].m_worldTransform.values[12] = models[2].m_worldTransform.values[12];
    ScanModels(eye, forward);
    assert(stats.tableLights == 1);
    PrintSources();
    staticLampsEnabled = false;
    ScanModels(eye, forward);
    assert(stats.tableLights == 1 && selectedStaticLamps == 0);
    PrintSources();

    // The 400-yard pass admits more models and ranks past the selection cap.
    staticLampsEnabled = true;
    staticLampRange = 400;
    for (unsigned i = 0; i < 210; ++i) {
        auto& model = models[i + 20];
        model.m_shared = &shared[1];
        model.m_worldTransform.values[12] = 75 + i;
        placements[i + 20].m_mat = model.m_worldTransform;
    }
    ScanModels(eye, forward);
    assert(selectedStaticLamps == sl::kMaxStaticLamps && stats.lampsBudgetDropped > 0);
    PrintSources();
    for (unsigned i = 0; i < 210; ++i) models[i + 20].m_shared = &shared[0];

    // The timing sample uses unique light distances on the normal fast path.
    staticLampRange = 100;
    models[3].m_worldTransform.values[12] = 55;
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 60; ++i) ScanModels(eye, forward);
    const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::fprintf(stderr, "24,006 scene models and 24,000 placements: %.3f ms per scan\n", elapsed / 60);
}
