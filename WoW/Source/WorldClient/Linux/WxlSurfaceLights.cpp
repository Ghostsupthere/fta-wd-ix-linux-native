#if defined(WOWDECOMP_LINUX)
#define WOWDECOMP_GX_DXVK
#include <d3d9.h>
#include "WxlSurfaceLights.h"
#include "WxlModernVisuals.h"
#include "Engine/Source/ModernAssets/WxlVisuals/engine.h"
#include "Engine/Source/ModernAssets/WxlVisuals/player_lantern.h"
#include "Engine/Source/ModernAssets/WxlVisuals/corpse_light_list.h"
#include "Engine/Source/ModernAssets/WxlVisuals/spell_light_list.h"
#include "Engine/Source/ModernAssets/WxlVisuals/terrain_occlusion.h"
#include "WxlVisualEnvironment.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxDeviceD3d9Ex/CGxDeviceD3d9Ex.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Model2/M2Data.h"
#include "Engine/Source/Model2/M2Light.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Model2/M2ModelLight.h"
#include "Engine/Source/Model2/M2Scene.h"
#include "Engine/Source/Model2/M2Shared.h"
#include "Engine/Source/ModernAssets/NativeDiagnostic.h"
#include "Engine/Source/ModernAssets/NativeShaderReplace.h"
#include "Engine/Source/ModernAssets/NativeVisualHooks.h"
#include "Engine/Source/ModernAssets/WxlGrass/EnvironmentOptions.hpp"
#include "Engine/Source/ModernAssets/WxlSurfaceLights/StaticLampCatalog.hpp"
#include "Engine/Source/ModernAssets/WxlSurfaceLights/CharacterRim.hpp"
#include "Engine/Source/ModernAssets/WxlSurfaceLights/DoodadLightTables.hpp"
#include "Engine/Source/ModernAssets/WxlSurfaceLights/LightPool.hpp"
#include "Engine/Source/ModernAssets/WxlSurfaceLights/LightShaderPack.hpp"
#include "Engine/Source/ModernAssets/WxlSurfaceLights/LightTextureLayout.hpp"
#include "WoW/Source/WorldClient/Map.h"
#include "WoW/Source/WorldClient/MapDoodadDef.h"
#include "WoW/Source/WorldClient/DayNight.h"
#include "WoW/Source/WorldClient/DayNight__DNInfo.h"
#include "WoW/Source/WorldClient/MapObj.h"
#include "WoW/Source/WorldClient/MapObjGroup.h"
#include "WoW/Source/WorldClient/MapObjDef.h"
#include "WoW/Source/WorldClient/World.h"
#include "WoW/Source/DB/Rec/SpellVisualKitRec.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "WoW/Source/Object/ObjectClient/Effect_C.h"
#include "WoW/Source/WorldClient/Linux/WxlWmoMaterials.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sl = wxl::surfacelights;
namespace tex = wxl::surfacelights::texture;
namespace env = wxl::grass::environment;

