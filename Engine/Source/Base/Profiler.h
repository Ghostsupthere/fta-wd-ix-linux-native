#ifndef ENGINE_SOURCE_BASE_PROFILER_H
#define ENGINE_SOURCE_BASE_PROFILER_H

#include <cstdint>

class C3Vector;
class C44Matrix;

// A small frame profiler for the native Linux client: where the main thread's time goes, plus draw calls and
// triangles per frame. Zones nest; each reports self time (without nested zones) and total time. Everything runs
// on the main thread: a Scope created on any other thread, or while the profiler is disabled, does nothing.
// Results are averaged over the most recent frames and read through GetProfilerZones() in Lua, or logged
// every N seconds when WXL_PROFILE_LOG=N is set. WXL_PROFILER=0 turns the profiler off.
namespace Profiler {

enum Id : uint32_t {
    WorldRender = 0,  // CGWorldFrame::OnWorldRender, the whole 3D view
    WorldScene,       // CWorldScene::Render, the scene inside it
    Terrain,          // CWorldScene::RenderChunks
    WmoDraw,          // CWorldScene::RenderMapObjDefGroups (WMO structures)
    Weather,          // CWorldScene::RenderWeather (rain, snow, sand particles)
    M2Animate,        // CM2Scene::Animate, model animation (main thread plus its worker)
    M2Draw,           // CM2Scene::Draw, model passes
    Present,          // the device's ScenePresent: includes waiting for the GPU or vsync
    Paint,            // the paint event: every screen layer (the world view, nested, and the UI) and the present
    ClientIdle,       // the client's idle handler: game clock, zone, network-driven updates
    WorldUpdate,      // CGWorldFrame::Update: object and camera updates before the world draws
    UiIdle,           // CGGameUI::Idle: the UI's own per-frame work
    UiUpdate,         // CSimpleTop::OnLayerUpdate: layout and every frame's OnUpdate script (add-ons)
    UiRender,         // CSimpleTop::OnLayerRender: drawing every visible frame, texture and font string
    M2TransparentSort, // both transparent passes, including key preparation
    UiLayout,
    UiFrameUpdates,
    UiOnUpdateScripts,
    UiBatchBuild,
    UiTextures,
    UiText,
    UiCallbacks,
    UiCallbackWorld,
    UiCallbackUnitModel,
    UiCallbackModel,
    UiCallbackMinimap,
    UiCallbackQuestPoi,
    UiCallbackScroll,
    UiCallbackMovie,
    UiCallbackModelFfx,
    UiCallbackOther,
    WorldFrameUpdate, // CGWorldFrame::OnWorldUpdate, inside the world frame callback
    PlayerNameUpdate,
    WorldTextRender,
    WorldUpdateCamera,
    WorldUpdateDayNightAndHousing,
    WorldUpdateAudio,
    WorldUpdateObjectEffects,
    WorldUpdatePortraits,
    WorldUpdateObjectDisplays,
    WorldUpdateSpells,
    WorldUpdateMap,
    WorldUpdateAreaOfInterest,
    WorldUpdateScene,
    WorldUpdateMapPreparation,
    WorldUpdateSmoothFacing,
    MapPrepareWmoResources,
    MapPrepareAreas,
    MapPrepareAreaQueues,
    MapPrepareAreaTraversal,
    MapPrepareBarriers,
    MapPrepareWmoInstances,
    MapPrepareDoodads,
    MapPrepareEntities,
    MapPreparePreload,
    CameraUpdateVehicles,
    CameraUpdateTarget,
    CameraUpdateCollision,
    CameraUpdateLiquid,
    MapPrepareAreaReplay,
    AsyncLoadPoll,
    AsyncLoadWait,
    CharacterCompose,
    CharacterTexturePublish,
    TextureDecode,
    LuaGcStep,
    TerrainSurfacePreparation,
    TerrainWeatherConstants,
    TerrainShadows,
    TerrainChunkSinglePass,
    TerrainChunkSolid,
    TerrainChunkSetup,
    TerrainLayerAllocation,
    TerrainAlphaBuild,
    TerrainD3dStateSync,
    TerrainD3dSubmit,
    TerrainDetailDoodads,
    TerrainDetailCreation,
    TerrainStreamingBuffers,
    ResidentFogFloor,
    GroundMist,
    FogGroundFloorUpload,
    D3dTextureUpload,
    D3dBufferLock,
    M2WorkerWait,
    CharacterPublicationLock,
    MovementIdle,
    MoveUnitUpdates,
    MovementMapRelink,
    CullSort,
    CullOcclusionQuery,
    CullOcclusionWrite,
    SurfaceLightScan,
    Count
};

struct ZoneLine {
    const char* name;
    double selfMs;   // average per frame, excluding nested zones
    double totalMs;  // average per frame, including nested zones
    double calls;    // average per frame
    double selfDraws;  // draw calls made directly in this zone (not in a nested one), average per frame
};

enum class M2DrawType : uint32_t {
    Model, Projected, Doodad, Ribbon, Particle, Callback, Count
};

// Position bins in yards: [0, .002), [.002, .01), [.01, .05), [.05, +inf), and incomparable.
inline constexpr uint32_t MovementDisplacementBins = 5;
struct MovementTotals {
    double units;
    double changed;
    double relinks;
    double positionChanged;
    double facingChanged;
    double pitchChanged;
    double rayTriangleTests;
    double linkTriangleTests;
    double linkBspFaces;
    double candidateHit;
    double candidateMiss;
    double candidateDeferred;
    double candidateUnsupportedRay;
    double candidateSmallLeaf;
    double displacement[MovementDisplacementBins];
    double relinkDisplacement[MovementDisplacementBins];
};

enum class M2AnimationStage : uint32_t { Parallel, Serial, Bones, Texture, Count };
inline constexpr uint32_t M2AnimationStages = static_cast<uint32_t>(M2AnimationStage::Count);

enum class M2BatchOpportunity : uint32_t {
    OrdinaryDraw, SameMeshTextures, TransformOnly, DoodadDraw, RigidDoodadSplit, Count
};
inline constexpr uint32_t M2BatchOpportunities = static_cast<uint32_t>(M2BatchOpportunity::Count);

struct Summary {
    double frameMs;      // average time between frames
    double fps;
    double drawCalls;    // average per frame
    double triangles;    // average per frame
    double m2DrawsByType[static_cast<uint32_t>(M2DrawType::Count)];
    uint32_t frames;     // frames averaged
    uint32_t gpuRenderSamples;
    double gpuRenderMs;  // GPU timestamp span averaged over sampled frames
    ZoneLine zones[Count];
    MovementTotals movement;
    double d3dBufferLocks;
    double d3dBufferMappedBytes;
    double m2AnimationCalls[M2AnimationStages];
    double m2RepeatedModels[M2AnimationStages];
    uint32_t m2MaxRepeats[M2AnimationStages];
    double m2AnimationDropped;
    double cullCameraSamples;
    double cullCameraComparisons;
    double cullCameraUnchanged;
    double m2BatchCounts[M2BatchOpportunities];
};

struct Scope {
    explicit Scope(Id zone);
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    int32_t Zone() const { return m_zone; }

private:
    friend Id UiDetailZone(Id detail, Id parent);
    friend Id ProbeChildZone(Id zone, Id parent);
    int32_t m_zone;  // -1: inactive
    uint64_t m_start;
    uint64_t m_child;
    Scope* m_parent;
};

// Tags backend draws made by an M2 element. Nested element callbacks restore their caller's tag.
struct M2DrawScope {
    explicit M2DrawScope(M2DrawType type);
    ~M2DrawScope();
    M2DrawScope(const M2DrawScope&) = delete;
    M2DrawScope& operator=(const M2DrawScope&) = delete;

private:
    M2DrawType m_previous;
};

// Main-thread diagnostics for WXL_PROFILE_SPIKES=1. Returns an inactive zone when off.
Id SpikeZone(Id zone);
// Terrain child timing is scoped to its passes. Work probes also support periodic profiling.
Id ProbeChildZone(Id zone, Id parent);
Id WorkZone(Id zone);

struct MovementWorkScope {
    struct Counters {
        uint32_t units;
        uint32_t changed;
        uint32_t relinks;
        uint32_t positionChanged;
        uint32_t facingChanged;
        uint32_t pitchChanged;
        uint32_t rayTriangleTests;
        uint32_t linkTriangleTests;
        uint32_t linkBspFaces;
        uint32_t candidateHit;
        uint32_t candidateMiss;
        uint32_t candidateDeferred;
        uint32_t candidateUnsupportedRay;
        uint32_t candidateSmallLeaf;
        uint32_t displacement[MovementDisplacementBins];
        uint32_t relinkDisplacement[MovementDisplacementBins];
        void ObserveDisplacement(double squared, bool relinked);
    };
    MovementWorkScope();
    ~MovementWorkScope();
    MovementWorkScope(const MovementWorkScope&) = delete;
    MovementWorkScope& operator=(const MovementWorkScope&) = delete;
    static Counters* Active() { return s_active; }

private:
    static thread_local Counters* s_active;
    Counters m_counters{};
    bool m_collecting = false;
};

void CountD3dBufferLock(uint32_t bytes);
void CountM2Animation(const void* model, M2AnimationStage stage);
void ObserveCullCamera(const C3Vector& position, const C44Matrix& view, const C44Matrix& projection);
bool M2BatchProbeEnabled();
void CountM2BatchOpportunity(M2BatchOpportunity kind);
bool M2DiagnosticsEnabled();
bool UiDetailEnabled();
Id UiDetailZone(Id detail, Id parent);
const char* M2DrawTypeName(M2DrawType type);

// One draw call of `primitives` triangles (or primitives of any kind; the device passes what it draws).
void CountDraw(uint32_t primitives);

// Marks the end of a frame (call once per presented frame).
void EndFrame();

// GPU queries report asynchronously. Use the original CPU frame ID when the result is ready.
bool GpuTimingEnabled();
uint64_t CurrentFrameId();
void RecordGpuRenderTime(uint64_t frameId, double milliseconds);

// Averages the most recent `frameCount` frames (at most the 240 kept). Returns how many frames it used; 0 means
// no frame has been recorded yet.
uint32_t Summarize(uint32_t frameCount, Summary& out);

bool Enabled();
void SetEnabled(bool enabled);

// Forgets all recorded frames.
void Reset();

const char* ZoneName(uint32_t id);

}  // namespace Profiler

#endif
