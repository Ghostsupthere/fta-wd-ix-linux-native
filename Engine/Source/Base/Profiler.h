#ifndef ENGINE_SOURCE_BASE_PROFILER_H
#define ENGINE_SOURCE_BASE_PROFILER_H

#include <cstdint>

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
};

struct Scope {
    explicit Scope(Id zone);
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    int32_t Zone() const { return m_zone; }

private:
    friend Id UiDetailZone(Id detail, Id parent);
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