namespace {
// ---- shader replacement --------------------------------------------------------------------------
sl::LightShaderPack pack, weatherPack;
bool weatherPackLoaded = false;
bool wmoWeatherSelected = false;
bool wmoWeatherExterior = false;
bool wmoWeatherVisible = false;
constexpr UINT kWmoCloudTransform = 246, kWmoCloud = 248, kWetVertex = 249;
float wmoCloud[4] = {};
bool packTried = false, packLoaded = false, profileOn = false;
// The WMO layered-material module finds its shaders by hashing the engine's CURRENT bytecode, so a
// light-patched WMO vertex shader only keeps its layered materials if the layered pack carries a variant
// for the light-patched bytecode (tools/build-wmo-shader-pack.py adds them when light_shaders.pack exists
// beside it). WMO lights are on when there is no layered pack, or it has those variants; otherwise off
// unless WXL_SURFACE_LIGHTS_WMO=1 says otherwise.
bool wmoLights = true, wmoPackPresent = false, wmoPackCombined = false;
unsigned replaced[4] = {};
std::string packProblem;

std::filesystem::path ClientDirectory() {
    std::error_code error;
    const auto executable = std::filesystem::canonical("/proc/self/exe", error);
    return error ? std::filesystem::current_path() : executable.parent_path();
}

void EnsurePack() {
    if (packTried) return;
    packTried = true;
    profileOn = WxlProfileEnabled("WXL_SURFACE_LIGHTS", true);
    if (!profileOn) return;
    std::error_code exists;
    wmoPackPresent = std::filesystem::exists(ClientDirectory() / "Utils/WarcraftXL/visuals/wmo_shaders.pack", exists);
    // (the check below needs the pack loaded first, so the default is settled after the load)
    std::string why;
    packLoaded = pack.Load((ClientDirectory() / "Utils/WarcraftXL/visuals/light_shaders.pack").string(), why);
    if (!packLoaded) {
        packProblem = why;
        std::fprintf(stderr, "WarcraftXL: surface lights pack unavailable (%s); stock lighting. Run tools/build-light-shader-pack.py\n", why.c_str());
        return;
    }
    weatherPackLoaded = weatherPack.Load((ClientDirectory() / "Utils/WarcraftXL/visuals/light_weather.pack").string(), why);
    if (weatherPackLoaded)
        std::fprintf(stderr, "WarcraftXL: supplemental weather material shader pack active\n");
    if (wmoPackPresent)
        for (const sl::LightPackEntry& e : pack.Entries())
            if (e.group == sl::kGroupWmo && e.kind == 0) {
                wmoPackCombined = WxlWmoPackHasVertexVariant(e.bytecode.data(), e.bytecode.size() * 4);
                break;
            }
    wmoLights = WxlProfileEnabled("WXL_SURFACE_LIGHTS_WMO", !wmoPackPresent || wmoPackCombined);
    std::fprintf(stderr, "WarcraftXL: surface lights enabled in the profile (%zu patched shader(s): terrain %zu, M2 %zu, WMO %zu%s)\n",
                 pack.Count(), pack.Count(sl::kGroupTerrain), pack.Count(sl::kGroupModel), pack.Count(sl::kGroupWmo),
                 wmoLights ? (wmoPackCombined ? "; WMO lights on, layered materials kept" : "")
                           : "; WMO lights off: the WMO layered-material pack has no light variants (rerun tools/build-light-shader-pack.py then tools/build-wmo-shader-pack.py), or set WXL_SURFACE_LIGHTS_WMO=1 to override");
}

bool Replace(const uint8_t* code, size_t size, const uint32_t** out, size_t* outBytes) {
    EnsurePack();
    if (!packLoaded || size < 8) return false;
    uint32_t version;
    std::memcpy(&version, code, 4);
    const uint8_t kind = (version & 0xFFFF0000u) == 0xFFFE0000u ? 0 : ((version & 0xFFFF0000u) == 0xFFFF0000u ? 1 : 2);
    if (kind > 1) return false;
    const sl::LightPackEntry* entry = pack.Find(kind, code, size);
    if (!entry || (entry->group == sl::kGroupWmo && !wmoLights)) return false;
    if (weatherPackLoaded) {
        if (const auto* weather = weatherPack.Find(kind, code, size); weather && weather->group == entry->group) {
            const bool wmo = entry->group == sl::kGroupWmo;
            // Match the new bytecode per permutation. Older layered packs keep
            // using the existing light-only WMO shaders.
            if (!wmo || !wmoPackPresent || WxlWmoPackHasVertexVariant(weather->bytecode.data(), weather->bytecode.size() * 4)) {
                entry = weather;
                wmoWeatherSelected |= wmo;
            }
        }
    }
    ++replaced[entry->group];
    *out = entry->bytecode.data();
    *outBytes = entry->bytecode.size() * 4;
    return true;
}

const bool registered = (NativeShaderReplace::Add(&Replace), true);

// The terrain pair works only together: a patched pixel shader reads a vertex output only a patched
// vertex shader writes, and the stock point lights are switched through VS c250. Both are checked by the
// pack builder; here at least one terrain shader of each kind must actually have been replaced.
bool ShadersActive() { return packLoaded && replaced[sl::kGroupTerrain] > 0; }

// ---- state ----------------------------------------------------------------------------------------
sl::Settings settings;
sl::LightTables tables;
bool tablesLoaded = false;
bool staticLampsEnabled = false, lampCatalogTried = false;
float staticLampStrength = 1, staticLampRange = 100;
sl::StaticLampCatalog lampCatalog;
sl::StaticWmoLampCatalog meshLampCatalog;
constexpr unsigned kLampDebugLimit = 24;
constexpr unsigned kMaxSceneModels = 100000;
constexpr unsigned kMaxMapPlacements = 100000;
struct LampDebug {
    std::string path;
    const char* stage;
    float pos[3], color[3], radius, intensity, distance;
    unsigned cells = 0;
    bool indoor = false;
};
std::vector<LampDebug> lampDebug;
std::deque<std::string> lampTransitions;
unsigned selectedStaticLamps = 0;
std::string tableStatus = "not loaded yet";

struct Source {
    // A model's own light record (CM2Light kept current by the client), or a table light.
    const CM2Model* ownModel = nullptr;
    uint32_t ownIndex = 0;
    float worldPos[3] = {};
    float fileColor[3] = {};
    bool haveFileColor = false;
    bool table = false;
    float color[3] = {};
    float radius = -1.0f, falloff = -1.0f;
    float spotDir[3] = {0.0f, 0.0f, -1.0f};
    float cosOuter = -2.0f, spotScale = 1.0f;
    int flickerMode = sl::kFlickerDefault;
    float flickerSpeed = -1.0f, flickerAmount = -1.0f;
    float scale = 1.0f;
    // Attached models (a torch in a hand): the model and the light in its own space, so the light follows it.
    const CM2Model* follow = nullptr;
    float localPos[3] = {}, localDir[3] = {0.0f, 0.0f, -1.0f};
    const CM2Model* model = nullptr;
    bool bake = false;
    bool staticLamp = false;
    int8_t indoorOverride = -1;
    int lampDebugIndex = -1;
};
std::vector<Source> sources;
std::unordered_set<const CM2Model*> liveModels;   // models seen by the last scan, minus those destroyed since

sl::GridBuilder gridBuilder;
sl::Grid grid;
sl::GridStats gridStats;
sl::OcclusionCache occlusion;
sl::ChosenPool pool;
double lightTime = 0.0;
float scanTimer = 0.25f;
uint32_t scanGeneration = 0;
std::chrono::steady_clock::time_point lastUpdate;
bool haveLastUpdate = false;

struct Stats {
    unsigned models = 0, modelsInRange = 0, modelLights = 0, fromClient = 0, fromWorld = 0, fileColor = 0, merged = 0,
             inRange = 0, active = 0, tableLights = 0, suppressedModels = 0, attachFallbacks = 0, attached = 0;
    unsigned staticLamps = 0, staticDuplicates = 0, staticWmoMatches = 0, wmoPlacements = 0, meshLamps = 0;
    unsigned lampsBudgetDropped = 0, lampsGroupPending = 0;
    double lampScanMs = 0;
    unsigned bakeWanted = 0, bakeDone = 0, bakeRays = 0, bakeCached = 0, indoor = 0, indoorTests = 0;
};
Stats stats;

void ModelDestroyed(const CM2Model* model) {
    if (!liveModels.empty()) liveModels.erase(model);
}

// ---- engine data ---------------------------------------------------------------------------------
static_assert(sizeof(C44Matrix) == 64, "C44Matrix is 16 floats");
const float* Floats(const C44Matrix& m) { return &m.a0; }

CM2Scene* Scene() { return World::s_m2Scene; }

bool Finite3(const float* v) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

// Only a model whose load has finished: m_shared->m_flags bit 0 is set at the end of CM2Shared::FinishLoading.
const M2Data* DataOf(const CM2Model* model) {
    return model && model->m_shared && (model->m_shared->m_flags & 1) ? model->m_shared->m_data : nullptr;
}

// The first value of a track's first sequence whose keys are resident. Per-sequence tracks only have
// resolved key arrays for embedded sequences (flag 0x20); the others point into .anim data that may not be
// loaded, so they must never be read (modern models keep sequence 0 external). Global-sequence tracks
// (loop index not 0xFFFF) always resolve.
template <class T>
bool FirstTrackValue(const M2Data* data, const M2Track<T>& track, T& out) {
    if (!data) return false;
    const uint32_t count = track.m_sequenceKeys.m_count;
    if (!count || count > 4096) return false;
    const bool perSequence = track.m_loopIndex == 0xFFFF;
    for (uint32_t i = 0; i < count && i < 64; ++i) {
        if (perSequence && (i >= data->m_sequences.m_count || !(data->m_sequences[i].m_flags & 0x20))) continue;
        const M2Array<T>& keys = track.m_sequenceKeys[i];
        if (!keys.m_count) continue;
        out = keys[0];
        return true;
    }
    return false;
}

bool AttachmentPosition(const M2Data* data, uint32_t type, int32_t index, const float offset[3], float out[3]) {
    out[0] = offset[0]; out[1] = offset[1]; out[2] = offset[2];
    if (type == sl::kOrigin) return true;
    if (!data) return false;
    const C3Vector* at = nullptr;
    if (type == sl::kAttachmentPoint) {
        const uint32_t count = data->m_attachments.m_count;
        for (uint32_t k = 0; k < count && k < 256; ++k) {
            const M2Attachment& a = data->m_attachments[k];
            if (a.m_id == static_cast<uint32_t>(index)) { at = &a.m_position; break; }
        }
    } else if (type == sl::kBone) {
        if (index >= 0 && static_cast<uint32_t>(index) < data->m_bones.m_count) at = &data->m_bones[index].m_pivot;
    } else if (type == sl::kParticleEmitter) {
        if (index >= 0 && static_cast<uint32_t>(index) < data->m_particles.m_count) at = &data->m_particles[index].m_position;
    }
    if (!at) return false;
    out[0] += at->x; out[1] += at->y; out[2] += at->z;
    return true;
}

constexpr int kMaxDepth = 4;
constexpr unsigned kMaxChildren = 64;

bool ParentWorldAtRest(const CM2Model* model, float out[16], int depth);

// A model's world matrix. Roots: their placement. Attached models: while they animated in the last couple
// of frames, their own model -> view root taken back through the scene view (the engine's own derivation,
// CM2Model::GetBounds); otherwise their parent's matrix moved to the attachment they hang on, at rest.
bool WorldOf(const CM2Model* model, float out[16], bool& animated, int depth) {
    if (!model) return false;
    if (!model->m_attachParent) {
        animated = true;
        std::memcpy(out, Floats(model->m_worldTransform), 64);
        return true;
    }
    if (depth > kMaxDepth) return false;
    const CM2Scene* scene = model->m_scene;
    if (scene && scene->m_frameStamp - model->m_frameStamp <= 2u) {
        const C44Matrix world = model->m_matrixF4 * scene->m_viewInv;
        std::memcpy(out, Floats(world), 64);
        animated = true;
        return true;
    }
    animated = false;
    return ParentWorldAtRest(model, out, depth);
}

bool ParentWorldAtRest(const CM2Model* model, float out[16], int depth) {
    const CM2Model* parent = model->m_attachParent;
    bool parentAnimated = false;
    if (!parent || !WorldOf(parent, out, parentAnimated, depth + 1)) return false;
    const M2Data* data = DataOf(parent);
    const uint32_t slot = model->m_attachmentIndex;
    if (data && slot < data->m_attachments.m_count) {
        float moved[3];
        const C3Vector& at = data->m_attachments[slot].m_position;
        const float local[3] = {at.x, at.y, at.z};
        sl::MulPoint(local, out, moved);
        out[12] = moved[0]; out[13] = moved[1]; out[14] = moved[2];
    }
    return true;
}

struct SceneModel {
    const CM2Model* model = nullptr;
    const M2Data* data = nullptr;
    const char* path = "";
    float world[16] = {};
    float distance = 0.0f;
    const CM2Model* parent = nullptr;
    bool animated = true;
    bool wmoDoodad = false;
    int8_t indoorOverride = -1;
    bool disabled = false;
};

void Describe(const CM2Model* model, SceneModel& out) {
    out.data = DataOf(model);
    if (model->m_shared && model->m_shared->m_fileName[0]) out.path = model->m_shared->m_fileName;
}

void AddChildren(const CM2Model* parent, const float center[3], float range, std::vector<SceneModel>& out, int depth, unsigned& attached) {
    if (depth > kMaxDepth) return;
    unsigned n = 0;
    for (const CM2Model* c = parent->m_attachList; c && n < kMaxChildren; c = c->m_attachNext, ++n) {
        SceneModel sm;
        sm.model = c;
        sm.parent = parent;
        if (WorldOf(c, sm.world, sm.animated, depth)) {
            const float dx = sm.world[12] - center[0], dy = sm.world[13] - center[1], dz = sm.world[14] - center[2];
            sm.distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (sm.distance <= range) { Describe(c, sm); out.push_back(sm); ++attached; }
        }
        AddChildren(c, center, range, out, depth + 1, attached);
    }
}

// Scene models are independent of map placement ownership. The map also
// maintains the stable world transform for terrain and WMO doodads.
unsigned SceneModels(const float center[3], float range, std::vector<SceneModel>& out, unsigned& attached,
                     std::unordered_set<const CM2Model*>* resident = nullptr) {
    constexpr float kChildReach = 30.0f;
    out.clear();
    attached = 0;
    CM2Scene* scene = Scene();
    if (!scene) return 0;
    unsigned total = 0;
    for (const CM2Model* m = scene->m_modelList; m && total < kMaxSceneModels; m = m->m_sceneNext, ++total) {
        if (resident) resident->insert(m);
        if (m->m_attachParent) continue;
        SceneModel sm;
        sm.model = m;
        std::memcpy(sm.world, Floats(m->m_worldTransform), 64);
        const float dx = sm.world[12] - center[0], dy = sm.world[13] - center[1], dz = sm.world[14] - center[2];
        sm.distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (sm.distance <= range) { Describe(m, sm); out.push_back(sm); }
        if (sm.distance <= range + kChildReach) AddChildren(m, center, range, out, 1, attached);
    }
    if (resident)
        for (const SceneModel& sm : out) resident->insert(sm.model);
    std::sort(out.begin(), out.end(), [](const SceneModel& a, const SceneModel& b) { return a.distance < b.distance; });
    return total;
}

unsigned MapDoodadModels(const float center[3], float range, std::vector<SceneModel>& out) {
    out.clear();
    unsigned visited = 0;
    auto& placements = CMap::doodadDefHash.m_fulllist;
    for (auto* doodad = placements.Head(); doodad && visited < kMaxMapPlacements;
         doodad = placements.Next(doodad), ++visited) {
        const CM2Model* model = doodad->m_model;
        if (!model || model->m_scene != Scene()) continue;
        const float* world = Floats(doodad->m_mat);
        const float dx = world[12] - center[0], dy = world[13] - center[1], dz = world[14] - center[2];
        const float d2 = dx*dx + dy*dy + dz*dz;
        if (!(d2 <= range * range)) continue;
        SceneModel info;
        info.model = model;
        info.parent = model->m_attachParent;
        info.disabled = (doodad->m_flags & MAPOBJ_FLAG_DISABLED) != 0;
        info.distance = std::sqrt(d2);
        std::copy_n(world, 16, info.world);
        for (auto* link = doodad->m_parentLinkList.Head(); link; link = doodad->m_parentLinkList.Next(link)) {
            if (link->m_ref && (link->m_ref->m_type & CMapBaseObj::Type_MapObjDefGroup)) {
                info.wmoDoodad = true;
                break;
            }
        }
        if (info.wmoDoodad) info.indoorOverride = (doodad->m_flags & MAPOBJ_FLAG_INTERIOR) != 0;
        Describe(model, info);
        out.push_back(info);
    }
    return visited;
}

// ---- the table rows per model file ----------------------------------------------------------------
struct RowCache { std::string path; uint32_t generation = 0; std::vector<const sl::Assignment*> rows; };
std::unordered_map<const char*, RowCache> rowCache;

const std::vector<const sl::Assignment*>& RowsFor(const char* path) {
    static const std::vector<const sl::Assignment*> empty;
    if (!path || !*path) return empty;
    if (rowCache.size() > 8192) rowCache.clear();
    RowCache& rc = rowCache[path];
    if (rc.generation != tables.Generation() || rc.path != path) {
        rc.path = path;
        rc.generation = tables.Generation();
        rc.rows = tables.ForModel(sl::NormalizePath(rc.path));
    }
    return rc.rows;
}

void AddTableLight(const sl::Assignment& a, const SceneModel& m, Stats& st) {
    const sl::Properties* p = tables.FindProperties(a.lightId);
    if (!p) return;
    float local[3];
    if (!AttachmentPosition(m.data, a.attachType, a.attachIndex, a.offset, local)) ++st.attachFallbacks;
    const sl::TableLight t = sl::MakeTableLight(a, *p, m.world);
    Source s;
    s.table = true;
    s.follow = m.parent ? m.model : nullptr;
    s.model = m.model;
    s.bake = t.bake;
    std::memcpy(s.localPos, local, sizeof(local));
    std::memcpy(s.localDir, a.direction, sizeof(s.localDir));
    s.scale = sl::MatrixScale(m.world);
    sl::MulPoint(local, m.world, s.worldPos);
    std::memcpy(s.color, t.color, sizeof(s.color));
    s.radius = t.radius; s.falloff = t.falloff;
    std::memcpy(s.spotDir, t.spotDir, sizeof(s.spotDir));
    s.cosOuter = t.cosOuter; s.spotScale = t.spotScale;
    s.flickerMode = t.flickerMode; s.flickerSpeed = t.flickerSpeed; s.flickerAmount = t.flickerAmount;
    sources.push_back(s);
    ++st.tableLights;
}

void LoadLampCatalog() {
    if (lampCatalogTried) return;
    lampCatalogTried = true;
    std::ifstream file(ClientDirectory() / "Utils/WarcraftXL/visuals/lamp_models.csv");
    if (file) lampCatalog = sl::ReadStaticLampCatalog(file, &meshLampCatalog);
    std::fprintf(stderr, "WarcraftXL: static lamp catalog: %zu models\n", lampCatalog.size());
}

void ScanStaticLamps(const float center[3], const float forward[3], const std::vector<SceneModel>& sceneModels,
                     bool haveTable, Stats& st) {
    const auto started = std::chrono::steady_clock::now();
    static std::vector<SceneModel> mapModels;
    MapDoodadModels(center, staticLampRange + 20, mapModels);
    st.wmoPlacements = std::count_if(mapModels.begin(), mapModels.end(), [](const auto& m) { return m.wmoDoodad; });

    struct Match {
        const CM2Model* model;
        LampDebug debug;
        sl::StaticLampRank rank;
        int8_t indoorOverride;
    };
    static std::vector<Match> matches;
    matches.clear();
    sl::VisitStaticPlacements(mapModels, sceneModels, [&](const SceneModel& m) {
        if (!m.path || !*m.path) return;
        auto path = sl::NormalizeLampPath(m.path);
        const auto catalog = lampCatalog.find(path);
        if (catalog == lampCatalog.end()) return;
        if (m.distance > staticLampRange + 20) return;
        ++st.staticLamps;
        st.staticWmoMatches += m.wmoDoodad;
        const auto* rows = haveTable ? &RowsFor(m.path) : nullptr;
        const auto& lamp = catalog->second;
        const auto placement = sl::PlaceStaticLamp(lamp, m.world);
        const float dx = placement.pos[0] - center[0], dy = placement.pos[1] - center[1], dz = placement.pos[2] - center[2];
        const float distance = std::sqrt(dx*dx + dy*dy + dz*dz);
        const float score = sl::StaticLampScore(placement, center, forward);
        const char* rejected = m.disabled ? "disabled doodad" : !m.data ? "model loading" :
            rows && !rows->empty() ? "authored assignment" :
            m.data->m_lights.m_count ? "native model lights" : m.parent ? "attached model" :
            !(score > 0) ? "invalid light" : distance >= staticLampRange ? "lamp distance limit" : nullptr;
        LampDebug debug{std::move(path), rejected, {}, {}, placement.radius, placement.strength, distance};
        std::copy_n(placement.pos, 3, debug.pos);
        std::copy_n(lamp.color, 3, debug.color);
        matches.push_back({m.model, std::move(debug), {score, distance, !rejected}, m.indoorOverride});
    });
    if (!meshLampCatalog.empty()) {
        auto& owners = CMap::mapObjDefHash.m_fulllist;
        unsigned visited = 0;
        for (auto* def = owners.Head(); def && visited < kMaxMapPlacements; def = owners.Next(def), ++visited) {
            const auto* wmo = def->m_owner;
            if (!wmo || !wmo->m_isGroupLoaded || (def->m_flags & MAPOBJ_FLAG_DISABLED)) continue;
            const auto path = sl::NormalizePath(wmo->m_wmoName);
            const auto catalog = meshLampCatalog.find(path);
            if (catalog == meshLampCatalog.end()) continue;
            for (const auto& row : catalog->second) {
                if (row.group >= std::size(wmo->m_mapObjGroupArray) ||
                    static_cast<int>(row.group) >= wmo->m_mapObjGroupCount) continue;
                const auto* group = wmo->m_mapObjGroupArray[row.group];
                const auto placement = sl::PlaceStaticLamp(row.lamp, Floats(def->m_mat));
                const float dx = placement.pos[0] - center[0], dy = placement.pos[1] - center[1], dz = placement.pos[2] - center[2];
                const float d2 = dx*dx + dy*dy + dz*dz;
                const float scanRange = staticLampRange + 20;
                if (!(d2 <= scanRange * scanRange)) continue;
                const float score = sl::StaticLampScore(placement, center, forward);
                if (!(score > 0)) continue;
                const float distance = std::sqrt(d2);
                const int8_t indoor = (def->m_owner->GetGroupFlags(static_cast<int>(row.group)) & 0x48u) ? 0 : 1;
                const char* rejected = distance >= staticLampRange ? "lamp distance limit" :
                    !group || !(group->m_unkLoadedFlag & 1) ? "WMO group loading" : nullptr;
                st.lampsGroupPending += rejected && distance < staticLampRange;
                LampDebug debug{path + "#" + std::to_string(row.group), rejected, {}, {},
                                placement.radius, placement.strength, distance};
                std::copy_n(placement.pos, 3, debug.pos);
                std::copy_n(row.lamp.color, 3, debug.color);
                matches.push_back({nullptr, std::move(debug), {score, distance, !rejected}, indoor});
                ++st.meshLamps;
            }
        }
    }
    const size_t budget = sl::StaticLampBudget(staticLampRange);
    const size_t eligible = std::count_if(matches.begin(), matches.end(), [](const Match& m) { return m.rank.eligible; });
    const unsigned previousSelected = selectedStaticLamps;
    selectedStaticLamps = std::min(eligible, budget);
    st.lampsBudgetDropped = eligible - selectedStaticLamps;
    const size_t shown = std::min(matches.size(), budget + kLampDebugLimit);
    std::partial_sort(matches.begin(), matches.begin() + shown, matches.end(), [](const Match& a, const Match& b) {
        return a.rank < b.rank;
    });
    for (size_t i = 0; i < matches.size(); ++i)
        if (i >= selectedStaticLamps && !matches[i].debug.stage) matches[i].debug.stage = "lamp selection limit";

    const auto sameLamp = [](const LampDebug& a, const LampDebug& b) {
        return a.path == b.path && std::equal(a.pos, a.pos + 3, b.pos);
    };
    const auto event = [](const char* verb, const LampDebug& lamp, float distance, const char* reason) {
        char fields[160];
        std::snprintf(fields, sizeof(fields), " %.1f yd @ (%.2f, %.2f, %.2f) %s", distance,
                      lamp.pos[0], lamp.pos[1], lamp.pos[2], reason);
        lampTransitions.emplace_back(std::string(verb) + " " + lamp.path + fields);
        if (lampTransitions.size() > sl::kMaxStaticLamps * 2) lampTransitions.pop_front();
    };
    std::vector<LampDebug> previous;
    previous.swap(lampDebug);
    for (unsigned i = 0; i < previousSelected && i < previous.size(); ++i) {
        const auto& old = previous[i];
        const auto found = std::find_if(matches.begin(), matches.end(), [&](const Match& m) { return sameLamp(old, m.debug); });
        if (found != matches.end() && !found->debug.stage) continue;
        const float dx = old.pos[0] - center[0], dy = old.pos[1] - center[1], dz = old.pos[2] - center[2];
        const float distance = std::sqrt(dx*dx + dy*dy + dz*dz);
        event("Left", old, distance, found != matches.end() ? found->debug.stage :
              distance >= staticLampRange ? "lamp distance limit" : "placement not resident");
    }
    for (size_t i = 0; i < shown; ++i) {
        auto& match = matches[i];
        auto& debug = match.debug;
        if (!debug.stage) {
            const auto old = std::find_if(previous.begin(), previous.end(), [&](const auto& m) { return sameLamp(m, debug); });
            if (old == previous.end() || static_cast<size_t>(old - previous.begin()) >= previousSelected) {
                event("Entered", debug, debug.distance, old == previous.end() ? "newly eligible or resident" : old->stage);
            }
            debug.stage = "selected";
            Source s;
            s.table = true;
            s.staticLamp = true;
            s.indoorOverride = match.indoorOverride;
            s.model = match.model;
            s.radius = debug.radius;
            s.falloff = 1;
            s.flickerMode = sl::kFlickerSmooth;
            s.lampDebugIndex = static_cast<int>(i);
            std::copy_n(debug.pos, 3, s.worldPos);
            for (unsigned channel = 0; channel < 3; ++channel)
                s.color[channel] = debug.color[channel] * debug.intensity;
            sources.push_back(s);
            if (s.model) liveModels.insert(s.model);
        }
        lampDebug.push_back(std::move(debug));
    }
    st.lampScanMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
}

void ScanModels(const float center[3], const float forward[3]) {
    if (staticLampsEnabled) LoadLampCatalog();
    sources.clear();
    liveModels.clear();
    if (!staticLampsEnabled) {
        lampDebug.clear();
        lampTransitions.clear();
        selectedStaticLamps = 0;
    }
    Stats st = stats;
    st.tableLights = st.suppressedModels = st.attachFallbacks = st.modelLights = 0;
    st.staticLamps = st.staticWmoMatches = st.wmoPlacements = st.meshLamps = st.lampsBudgetDropped = st.lampsGroupPending = 0;
    static std::vector<SceneModel> models;
    unsigned attached = 0;
    const float scanRange = staticLampsEnabled ? std::max(settings.range, staticLampRange + 20) : settings.range;
    st.models = SceneModels(center, scanRange, models, attached, &liveModels);
    st.modelsInRange = static_cast<unsigned>(models.size());
    st.attached = attached;
    const bool haveTable = !tables.AllAssignments().empty();
    if (staticLampsEnabled && (!lampCatalog.empty() || !meshLampCatalog.empty())) ScanStaticLamps(center, forward, models, haveTable, st);
    for (const SceneModel& m : models) {
        if (!m.data || m.distance > settings.range) continue;
        bool suppress = false;
        const auto* rows = haveTable ? &RowsFor(m.path) : nullptr;
        if (rows)
            for (const sl::Assignment* a : *rows) {
                if (a->flags & sl::kSuppressModelLights) suppress = true;
                if (a->lightId) AddTableLight(*a, m, st);
            }
        const uint32_t count = m.data->m_lights.m_count;
        if (suppress) { ++st.suppressedModels; continue; }
        // The model's own lights: point lights only.
        if (!count || count > 64) continue;
        for (uint32_t l = 0; l < count; ++l) {
            const M2Light& file = m.data->m_lights[l];
            if (file.m_type != 1) continue;
            ++st.modelLights;
            Source s;
            s.ownModel = m.model->m_lights ? m.model : nullptr;
            s.ownIndex = l;
            s.follow = m.parent ? m.model : nullptr;
            s.model = m.model;
            const float local[3] = {file.m_position.x, file.m_position.y, file.m_position.z};
            std::memcpy(s.localPos, local, sizeof(local));
            sl::MulPoint(local, m.world, s.worldPos);
            s.scale = sl::MatrixScale(m.world);
            C3Vector color;
            float intensity = 1.0f;
            if (FirstTrackValue(m.data, file.m_diffuseColor, color)) {
                FirstTrackValue(m.data, file.m_diffuseIntensity, intensity);
                s.fileColor[0] = color.x * intensity; s.fileColor[1] = color.y * intensity; s.fileColor[2] = color.z * intensity;
                s.haveFileColor = true;
            }
            sources.push_back(s);
        }
    }
    stats = st;
}

// ---- indoor and ray tests ------------------------------------------------------------------------
struct IndoorEntry { bool indoor; uint64_t expires; };
std::unordered_map<uint64_t, IndoorEntry> indoorCache;
unsigned indoorTestsThisFrame = 0;
constexpr unsigned kIndoorTestsPerFrame = 48;

uint64_t NowMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

bool LocateIndoor(const float pos[3]) {
    constexpr float kDown = 2.0f;
    C3Vector a(pos[0], pos[1], pos[2]), b(pos[0], pos[1], pos[2] - kDown);
    CMapObjDef* defs[2] = {};
    uint32_t groups[4] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
    if (!CMap::LocateViewerMapObjs(a, b, 1.0f, defs, groups) || !defs[0] || groups[0] == 0xFFFF) return false;
    CMapObj* wmo = defs[0]->m_owner;
    if (!wmo) return false;
    uint32_t flags = wmo->GetGroupFlags(static_cast<int32_t>(groups[0]));
    if (groups[1] != 0xFFFF) flags |= wmo->GetGroupFlags(static_cast<int32_t>(groups[1]));
    return (flags & 0x2000u) != 0;
}

// Cached per half yard; answers expire after 5-7 s (a WMO still loading reads as outdoor), at most 48
// new tests a frame: past that the old answer, or outdoor, stands.
bool IsIndoor(const float* pos) {
    const auto q = [](float v) { return static_cast<uint64_t>(static_cast<int64_t>(std::floor(v * 2.0f)) & 0x1FFFFF); };
    const uint64_t key = q(pos[0]) | (q(pos[1]) << 21) | (q(pos[2]) << 42);
    const uint64_t now = NowMs();
    if (indoorCache.size() > 16384) indoorCache.clear();
    auto it = indoorCache.find(key);
    if (it != indoorCache.end() && now < it->second.expires) return it->second.indoor;
    if (indoorTestsThisFrame >= kIndoorTestsPerFrame) return it != indoorCache.end() && it->second.indoor;
    ++indoorTestsThisFrame;
    const bool indoor = LocateIndoor(pos);
    indoorCache[key] = IndoorEntry{indoor, now + 5000 + key % 2000};
    return indoor;
}

// Terrain and WMO faces only (0x100 | 0x20): models never block, so a light's own torch or firewood
// does not, and moving characters are not baked in as permanent blockers.
float TraceFraction(const float* a, const float* b) {
    const C3Vector start(a[0], a[1], a[2]), end(b[0], b[1], b[2]);
    C3Vector hit;
    float t = 1.0f;
    const int32_t blocked = World::Intersect(start, end, &hit, &t, 0x120, nullptr);
    return blocked && std::isfinite(t) && t >= 0.0f && t <= 1.0f ? t : -1.0f;
}

// ---- the per-frame light list ---------------------------------------------------------------------
void UpdateLights(float dt, const float eye[3], const float forward[3]) {
    scanTimer += dt;
    if (scanTimer >= 0.25f || scanGeneration != tables.Generation()) {
        scanTimer = 0.0f;
        scanGeneration = tables.Generation();
        ScanModels(eye, forward);
    }
    lightTime += dt;
    std::vector<sl::Candidate> cands;
    struct PendingLamp { sl::Candidate candidate; LampDebug* debug; };
    std::array<PendingLamp, sl::kMaxStaticLamps> lamps;
    unsigned lampCount = 0;
    unsigned fromClient = 0, fromWorld = 0, fileColor = 0;
    const float range = settings.range;
    for (const Source& s : sources) {
        LampDebug* debug = s.lampDebugIndex >= 0 ? &lampDebug[s.lampDebugIndex] : nullptr;
        if (debug) {
            debug->stage = "model no longer resident";
            debug->cells = 0;
            debug->indoor = false;
        }
        sl::Candidate c;
        c.radius = s.radius; c.falloff = s.falloff;
        std::memcpy(c.spotDir, s.spotDir, sizeof(c.spotDir));
        c.cosOuter = s.cosOuter; c.spotScale = s.spotScale;
        c.flickerMode = s.flickerMode; c.flickerSpeed = s.flickerSpeed; c.flickerAmount = s.flickerAmount;
        c.scale = s.scale > 0.0f ? s.scale : 1.0f;
        c.model = s.model;
        c.bake = s.bake;
        c.moving = s.follow != nullptr;
        // A model that has been destroyed since the scan takes its lights with it.
        if (s.model && !liveModels.count(s.model)) continue;
        float worldPos[3];
        std::memcpy(worldPos, s.worldPos, sizeof(worldPos));
        if (s.follow) {
            float w[16];
            bool animated = false;
            if (!liveModels.count(s.follow) || !WorldOf(s.follow, w, animated, 0)) continue;
            sl::MulPoint(s.localPos, w, worldPos);
            if (s.cosOuter > -1.5f) sl::MulDirection(s.localDir, w, c.spotDir);
            const float scale = sl::MatrixScale(w);
            c.scale = scale > 0.0f ? scale : 1.0f;
        }
        if (s.table) {
            std::memcpy(c.pos, worldPos, sizeof(c.pos));
            std::memcpy(c.color, s.color, sizeof(c.color));
        } else {
            // The client keeps the model's light record current while it is in view and leaves the last
            // values when it is culled: use them, else the file position and first colour.
            float stored[3] = {}, color[3] = {};
            bool placed = false, lit = false;
            if (s.ownModel && liveModels.count(s.ownModel) && s.ownModel->m_lights) {
                const CM2Light& light = s.ownModel->m_lights[s.ownIndex].m_light;
                stored[0] = light.m_pos.x; stored[1] = light.m_pos.y; stored[2] = light.m_pos.z;
                color[0] = light.m_dirColor.x; color[1] = light.m_dirColor.y; color[2] = light.m_dirColor.z;
                placed = stored[0] != 0.0f || stored[1] != 0.0f || stored[2] != 0.0f;
                lit = color[0] > 0.0f || color[1] > 0.0f || color[2] > 0.0f;
            }
            std::memcpy(c.pos, placed ? stored : worldPos, sizeof(c.pos));
            (placed ? fromClient : fromWorld)++;
            if (!lit) {
                if (!s.haveFileColor) continue;
                std::memcpy(color, s.fileColor, sizeof(color));
                ++fileColor;
            }
            std::memcpy(c.color, color, sizeof(c.color));
        }
        bool valid = std::isfinite(c.scale) && c.scale > 0.0f && std::isfinite(c.radius) && Finite3(c.spotDir);
        for (unsigned axis = 0; axis < 3; ++axis)
            valid = valid && std::isfinite(c.pos[axis]) && std::fabs(c.pos[axis]) < 100000.0f &&
                    std::isfinite(c.color[axis]) && c.color[axis] >= 0.0f;
        if (debug) debug->stage = "invalid light";
        if (!valid) continue;
        const float dx = c.pos[0] - eye[0], dy = c.pos[1] - eye[1], dz = c.pos[2] - eye[2];
        const float distanceSquared = dx * dx + dy * dy + dz * dz;
        if (debug) {
            debug->distance = std::sqrt(distanceSquared);
            debug->stage = "outside light range";
        }
        const float reach = s.staticLamp ? staticLampRange : range;
        if (distanceSquared > reach * reach || (s.staticLamp && distanceSquared == reach * reach)) continue;
        if (s.staticLamp) {
            if (debug) debug->stage = "zero strength";
            if (!staticLampsEnabled || staticLampStrength <= 0) continue;
            for (float& channel : c.color) channel *= staticLampStrength;
            c.supplemental = true;
            c.staticLamp = true;
            c.distanceFade = sl::StaticLampFade(std::sqrt(distanceSquared), staticLampRange);
            c.indoorOverride = s.indoorOverride;
            lamps[lampCount++] = {c, debug};
        } else {
            cands.push_back(c);
        }
    }
    stats.fromClient = fromClient; stats.fromWorld = fromWorld; stats.fileColor = fileColor;
    stats.inRange = static_cast<unsigned>(cands.size());
    stats.merged = sl::MergeCandidates(cands, settings.mergeDistance);
    const auto& lantern = WxlFrameLantern();
    if (lantern.strength > 0 && cands.size() < sl::kMaxPool)
    {
        const float dx = lantern.position[0] - eye[0], dy = lantern.position[1] - eye[1];
        const float dz = lantern.position[2] - eye[2];
        if (dx * dx + dy * dy + dz * dz < range * range)
        {
            sl::Candidate light;
            std::memcpy(light.pos, lantern.position, sizeof(light.pos));
            for (int i = 0; i < 3; ++i)
                light.color[i] = lantern.Color[i] * lantern.strength;
            light.radius = lantern.SurfaceRadius * 16.0f / settings.radius;
            light.flickerAmount = 0;
            light.moving = true;
            light.supplemental = true;
            cands.push_back(light);
        }
    }
    const auto& corpses = WxlFrameCorpseLights();
    for (unsigned i = 0; i < corpses.count && cands.size() < sl::kMaxPool; ++i)
    {
        sl::Candidate light;
        std::memcpy(light.pos, corpses.lights[i].position, sizeof(light.pos));
        light.color[0] = 0.38f; light.color[1] = 0.26f; light.color[2] = 0.08f;
        light.radius = 4.0f * 16.0f / settings.radius;
        light.flickerMode = sl::kFlickerOff;
        light.moving = true;
        light.supplemental = true;
        cands.push_back(light);
    }
    const auto& spells = WxlFrameSpellLights();
    for (unsigned i = 0; i < spells.count && cands.size() < sl::kMaxPool; ++i)
    {
        sl::Candidate light;
        std::memcpy(light.pos, spells.lights[i].position, sizeof(light.pos));
        std::memcpy(light.color, spells.lights[i].colour, sizeof(light.color));
        light.radius = spells.lights[i].reach * 16.0f / settings.radius;
        light.falloff = 2.0f;
        light.flickerMode = sl::kFlickerOff;
        light.moving = true;
        light.supplemental = true;
        cands.push_back(light);
    }
    stats.staticDuplicates = 0;
    std::array<LampDebug*, sl::kMaxStaticLamps> submittedLamps{};
    unsigned submitted = 0;
    size_t poolSlotsUsed = lampCount ? std::count_if(cands.begin(), cands.end(),
        [](const auto& light) { return light.weight > 0; }) : 0;
    for (unsigned i = 0; i < lampCount; ++i) {
        const auto& [lamp, debug] = lamps[i];
        bool duplicate = false;
        // Ignore entries retired by MergeCandidates; survivors retain their
        // original slots, so inspect the original authored range.
        for (unsigned j = 0; j < stats.inRange && !duplicate; ++j) {
            const auto& native = cands[j];
            if (native.weight <= 0) continue;
            const float x = lamp.pos[0] - native.pos[0], y = lamp.pos[1] - native.pos[1];
            const float z = lamp.pos[2] - native.pos[2];
            duplicate = x*x + y*y + z*z < 4.0f;
        }
        if (duplicate) {
            ++stats.staticDuplicates;
            if (debug) debug->stage = "near authored light";
        } else {
            if (debug) debug->stage = "pool limit";
            if (poolSlotsUsed < sl::kMaxPool) {
                cands.push_back(lamp);
                submittedLamps[submitted++] = debug;
                ++poolSlotsUsed;
            }
        }
    }
    indoorTestsThisFrame = 0;
    pool = sl::ChooseActive(cands, settings, eye, lightTime, [](const float* p) { return IsIndoor(p); });
    stats.active = static_cast<unsigned>(pool.active.size());
    stats.indoor = pool.indoor;
    stats.indoorTests = indoorTestsThisFrame;
    const auto bake = occlusion.Update(pool.active, pool.wantBake, eye, settings, &TraceFraction);
    stats.bakeWanted = bake.wanted; stats.bakeDone = bake.done; stats.bakeRays = bake.rays; stats.bakeCached = bake.cached;
    auto gridSettings = settings;
    if (staticLampsEnabled) gridSettings.range = std::max(settings.range, staticLampRange);
    gridBuilder.Build(pool.active, pool.baseLuminance, eye, gridSettings, grid, gridStats);
    if (submitted) {
        // Static entries were appended in order with room in the global pool.
        unsigned slot = 0;
        for (size_t i = 0; i < pool.active.size(); ++i) {
            const auto& light = pool.active[i];
            if (!light.staticLamp) continue;
            auto* debug = submittedLamps[slot++];
            if (!debug) continue;
            debug->indoor = light.indoor;
            debug->cells = sl::CountGridCells(grid, static_cast<uint16_t>(i), light);
            debug->stage = debug->cells ? "submitted" : "no grid slots";
        }
    }
}

void ClearLights() {
    sources.clear();
    lampDebug.clear();
    lampTransitions.clear();
    selectedStaticLamps = 0;
    liveModels.clear();
    pool = sl::ChosenPool();
    grid = sl::Grid{};
    gridBuilder.Reset();
    occlusion.Clear();
    rowCache.clear();
    indoorCache.clear();
    scanTimer = 0.25f;
    scanGeneration = 0;
    stats = Stats();
}

// ---- the texture ----------------------------------------------------------------------------------
IDirect3DDevice9* device = nullptr;
IDirect3DTexture9* texture = nullptr;
tex::Geometry geometry;
unsigned gridWritten = ~0u;
bool textureFailed = false, headerActive = false, headerInitialised = false, gridOn = false;
unsigned poolCount = 0;
std::vector<const float*> occWritten;

CGxDeviceD3d9Ex* Backend() {
    auto* gx = g_theGxDevicePtr;
    return gx && gx->m_api == GxApi_D3d9Ex ? static_cast<CGxDeviceD3d9Ex*>(gx) : nullptr;
}

void Bind(IDirect3DDevice9* d) {
    for (DWORD sampler : {static_cast<DWORD>(D3DVERTEXTEXTURESAMPLER0), static_cast<DWORD>(13)}) {
        d->SetTexture(sampler, texture);
        if (!texture) continue;
        d->SetSamplerState(sampler, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        d->SetSamplerState(sampler, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        d->SetSamplerState(sampler, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        d->SetSamplerState(sampler, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        d->SetSamplerState(sampler, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        if (sampler == 13) d->SetSamplerState(sampler, D3DSAMP_SRGBTEXTURE, FALSE);
    }
}

void ReleaseTexture() {
    if (texture && device) {
        for (DWORD sampler : {static_cast<DWORD>(D3DVERTEXTEXTURESAMPLER0), static_cast<DWORD>(13)}) {
            IDirect3DBaseTexture9* bound = nullptr;
            if (SUCCEEDED(device->GetTexture(sampler, &bound)) && bound == texture) device->SetTexture(sampler, nullptr);
            if (bound) bound->Release();
        }
    }
    if (texture) texture->Release();
    texture = nullptr;
    device = nullptr;
    geometry = tex::Geometry();
    headerInitialised = false;
    gridWritten = ~0u;
    occWritten.clear();
}

bool EnsureTexture(IDirect3DDevice9* d) {
    if (d != device) { ReleaseTexture(); textureFailed = false; device = d; }
    const tex::Geometry wanted = tex::Geometry::For(grid);
    // Before the first grid exists the texture only needs its header and the (empty) regions.
    const tex::Geometry sized = grid.cells ? wanted : tex::Geometry{0, 1 + tex::kPoolRows + tex::kOccRows};
    if (texture && sized.height == geometry.height) return true;
    if (texture) { ReleaseTexture(); device = d; textureFailed = false; }
    if (textureFailed) return false;
    if (FAILED(d->CreateTexture(tex::kWidth, sized.height, 1, 0, D3DFMT_A32B32G32R32F, D3DPOOL_MANAGED, &texture, nullptr))) {
        texture = nullptr;
        textureFailed = true;
        return false;
    }
    geometry = sized;
    gridWritten = ~0u;
    occWritten.clear();
    return true;
}

constexpr UINT kRimBranch = 15;
float characterRimStrength = 0, characterRimWidth = 4, characterRimTint = 0.45f;
const CM2Model* rimPlayer = nullptr;
bool rimHeaderWritten = false, rimArmed = false;
float rimTexels[8] = {};

void ArmCharacterRim(const CM2Model* model) {
    if (!model && !rimArmed) return;
    auto* gx = Backend();
    if (!gx || !gx->m_d3dDevice) return;

    BOOL enabled = FALSE;
    if (model && rimPlayer && model->m_scene == Scene()) {
        for (unsigned parents = 0; model && parents < 8; ++parents, model = model->m_attachParent) {
            if (model == rimPlayer) {
                enabled = TRUE;
                break;
            }
        }
        if (!enabled) return;
    }
    if (rimArmed == (enabled != FALSE)) return;
    if (SUCCEEDED(gx->m_d3dDevice->SetVertexShaderConstantB(kRimBranch, &enabled, 1)))
        rimArmed = enabled != FALSE;
}

void UpdateCharacterRim(const float view[16], bool viewOk) {
    rimPlayer = nullptr;
    NativeVisualHooks::characterRim = nullptr;
    if (!weatherPackLoaded || !viewOk || characterRimStrength <= 0) return;
    const auto* player = ClntObjMgrGetActivePlayerObj();
    const auto* sky = DayNight::GetInfo();
    if (!player || !sky) return;
    const auto* model = player->GetObjectModel();
    if (!model || model->m_scene != Scene() || !(model->m_fFlags & 1)) return;

    sl::CharacterRim rim{{1.0f - 0.64f * characterRimTint, 0.71f,
                          0.28f + 0.72f * characterRimTint}, characterRimStrength, characterRimWidth};
    unsigned links = 0;
    for (const auto* effect = player->m_procEffects; effect && links++ < 24; effect = effect->m_next) {
        const auto* kit = effect->m_kitRec;
        if (kit && sl::ReadAuthoredRim(kit->m_charProc, kit->m_charParamZero, kit->m_charParamOne,
                                       kit->m_charParamTwo, kit->m_charParamThree, rim)) break;
    }
    if (rim.strength <= 0) return;

    const auto* origin = &model->m_worldTransform.d0;
    const bool moon = sky->m_dayProgression < 0.2013889f || sky->m_dayProgression > 0.9236111f;
    const auto& celestial = moon ? DayNight::g_planets.m_moon.m_position : DayNight::g_planets.m_sun.m_position;
    float direction[] = {celestial.x - sky->m_cameraPos.x, celestial.y - sky->m_cameraPos.y,
                         celestial.z - sky->m_cameraPos.z};
    float strongest = moon ? 0.12f : 0.35f;
    auto consider = [&](const float* pos, const float* rgb, float radius, float gain) {
        if (!(radius > 0)) return;
        const float candidate[] = {pos[0] - origin[0], pos[1] - origin[1], pos[2] - origin[2] - 1.0f};
        const float distance = std::sqrt(candidate[0]*candidate[0] + candidate[1]*candidate[1] + candidate[2]*candidate[2]);
        if (!(distance > 0.1f && distance < radius)) return;
        const float edge = 1.0f - distance / radius;
        const float score = std::max({rgb[0], rgb[1], rgb[2]}) * gain * edge * edge;
        if (score > strongest) {
            strongest = score;
            std::copy_n(candidate, 3, direction);
        }
    };
    const auto& lantern = WxlFrameLantern();
    if (lantern.strength > 0) consider(lantern.position, lantern.Color, lantern.SurfaceRadius, lantern.strength);
    for (const auto& light : pool.active) consider(light.pos, light.color, light.radius, 1);
    const float length = std::sqrt(direction[0]*direction[0] + direction[1]*direction[1] + direction[2]*direction[2]);
    if (!(length > 0.001f) || !std::isfinite(length)) return;

    std::copy_n(rim.color.data(), 3, rimTexels);
    rimTexels[3] = rim.strength;
    for (unsigned axis = 0; axis < 3; ++axis)
        rimTexels[4 + axis] = (direction[0] * view[axis] + direction[1] * view[4 + axis] +
                                direction[2] * view[8 + axis]) / length;
    rimTexels[7] = rim.exponent;
    rimPlayer = model;
}

float* LockRows(int first, int count, int& pitchFloats) {
    D3DLOCKED_RECT lr{};
    const RECT r{0, first, tex::kWidth, first + count};
    if (!texture || count <= 0 || FAILED(texture->LockRect(0, &lr, &r, 0))) return nullptr;
    pitchFloats = lr.Pitch / static_cast<int>(sizeof(float));
    return static_cast<float*>(lr.pBits);
}

bool FillGrid(bool on) {
    if (!on) { poolCount = 0; gridOn = false; return true; }
    int pitch = 0;
    if (gridWritten != grid.generation || !gridOn) {
        float* t = LockRows(1, geometry.entryRows, pitch);
        if (!t) return false;
        tex::WriteEntries(t, pitch, grid, geometry);
        if (FAILED(texture->UnlockRect(0))) return false;
        gridWritten = grid.generation;
    }
    float* t = LockRows(geometry.PoolRow(), tex::kPoolRows, pitch);
    if (!t) return false;
    poolCount = tex::WritePool(t, pitch, pool.active);
    if (FAILED(texture->UnlockRect(0))) return false;
    bool changed = occWritten.size() != poolCount;
    for (unsigned p = 0; p < poolCount && !changed; ++p) changed = occWritten[p] != pool.active[p].occlusion;
    if (changed) {
        t = LockRows(geometry.OccRow(), tex::kOccRows, pitch);
        if (!t) return false;
        tex::WriteOcclusion(t, pitch, pool.active, poolCount);
        occWritten.assign(poolCount, nullptr);
        for (unsigned p = 0; p < poolCount; ++p) occWritten[p] = pool.active[p].occlusion;
        if (FAILED(texture->UnlockRect(0))) return false;
    }
    gridOn = true;
    return true;
}

bool FillHeader(bool active, const float view[16], const float camera[3]) {
    if (active && !gridOn) active = false;
    // Keep the zero header and the binding while disabled: repeated managed-texture locks add work even
    // though the shader-visible bytes cannot change.
    if (!active && !headerActive && headerInitialised && !rimPlayer && !rimHeaderWritten) return true;
    int pitch = 0;
    float* t = LockRows(0, 1, pitch);
    if (!t) return false;
    tex::HeaderInput in;
    in.active = active;
    in.stockOff = settings.modelStockOff != 0;
    in.grid = &grid;
    in.geometry = &geometry;
    in.bakedAdd = settings.bakedAdd;
    if (active) {
        std::memcpy(in.camera, camera, sizeof(in.camera));
        std::memcpy(in.view, view, sizeof(in.view));
    }
    tex::WriteHeader(t, in);
    if (rimPlayer) std::copy_n(rimTexels, 8, t + 12 * 4);
    rimHeaderWritten = rimPlayer != nullptr;
    if (FAILED(texture->UnlockRect(0))) return false;
    headerActive = active;
    headerInitialised = true;
    return true;
}

constexpr DWORD kTerrainShadowSampler = 14;
constexpr UINT kTerrainShadowConstant = 218;
wxl::atmosphere::GroundFloor terrainHeights;
IDirect3DTexture9* terrainShadow = nullptr;
double terrainShadowAt = 0;
int terrainShadowMap = -1;
float terrainShadowParams[4] = {};
float terrainShadowCenter[2] = {};
unsigned terrainShadowFrames = 0;
double terrainShadowMs = 0;

bool SampleTerrain(int map, float x, float y, wxl::atmosphere::FloorSample& out) {
    int32_t water;
    return CMap::QueryResidentFloor(map, x, y, out.height, water, false);
}

void ResetTerrainShadow() {
    if (terrainShadow) {
        if (auto* gx = Backend(); gx && gx->m_d3dDevice)
            gx->m_d3dDevice->SetTexture(kTerrainShadowSampler, nullptr);
        terrainShadow->Release();
        terrainShadow = nullptr;
    }
    terrainHeights.Reset();
    terrainShadowMap = -1;
    terrainShadowParams[3] = 0;
    terrainShadowAt = 0;
}

void UpdateTerrainShadow(IDirect3DDevice9* d, const float* camera, bool enabled, double now) {
    using wxl::atmosphere::GroundFloor;
    constexpr int side = GroundFloor::Side;
    const float size = side * GroundFloor::CellYards;
    const auto* sky = DayNight::GetInfo();
    const auto& sun = DayNight::g_planets.m_sun.m_position;
    const auto direction = sky ? sun - sky->m_cameraPos : C3Vector(0, 0, 0);
    const float horizontal = std::hypot(direction.x, direction.y);
    const int map = World::QueryMapId();
    if (!enabled || map < 0 || CMap::bDungeon || !sky || !std::isfinite(horizontal) || horizontal < 0.001f ||
        !(direction.z > horizontal * 0.02f && direction.z < horizontal) ||
        sky->m_dayProgression < 0.2013889f || sky->m_dayProgression > 0.9236111f) {
        if (terrainShadow || terrainHeights.HasGrid()) ResetTerrainShadow();
        d->SetPixelShaderConstantF(kTerrainShadowConstant, terrainShadowParams, 1);
        return;
    }
    const double began = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
    // Reset a far-away field immediately on a teleport, even on the same map.
    if (map != terrainShadowMap || std::abs(camera[0] - terrainShadowCenter[0]) >= size * 0.5f ||
        std::abs(camera[1] - terrainShadowCenter[1]) >= size * 0.5f)
        terrainShadowParams[3] = 0;
    terrainHeights.Update(map, camera, now, SampleTerrain);
    if (now >= terrainShadowAt && terrainHeights.Known() > 0) {
        terrainShadowAt = now + 0.12;
        if (!terrainShadow)
            d->CreateTexture(side, side, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &terrainShadow, nullptr);
        D3DLOCKED_RECT lock{};
        if (terrainShadow && SUCCEEDED(terrainShadow->LockRect(0, &lock, nullptr, 0))) {
            wxl::atmosphere::TerrainShade shade;
            wxl::atmosphere::BuildTerrainShade(terrainHeights.Texels(), direction.x, direction.y, direction.z, shade);
            for (int y = 0; y < side; ++y)
                std::memcpy(static_cast<char*>(lock.pBits) + y * lock.Pitch, shade.data() + y * side, side * sizeof(uint32_t));
            if (SUCCEEDED(terrainShadow->UnlockRect(0))) {
                float uv[2];
                terrainHeights.CameraUV(camera, uv);
                terrainShadowParams[0] = camera[0] - size * uv[0];
                terrainShadowParams[1] = camera[1] - size * uv[1];
                terrainShadowParams[2] = 1.0f / size;
                terrainShadowParams[3] = 0.38f;
                terrainShadowMap = map;
                terrainShadowCenter[0] = camera[0];
                terrainShadowCenter[1] = camera[1];
            } else {
                ResetTerrainShadow();
            }
        }
    }
    if (terrainShadowParams[3] > 0) {
        d->SetTexture(kTerrainShadowSampler, terrainShadow);
        d->SetSamplerState(kTerrainShadowSampler, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        d->SetSamplerState(kTerrainShadowSampler, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        d->SetSamplerState(kTerrainShadowSampler, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        d->SetSamplerState(kTerrainShadowSampler, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        d->SetSamplerState(kTerrainShadowSampler, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    }
    d->SetPixelShaderConstantF(kTerrainShadowConstant, terrainShadowParams, 1);
    const double end = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
    terrainShadowMs += end - began;
    if (++terrainShadowFrames == 300) {
        if (WxlProfileEnabled("WXL_PROFILER", false))
            std::fprintf(stderr, "WarcraftXL: terrain shadows %.4f ms CPU average, %u/1024 terrain samples\n",
                         terrainShadowMs / terrainShadowFrames, terrainHeights.Known());
        terrainShadowMs = 0;
        terrainShadowFrames = 0;
    }
}

void ResetGpu() {
    ReleaseTexture();
    gridOn = false;
    headerActive = false;
    textureFailed = false;
    poolCount = 0;
}

// Fills the texture from this frame's list and binds it; false = leave the stock lighting.
bool Prepare(IDirect3DDevice9* d, bool lightsOn, const float view[16], const float camera[3]) {
    if (!EnsureTexture(d)) { gridOn = false; headerActive = false; return false; }
    const bool wantLights = lightsOn && grid.cells > 0;
    if (!FillGrid(wantLights) || !FillHeader(lightsOn && settings.models, view, camera)) {
        // Never expose a new header with a stale grid. Unbinding the texture makes the patched shaders
        // use the native lighting.
        ResetGpu();
        return false;
    }
    Bind(d);
    return gridOn;
}

// VS c250.x: the stock terrain point lights' share (1 = as the client lights the chunk, 0 = off).
constexpr UINT kStockSwitch = 250, kGridConstant = 206, kGridConstant2 = 212;
bool terrainLightsOn = false;
}  // namespace

void WxlSurfaceLightsInitialize() { (void)registered; }

bool WxlSurfaceLightsAvailable() {
    EnsurePack();
    return ShadersActive();
}

void WxlSelectWaterLamps(const float* eye, WaterLamp (&lamps)[2]) {
    const auto& lantern = WxlFrameLantern();
    unsigned first = 0;
    if (lantern.strength > 0) {
        std::copy_n(lantern.position, 3, lamps[0].position);
        for (unsigned i = 0; i < 3; ++i) lamps[0].color[i] = lantern.Color[i] * lantern.strength;
        lamps[0].radius = lantern.SurfaceRadius;
        first = 1;
    }
    if (!staticLampsEnabled || !settings.enabled || !terrainLightsOn) return;
    float nearest[] = {32.0f * 32.0f, 32.0f * 32.0f};
    for (const auto& light : pool.active) {
        if (!light.staticLamp || light.indoor) continue;
        const float x = light.pos[0] - eye[0], y = light.pos[1] - eye[1], z = light.pos[2] - eye[2];
        const float distance = x*x + y*y + z*z;
        if (!(distance < nearest[1])) continue;
        const unsigned slot = first == 0 && distance < nearest[0] ? 0 : 1;
        if (slot == 0) {
            lamps[1] = lamps[0];
            nearest[1] = nearest[0];
        }
        nearest[slot] = distance;
        const float fade = std::clamp((32.0f - std::sqrt(distance)) / 8.0f, 0.0f, 1.0f);
        std::copy_n(light.pos, 3, lamps[slot].position);
        for (unsigned i = 0; i < 3; ++i) lamps[slot].color[i] = light.color[i] * fade;
        lamps[slot].radius = light.radius;
    }
}

void WxlSurfaceLightsApply(const double* values, size_t count) {
    if (!values || count < env::kCount) return;
    settings.enabled = static_cast<int>(values[env::kLightsEnabled]);
    settings.models = static_cast<int>(values[env::kLightsModels]);
    settings.brightness = static_cast<float>(values[env::kLightsBrightness]);
    settings.radius = static_cast<float>(values[env::kLightsRadius]);
    settings.range = static_cast<float>(values[env::kLightsRange]);
    settings.maxPerCell = static_cast<int>(values[env::kLightsPerCell]);
    settings.flicker = static_cast<float>(values[env::kLightsFlicker]);
    settings.flickerSpeed = static_cast<float>(values[env::kLightsFlickerSpeed]);
    settings.bakeEnabled = static_cast<int>(values[env::kLightsOcclusion]);
    settings.bakeRaysPerFrame = static_cast<int>(values[env::kLightsRaysPerFrame]);
    settings.indoorSkipsTerrain = static_cast<int>(values[env::kLightsIndoorIsolation]);
    settings.modelStockOff = 1;
    const bool enableLamps = values[env::kAtmosphereOverride] && values[env::kStaticLamps];
    const float range = static_cast<float>(values[env::kStaticLampRange]);
    if (staticLampsEnabled != enableLamps || staticLampRange != range) scanTimer = 0.25f;
    staticLampRange = range;
    staticLampsEnabled = enableLamps;
    staticLampStrength = static_cast<float>(values[env::kStaticLampStrength]);
    characterRimStrength = values[env::kAtmosphereOverride] && values[env::kCharacterRim] ?
        static_cast<float>(values[env::kRimStrength]) : 0.0f;
    characterRimWidth = static_cast<float>(values[env::kRimWidth]);
    characterRimTint = static_cast<float>(values[env::kRimTint]);
}

void WxlSurfaceLightsBeforeTerrain() {
    if (!WxlSurfaceLightsAvailable()) return;
    CGxDeviceD3d9Ex* gx = Backend();
    IDirect3DDevice9* d = gx ? gx->m_d3dDevice : nullptr;
    if (!d) return;
    static bool announced = false;
    if (!announced) {
        announced = true;
        NativeVisualHooks::modelDestroyed = &ModelDestroyed;
        std::fprintf(stderr, "WarcraftXL: surface lights shaders active (%u terrain, %u M2, %u WMO replaced so far); lights %s\n",
                     replaced[sl::kGroupTerrain], replaced[sl::kGroupModel], replaced[sl::kGroupWmo],
                     settings.enabled ? "on" : "off (Environment page: Dynamic lights)");
    }
    if (!tablesLoaded) {
        tablesLoaded = true;
        const auto load = [](const char* file, sl::Wdbc& out, std::initializer_list<uint32_t> strings) {
            std::ifstream in(ClientDirectory() / "DBFilesClient" / file, std::ios::binary);
            if (!in) return false;
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            std::string why;
            if (!sl::ParseWdbc(bytes.data(), bytes.size(), out, why, strings)) { tableStatus += std::string(" (") + file + ": " + why + ")"; return false; }
            return true;
        };
        tableStatus.clear();
        sl::Wdbc properties, assignments;
        tables.Clear();
        if (load("DoodadLightProperties.cdbc", properties, {1})) tables.LoadProperties(properties);
        if (load("DoodadLightAssignment.cdbc", assignments, {1, 4})) tables.LoadAssignments(assignments);
        char line[160];
        std::snprintf(line, sizeof(line), "%zu light(s), %zu assignment(s)%s", tables.AllProperties().size(), tables.AllAssignments().size(),
                      tables.Rejected() ? ", some rows rejected" : "");
        tableStatus = line + tableStatus;
    }
    const auto now = std::chrono::steady_clock::now();
    const float dt = haveLastUpdate ? std::chrono::duration<float>(now - lastUpdate).count() : 0.0f;
    lastUpdate = now;
    haveLastUpdate = true;

    // The view the world draws with (camera-relative world -> view) and the camera.
    float view[16], camera[3] = {};
    CGxMatrixStack& viewStack = g_theGxDevicePtr->m_xforms[GxXform_View];
    std::memcpy(view, Floats(viewStack.m_mtx[viewStack.m_level]), sizeof(view));
    if (const C3Vector* cam = World::GetCamPos()) { camera[0] = cam->x; camera[1] = cam->y; camera[2] = cam->z; }
    bool viewOk = true;
    for (float v : view) viewOk = viewOk && std::isfinite(v);
    viewOk = viewOk && Finite3(camera);

    if (weatherPackLoaded) {
        const float wet[] = {viewOk ? WxlSurfaceWetness() : 0.0f, 0.22f, 0.22f, 0.085f};
        const float eye[] = {camera[0], camera[1], camera[2], 0};
        d->SetPixelShaderConstantF(215, wet, 1);
        d->SetPixelShaderConstantF(216, eye, 1);
        d->SetPixelShaderConstantF(217, WxlSurfaceCloud(), 1);
        d->SetVertexShaderConstantF(kWetVertex, wet, 1);
        if (wmoWeatherSelected) {
            const float* cloud = WxlSurfaceCloud();
            wmoWeatherVisible = viewOk && (wet[0] > 0 || cloud[0] > 0);
            wmoCloud[0] = wmoWeatherVisible ? cloud[0] : 0.0f;
            wmoCloud[1] = 1.0f;
            wmoCloud[3] = 0.0f;
            wmoWeatherExterior = false;
            d->SetVertexShaderConstantF(kWmoCloud, wmoCloud, 1);
            if (wmoCloud[0] > 0) {
                float transform[8];
                for (unsigned axis = 0; axis < 2; ++axis) {
                    float* row = transform + 4 * axis;
                    const float* rotation = view + 4 * axis;
                    for (unsigned i = 0; i < 3; ++i)
                        row[i] = rotation[i] * cloud[3];
                    row[3] = cloud[1 + axis] + camera[axis] * cloud[3] -
                             (row[0] * view[12] + row[1] * view[13] + row[2] * view[14]);
                }
                d->SetVertexShaderConstantF(kWmoCloudTransform, transform, 2);
            }
        }
    }
    if (weatherPackLoaded) UpdateTerrainShadow(d, camera, viewOk && WxlTerrainShadows(), std::chrono::duration<double>(now.time_since_epoch()).count());
    const bool lightsOn = viewOk && settings.enabled && Scene();
    if (lightsOn) {
        const auto& direction = DayNight::GetInfo()->m_cameraDir;
        float forward[3] = {direction.x, direction.y, direction.z};
        const float length = std::sqrt(direction.SquaredMag());
        if (std::isfinite(length) && length > 0) {
            for (float& axis : forward) axis /= length;
        } else {
            std::fill_n(forward, 3, 0.0f);
        }
        UpdateLights(std::min(dt, 0.25f), camera, forward);
    }
    else if (grid.cells) { ClearLights(); }
    UpdateCharacterRim(view, viewOk);
    const bool ready = Prepare(d, lightsOn, view, camera);
    if (rimPlayer && texture && rimHeaderWritten)
        NativeVisualHooks::characterRim = &ArmCharacterRim;
    float c206[4] = {}, c212[4] = {};
    const bool on = lightsOn && ready && gridOn && texture;
    tex::GridConstants(grid, geometry, on, c206, c212);
    // The terrain's pixel shaders read the shared texture on s13 and the grid from PS c206/c212. Zero
    // lights per cell while off: the loop ends at once.
    d->SetPixelShaderConstantF(kGridConstant, c206, 1);
    d->SetPixelShaderConstantF(kGridConstant2, c212, 1);
    terrainLightsOn = on;
    const float stock[4] = {on ? 0.0f : 1.0f, 0.0f, 0.0f, 0.0f};
    d->SetVertexShaderConstantF(kStockSwitch, stock, 1);
}

bool WxlSetWmoWeatherExterior(bool exterior) {
    const bool previous = wmoWeatherExterior;
    if (previous != exterior && wmoWeatherVisible) {
        if (auto* gx = Backend(); gx && gx->m_d3dDevice) {
            wmoWeatherExterior = exterior;
            wmoCloud[3] = exterior ? 1.0f : 0.0f;
            gx->m_d3dDevice->SetVertexShaderConstantF(kWmoCloud, wmoCloud, 1);
        }
    }
    return previous;
}

void WxlSurfaceLightsFrame() {
    if (rimArmed) ArmCharacterRim(nullptr);
    NativeVisualHooks::characterRim = nullptr;
    rimPlayer = nullptr;
    wmoWeatherVisible = false;
    wmoWeatherExterior = false;
    if (weatherPackLoaded) {
        if (auto* gx = Backend(); gx && gx->m_d3dDevice) {
            const float dry[4] = {};
            gx->m_d3dDevice->SetPixelShaderConstantF(215, dry, 1);
            gx->m_d3dDevice->SetPixelShaderConstantF(217, dry, 1);
            gx->m_d3dDevice->SetPixelShaderConstantF(kTerrainShadowConstant, dry, 1);
            gx->m_d3dDevice->SetVertexShaderConstantF(kWetVertex, dry, 1);
            if (wmoWeatherSelected) gx->m_d3dDevice->SetVertexShaderConstantF(kWmoCloud, dry, 1);
        }
    }
    if (!texture || !device) return;
    CGxDeviceD3d9Ex* gx = Backend();
    IDirect3DDevice9* d = gx ? gx->m_d3dDevice : nullptr;
    if (!d) return;
    if (d != device) { ResetGpu(); return; }
    // Screens without a world (login, character select) keep the client's own lighting.
    if (headerActive) {
        int pitch = 0;
        float* t = LockRows(0, 1, pitch);
        if (!t) { ResetGpu(); return; }
        tex::WriteHeader(t, tex::HeaderInput{});
        if (FAILED(texture->UnlockRect(0))) { ResetGpu(); return; }
        headerActive = false;
    }
    Bind(d);
}

void WxlSurfaceLightsRelease() {
    ArmCharacterRim(nullptr);
    rimArmed = false;
    NativeVisualHooks::characterRim = nullptr;
    rimPlayer = nullptr;
    ResetTerrainShadow();
    wmoWeatherVisible = false;
    wmoWeatherExterior = false;
    ResetGpu();
}

void WxlSurfaceLightsWorldLeave() {
    ClearLights();
    haveLastUpdate = false;
    WxlSurfaceLightsRelease();
}

std::string WxlSurfaceLightsStatus() {
    EnsurePack();
    char text[768];
    if (!profileOn) {
        return "Lighting: the surface-light shaders are not loaded (WXL_SURFACE_LIGHTS is turned off in WarcraftXL.cfg or the environment). "
               "The light settings are saved but change nothing.";
    }
    if (!packLoaded) {
        std::snprintf(text, sizeof(text), "Lighting: unavailable, the shader pack could not be loaded (%s). Run tools/build-light-shader-pack.py.",
                      packProblem.c_str());
        return text;
    }
    if (!ShadersActive()) return "Lighting: the shader pack is loaded but the terrain shaders have not been replaced yet; enter the world.";
    const char* condition = !settings.enabled ? "off in settings (the patched shaders are inert)"
                          : !Scene() ? "enter the world to use surface lights"
                          : stats.active == 0 ? "on, no active light sources nearby"
                          : !texture ? "light texture unavailable"
                          : "on";
    std::snprintf(text, sizeof(text),
                  "Lighting: %s. %u active sources, %u nearby models scanned (%u model lights, %u table lights, %u merged); tables: %s.\n"
                  "Terrain lights %s; shaders replaced: terrain %u, M2 %u, WMO %u%s; occlusion %u/%u baked, %u cached; %u indoor.",
                  condition, stats.active, stats.modelsInRange, stats.modelLights, stats.tableLights, stats.merged, tableStatus.c_str(),
                  terrainLightsOn ? "on" : "off", replaced[sl::kGroupTerrain], replaced[sl::kGroupModel], replaced[sl::kGroupWmo],
                  wmoLights ? "" : " (buildings not lit: WMO lights are off, the WMO layered-material pack has no light variants)",
                  stats.bakeDone, stats.bakeWanted, stats.bakeCached, stats.indoor);
    std::string status = text;
    if (staticLampsEnabled) {
        char lamps[380];
        std::snprintf(lamps, sizeof(lamps), "\nStatic lamps: %.0f yd, %u/%u selected, %u budget drops, %u waiting for WMO groups, %u near authored lights; scan %.3f ms. %u doodad matches and %u embedded fixtures.",
                      staticLampRange, selectedStaticLamps, sl::StaticLampBudget(staticLampRange),
                      stats.lampsBudgetDropped, stats.lampsGroupPending, stats.staticDuplicates, stats.lampScanMs, stats.staticLamps, stats.meshLamps);
        status += lamps;
    }
    return status;
}
std::string WxlStaticLampDebug() {
    if (!staticLampsEnabled) return "Static lamps: off.";
    if (!settings.enabled) return "Static lamps: enable Dynamic lights.";
    if (lampDebug.empty()) return "Static lamps: no matches captured.";
    std::string result;
    for (const auto& lamp : lampDebug) {
        char values[256];
        const float intensity = lamp.intensity * staticLampStrength * settings.brightness;
        std::snprintf(values, sizeof(values),
            " %.1f yd @ (%.2f, %.2f, %.2f) radius %.1f intensity %.2f RGB %.2f/%.2f/%.2f | %s, %u cells%s",
            lamp.distance, lamp.pos[0], lamp.pos[1], lamp.pos[2],
            sl::StaticLampRadius(lamp.radius, settings), intensity,
            lamp.color[0] * intensity, lamp.color[1] * intensity, lamp.color[2] * intensity, lamp.stage, lamp.cells,
            lamp.indoor ? ", indoor (terrain excluded)" : "");
        if (!result.empty()) result += '\n';
        result += lamp.path;
        result += values;
    }
    if (!lampTransitions.empty()) {
        result += "\nRecent lamp selection changes";
        for (const auto& event : lampTransitions) result += "\n" + event;
    }
    return result;
}
std::string WxlNearbyModelDebug(float maxYards) {
    if (!Scene()) return "Nearby models: enter the world.";
    const auto* player = ClntObjMgrGetActivePlayerObj();
    const C3Vector position = player ? player->GetPosition() : *World::GetCamPos();
    const float center[3] = {position.x, position.y, position.z};
    if (!Finite3(center)) return "Nearby models: position unavailable.";
    const float range = std::clamp(std::isfinite(maxYards) ? maxYards : 12.0f, 1.0f, 256.0f);
    std::vector<SceneModel> map, scene;
    unsigned attached = 0;
    const unsigned sceneVisited = SceneModels(center, range, scene, attached);
    const unsigned mapVisited = MapDoodadModels(center, range, map);
    LoadLampCatalog();

    std::unordered_set<const CM2Model*> inSceneRange;
    for (const SceneModel& m : scene) inSceneRange.insert(m.model);
    std::vector<const SceneModel*> nearest;
    unsigned wmo = 0, wmoOutsideSceneRange = 0;
    sl::VisitStaticPlacements(map, scene, [&](const SceneModel& m) {
        nearest.push_back(&m);
        wmo += m.wmoDoodad;
        wmoOutsideSceneRange += m.wmoDoodad && inSceneRange.find(m.model) == inSceneRange.end();
    });
    const size_t shown = std::min<size_t>(nearest.size(), kLampDebugLimit);
    std::partial_sort(nearest.begin(), nearest.begin() + shown, nearest.end(),
        [](const SceneModel* a, const SceneModel* b) { return a->distance < b->distance; });
    char line[256];
    std::snprintf(line, sizeof(line),
        "Nearby models within %.1f yd of %s (%.2f, %.2f, %.2f): %zu models, %u WMO (%u map-only in range). Scene %u/%u, map %u/%u visited.",
        range, player ? "player" : "camera", center[0], center[1], center[2],
        nearest.size(), wmo, wmoOutsideSceneRange, sceneVisited, kMaxSceneModels, mapVisited, kMaxMapPlacements);
    std::string result = line;
    for (size_t i = 0; i < shown; ++i) {
        const auto& m = *nearest[i];
        const auto path = sl::NormalizeLampPath(m.path);
        std::snprintf(line, sizeof(line),
            " %.1f yd @ (%.2f, %.2f, %.2f) parent=%s WMO=%s scene=%s lights=%s catalog=%s%s",
            m.distance, m.world[12], m.world[13], m.world[14], m.parent ? "yes" : "no", m.wmoDoodad ? "yes" : "no",
            inSceneRange.find(m.model) != inSceneRange.end() ? "yes" : "no",
            m.data ? std::to_string(m.data->m_lights.m_count).c_str() : "loading",
            lampCatalog.find(path) != lampCatalog.end() ? "yes" : "no", m.disabled ? " disabled" : "");
        result += '\n';
        result += path.empty() ? "(unnamed)" : path;
        result += line;
    }
    return result;
}

#endif
