#include "Engine/Source/Base/Profiler.h"
#include "Engine/Source/Tempest/C3Vector.h"
#include "Engine/Source/Tempest/C44Matrix.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <mutex>
#include <thread>

#if defined(WOWDECOMP_LINUX)
#include <execinfo.h>
#include <pthread.h>
#include <signal.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#endif

namespace Profiler {
namespace {

constexpr uint32_t kKeptFrames = 240;
constexpr const char* kNames[Count] = {
    "World render", "World scene", "Terrain", "WMO draw", "Weather",
    "M2 animate", "M2 draw", "Present", "Paint", "Client idle",
    "World update", "UI idle", "UI update", "UI render", "M2 transparent sort",
    "UI layout", "UI frame updates", "UI OnUpdate scripts", "UI batch build", "UI textures", "UI text", "UI callbacks",
    "UI callback world frame", "UI callback unit model", "UI callback other model", "UI callback minimap",
    "UI callback quest POI", "UI callback scroll", "UI callback movie", "UI callback model effects", "UI callback other",
    "World frame update", "Player name update", "World text render",
    "World update camera", "World update day-night and housing", "World update audio",
    "World update object effects", "World update portraits", "World update object displays", "World update spells",
    "World update map", "World update area of interest", "World update scene", "World update map preparation",
    "World update smooth facing",
    "Map prepare WMO resources", "Map prepare areas", "Map prepare area queues", "Map prepare area traversal",
    "Map prepare barriers", "Map prepare WMO instances", "Map prepare doodads", "Map prepare entities", "Map prepare preload",
    "Camera update vehicles", "Camera update target", "Camera update collision", "Camera update liquid",
    "Map prepare area replay", "Async load callbacks", "Async load wait",
    "Character composition", "Character texture publication", "Texture decode", "Lua GC step",
    "Terrain surface preparation", "Terrain weather constants", "Terrain resident shadows",
    "Terrain single pass", "Terrain solid", "Terrain chunk setup", "Terrain layer allocation", "Terrain alpha build",
    "Terrain D3D9 state sync", "Terrain D3D9 submit", "Terrain detail doodads", "Terrain detail creation",
    "Terrain streaming buffers", "Resident fog floor", "Ground mist",
    "Fog ground floor upload", "D3D9 texture upload", "D3D9 buffer lock", "M2 worker wait", "Character publication lock",
    "Movement idle", "Unit movement updates", "Movement map relinks", "Cull sort", "Cull occlusion queries",
    "Cull occlusion writes", "Surface light scan",
    "Static lamp scan", "Liquid draw", "Modern water preparation", "Water glint selection", "Modern water composite", "Water reflection preparation",
    "Particle vertex prebuild", "Particle vertex inline build", "Network event poll"
};

constexpr uint32_t kM2DrawTypes = static_cast<uint32_t>(M2DrawType::Count);
constexpr const char* kM2DrawNames[kM2DrawTypes] = {
    "model", "projected", "doodad", "ribbon", "particle", "callback"
};
thread_local M2DrawType g_m2DrawType = M2DrawType::Count;

struct FrameRecord {
    uint64_t self[Count];
    uint64_t total[Count];
    uint32_t calls[Count];
    uint32_t selfDraws[Count];
    uint32_t draws;
    uint32_t m2DrawsByType[kM2DrawTypes];
    uint64_t triangles;
    uint64_t frameNs;
    uint64_t frameId;
    double gpuRenderMs;
    bool gpuRenderSample;
    MovementWorkScope::Counters movement;
    uint32_t d3dBufferLocks;
    uint64_t d3dBufferMappedBytes;
    uint32_t m2AnimationCalls[M2AnimationStages];
    uint32_t m2RepeatedModels[M2AnimationStages];
    uint32_t m2MaxRepeats[M2AnimationStages];
    uint32_t m2AnimationDropped;
    bool cullCameraSample;
    bool cullCameraComparison;
    bool cullCameraUnchanged;
    uint32_t staticCullPasses;
    uint32_t staticCullHits;
    uint32_t staticCullMisses;
    uint32_t staticCullProjectionHits;
    uint64_t characterWorkerNs;
    uint32_t characterWorkerRequests;
    uint32_t m2BatchCounts[M2BatchOpportunities];
};

uint64_t NowNs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

bool EnvEnabled() {
    const char* value = std::getenv("WXL_PROFILER");
    return !(value && value[0] == '0');
}

// All state belongs to the thread that first made a Scope or called EndFrame (the render thread).
std::atomic<bool> g_enabled{EnvEnabled()};
Scope* g_current = nullptr;
FrameRecord g_open = {};       // the frame being recorded
FrameRecord g_ring[kKeptFrames] = {};
uint32_t g_count = 0;          // frames stored (<= kKeptFrames)
uint32_t g_next = 0;           // ring slot for the next frame
uint64_t g_lastFrameEnd = 0;
uint64_t g_frameId = 1;
uint64_t g_logIntervalNs = 0;
uint64_t g_lastLog = 0;
bool g_logResolved = false;

constexpr uint32_t kSpikeHistoryFrames = 63;
constexpr uint32_t kSpikeWarmupFrames = 9;
constexpr uint64_t kSpikeLogIntervalNs = 250'000'000;
uint64_t g_lastSpikeLog = 0;
uint32_t g_suppressedSpikes = 0;

bool SpikeLoggingEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("WXL_PROFILE_SPIKES");
        return value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}

bool PeriodicLoggingEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("WXL_PROFILE_LOG");
        return value && std::atof(value) > 0.0;
    }();
    return enabled;
}

bool BufferWritesEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("WXL_PROFILE_BUFFER_WRITES");
        return PeriodicLoggingEnabled() && value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}

bool M2AnimationEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("WXL_PROFILE_M2_ANIMATION");
        return PeriodicLoggingEnabled() && value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}

// Workers write to this table. Take and clear the sample when the owner presents.
constexpr uint32_t kAnimationSlots = 8192;
constexpr uint32_t kAnimationProbes = 32;
struct AnimationEntry {
    const void* model;
    uint32_t calls[M2AnimationStages];
};
std::mutex g_animationMutex;
AnimationEntry g_animation[kAnimationSlots] = {};
uint32_t g_animationTouched[kAnimationSlots];
uint32_t g_animationUsed = 0;
uint32_t g_animationDropped = 0;

void FinishAnimationFrame(FrameRecord* frame) {
    if (!M2AnimationEnabled()) return;
    std::lock_guard<std::mutex> lock(g_animationMutex);
    for (uint32_t i = 0; i < g_animationUsed; ++i) {
        auto& entry = g_animation[g_animationTouched[i]];
        if (frame) {
            for (uint32_t stage = 0; stage < M2AnimationStages; ++stage) {
                const uint32_t calls = entry.calls[stage];
                frame->m2AnimationCalls[stage] += calls;
                if (calls > 1) {
                    ++frame->m2RepeatedModels[stage];
                    frame->m2MaxRepeats[stage] = std::max(frame->m2MaxRepeats[stage], calls - 1);
                }
            }
        }
        entry = {};
    }
    if (frame) frame->m2AnimationDropped = g_animationDropped;
    g_animationUsed = 0;
    g_animationDropped = 0;
}

bool CullStabilityEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("WXL_PROFILE_CULL_STABILITY");
        return PeriodicLoggingEnabled() && value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}

bool PortWorkEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("WXL_PROFILE_PORT_WORK");
        return value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}

std::mutex g_characterWorkerLock;
uint64_t g_characterWorkerNs = 0;
uint32_t g_characterWorkerRequests = 0;

void TakeCharacterWorkerSample(FrameRecord* frame) {
    if (!PortWorkEnabled()) return;
    std::lock_guard<std::mutex> lock(g_characterWorkerLock);
    if (frame) {
        frame->characterWorkerNs = g_characterWorkerNs;
        frame->characterWorkerRequests = g_characterWorkerRequests;
    }
    g_characterWorkerNs = 0;
    g_characterWorkerRequests = 0;
}

uint64_t SpikeBaseline() {
    if (g_count < kSpikeWarmupFrames) return 0;
    // Use an odd number of prior frames so the median is an observed frame time.
    uint32_t count = std::min(g_count, kSpikeHistoryFrames);
    if (count % 2 == 0) --count;
    uint64_t times[kSpikeHistoryFrames];
    for (uint32_t i = 0; i < count; ++i) {
        times[i] = g_ring[(g_next + kKeptFrames - 1 - i) % kKeptFrames].frameNs;
    }
    std::nth_element(times, times + count / 2, times + count);
    return times[count / 2];
}

void LogSpike(const FrameRecord& frame, uint64_t medianNs) {
    // Bound the cost when a loading screen or scheduler stall yields a burst.
    if (g_lastSpikeLog && g_lastFrameEnd - g_lastSpikeLog < kSpikeLogIntervalNs) {
        ++g_suppressedSpikes;
        return;
    }
    g_lastSpikeLog = g_lastFrameEnd;
    const uint64_t start = NowNs();
    char line[128 * (Count + 2)];
    int used = std::snprintf(line, sizeof(line),
        "WarcraftXL frame spike: frame=%llu %.3f ms, median=%.3f ms, draws=%u, suppressed=%u | self/total ms (calls)",
        static_cast<unsigned long long>(frame.frameId), frame.frameNs * 1.0e-6, medianNs * 1.0e-6,
        frame.draws, g_suppressedSpikes);
    for (uint32_t i = 0; i < Count && used > 0 && static_cast<size_t>(used) < sizeof(line) - 2; ++i) {
        if (!frame.calls[i]) continue;
        used += std::snprintf(line + used, sizeof(line) - used, " | %s %.3f/%.3f (%u)",
            kNames[i], frame.self[i] * 1.0e-6, frame.total[i] * 1.0e-6, frame.calls[i]);
    }
    if (frame.calls[MoveUnitUpdates] && used > 0 && static_cast<size_t>(used) < sizeof(line)) {
        const auto& work = frame.movement;
        const double perUnitUs = work.units ? frame.total[MoveUnitUpdates] * 1.0e-3 / work.units : 0.0;
        std::snprintf(line + used, sizeof(line) - used,
            " | movement units=%u changed=%u relinks=%u rayTris=%u linkTris=%u bspFaces=%u position=%u facing=%u pitch=%u us/unit=%.2f "
            "under002=%u under01=%u under05=%u atLeast05=%u incomparable=%u "
            "relinkUnder002=%u relinkUnder01=%u relinkUnder05=%u relinkAtLeast05=%u relinkIncomparable=%u "
            "candidateHit=%u candidateMiss=%u candidateDeferred=%u candidateUnsupportedRay=%u candidateSmallLeaf=%u",
            work.units, work.changed, work.relinks, work.rayTriangleTests, work.linkTriangleTests,
            work.linkBspFaces, work.positionChanged, work.facingChanged, work.pitchChanged, perUnitUs,
            work.displacement[0], work.displacement[1], work.displacement[2], work.displacement[3], work.displacement[4],
            work.relinkDisplacement[0], work.relinkDisplacement[1], work.relinkDisplacement[2],
            work.relinkDisplacement[3], work.relinkDisplacement[4],
            work.candidateHit, work.candidateMiss, work.candidateDeferred, work.candidateUnsupportedRay, work.candidateSmallLeaf);
    }
    std::fprintf(stderr, "%s\n", line);
    // Keep this synchronous write from triggering a spike on the next frame.
    g_lastFrameEnd += NowNs() - start;
    g_suppressedSpikes = 0;
}

bool OnOwnerThread() {
    static const std::thread::id owner = std::this_thread::get_id();
    return owner == std::this_thread::get_id();
}

void ResolveLog() {
    g_logResolved = true;
    const char* value = std::getenv("WXL_PROFILE_LOG");
    const double seconds = value ? std::atof(value) : 0.0;
    g_logIntervalNs = seconds > 0.0 ? static_cast<uint64_t>(seconds * 1.0e9) : 0;
}

void LogSummary() {
    Summary s;
    if (!Summarize(120, s)) return;
    std::fprintf(stderr, "WarcraftXL profiler: %.1f fps, frame %.2f ms, %.0f draws, %.0f tris over %u frames |",
                 s.fps, s.frameMs, s.drawCalls, s.triangles, s.frames);
    const uint32_t zoneCount = UiDetailEnabled() ? AsyncLoadPoll : UiLayout;
    for (uint32_t i = 0; i < zoneCount; ++i)
    {
        const int precision = (i == M2TransparentSort || i >= UiLayout) ? 3 : 2;
        std::fprintf(stderr, " %s %.*f/%.*f ms", s.zones[i].name,
            precision, s.zones[i].selfMs, precision, s.zones[i].totalMs);
        if (i >= UiCallbackWorld) std::fprintf(stderr, " (%.2f calls)", s.zones[i].calls);
        if (s.zones[i].selfDraws >= 0.5) std::fprintf(stderr, " (%.0f draws)", s.zones[i].selfDraws);
    }
    if (s.zones[MovementIdle].calls || s.zones[CullSort].calls || s.zones[SurfaceLightScan].calls) {
        std::fprintf(stderr, " | work probes");
        for (uint32_t i = MovementIdle; i <= SurfaceLightScan; ++i) {
            const auto& zone = s.zones[i];
            if (zone.calls) std::fprintf(stderr, " | %s %.3f/%.3f ms (%.2f calls)", zone.name,
                zone.selfMs, zone.totalMs, zone.calls);
        }
        const auto& work = s.movement;
        const double perUnitUs = work.units ? s.zones[MoveUnitUpdates].totalMs * 1000 / work.units : 0.0;
        std::fprintf(stderr, " | movement/frame units=%.2f changed=%.2f relinks=%.2f rayTris=%.2f linkTris=%.2f bspFaces=%.2f position=%.2f facing=%.2f pitch=%.2f us/unit=%.2f "
            "under002=%.2f under01=%.2f under05=%.2f atLeast05=%.2f incomparable=%.2f "
            "relinkUnder002=%.2f relinkUnder01=%.2f relinkUnder05=%.2f relinkAtLeast05=%.2f relinkIncomparable=%.2f "
            "candidateHit=%.2f candidateMiss=%.2f candidateDeferred=%.2f candidateUnsupportedRay=%.2f candidateSmallLeaf=%.2f",
            work.units, work.changed, work.relinks, work.rayTriangleTests, work.linkTriangleTests,
            work.linkBspFaces, work.positionChanged, work.facingChanged, work.pitchChanged, perUnitUs,
            work.displacement[0], work.displacement[1], work.displacement[2], work.displacement[3], work.displacement[4],
            work.relinkDisplacement[0], work.relinkDisplacement[1], work.relinkDisplacement[2],
            work.relinkDisplacement[3], work.relinkDisplacement[4],
            work.candidateHit, work.candidateMiss, work.candidateDeferred, work.candidateUnsupportedRay, work.candidateSmallLeaf);
    }
    for (uint32_t i = StaticLampScan; i <= WaterReflectionPrepare; ++i) {
        const auto& zone = s.zones[i];
        if (zone.calls) std::fprintf(stderr, " | %s %.3f/%.3f ms (%.2f calls)",
            zone.name, zone.selfMs, zone.totalMs, zone.calls);
    }
    if (PortWorkEnabled()) {
        for (const Id id : {CharacterCompose, CharacterTexturePublish, CharacterPublicationLock, TextureDecode,
                            D3dTextureUpload, ParticleVertexPrebuild, ParticleVertexInline, NetworkPoll}) {
            const auto& zone = s.zones[id];
            if (zone.calls) std::fprintf(stderr, " | %s %.3f/%.3f ms (%.2f calls)",
                zone.name, zone.selfMs, zone.totalMs, zone.calls);
        }
        std::fprintf(stderr, " | Character worker %.3f/%.3f ms (%.2f calls)",
            s.characterWorkerMs, s.characterWorkerMs, s.characterWorkerRequests);
    }
    if (BufferWritesEnabled()) {
        const auto& zone = s.zones[D3dBufferLock];
        std::fprintf(stderr, " | %s %.3f/%.3f ms (%.2f calls)",
            zone.name, zone.selfMs, zone.totalMs, zone.calls);
        std::fprintf(stderr, " | D3D9 buffer/frame locks=%.2f mappedBytes=%.2f",
            s.d3dBufferLocks, s.d3dBufferMappedBytes);
    }
    if (M2AnimationEnabled()) {
        constexpr const char* stages[M2AnimationStages] = {"mt", "st", "bones", "texture"};
        std::fprintf(stderr, " | M2 animation/frame dropped=%.2f", s.m2AnimationDropped);
        for (uint32_t i = 0; i < M2AnimationStages; ++i) {
            std::fprintf(stderr, " %sCalls=%.2f %sRepeated=%.2f %sMaxRepeats=%u",
                stages[i], s.m2AnimationCalls[i], stages[i], s.m2RepeatedModels[i], stages[i], s.m2MaxRepeats[i]);
        }
    }
    if (CullStabilityEnabled()) {
        std::fprintf(stderr, " | cull camera/frame samples=%.3f compared=%.3f unchanged=%.3f",
            s.cullCameraSamples, s.cullCameraComparisons, s.cullCameraUnchanged);
    }
    if (s.staticCullPasses) {
        std::fprintf(stderr, " | static cull/frame passes=%.2f hits=%.2f misses=%.2f projectedHits=%.2f",
            s.staticCullPasses, s.staticCullHits, s.staticCullMisses, s.staticCullProjectionHits);
    }
    if (M2BatchProbeEnabled()) {
        constexpr const char* names[M2BatchOpportunities] = {
            "ordinary", "sameMeshTextures", "transformOnly", "doodad", "rigidDoodadSplit"
        };
        std::fprintf(stderr, " | M2 batching/frame");
        for (uint32_t i = 0; i < M2BatchOpportunities; ++i) {
            std::fprintf(stderr, " %s=%.2f", names[i], s.m2BatchCounts[i]);
        }
    }
    if (M2DiagnosticsEnabled()) {
        std::fprintf(stderr, " | M2 draws/frame");
        for (uint32_t i = 0; i < kM2DrawTypes; ++i) {
            std::fprintf(stderr, " %s=%.2f", kM2DrawNames[i], s.m2DrawsByType[i]);
        }
    }
    if (GpuTimingEnabled()) {
        if (s.gpuRenderSamples) {
            std::fprintf(stderr, " | GPU render span %.3f ms (%u samples)", s.gpuRenderMs, s.gpuRenderSamples);
        } else {
            std::fprintf(stderr, " | GPU render span unavailable (0 samples)");
        }
    }
    std::fprintf(stderr, "\n");
}

#if defined(WOWDECOMP_LINUX)
// WXL_SAMPLE_PROFILE=<hz> (opt-in): samples the render thread's call stack by CPU time, tagged with the profiler
// zone it was in, into a ring of the last ~40,000 samples. `kill -USR1 <pid>` writes them to Logs/samples.txt at the
// end of the next frame; tools/symbolize-samples.py turns that into a per-function report. The handler uses
// backtrace(), which is not async-signal-safe (it can block if the thread was inside the dynamic loader), so this
// is a diagnostic only.
constexpr uint32_t kSampleDepth = 24;
constexpr uint32_t kSampleCap = 40000;

struct Sample {
    int16_t zone;
    int16_t depth;
    void* pc[kSampleDepth];
};

Sample* g_samples = nullptr;
volatile uint32_t g_sampleCount = 0;
volatile sig_atomic_t g_dumpRequested = 0;
bool g_samplerResolved = false;
uint32_t g_sampleHz = 0;

void OnSample(int, siginfo_t*, void*) {
    if (!g_samples) return;
    Sample& sample = g_samples[g_sampleCount % kSampleCap];
    sample.zone = g_current ? static_cast<int16_t>(g_current->Zone()) : static_cast<int16_t>(-1);
    sample.depth = static_cast<int16_t>(backtrace(sample.pc, kSampleDepth));
    g_sampleCount = g_sampleCount + 1;
}

void OnDumpSignal(int) { g_dumpRequested = 1; }

void StartSampler() {
    g_samplerResolved = true;
    const char* value = std::getenv("WXL_SAMPLE_PROFILE");
    const long hz = value ? std::atol(value) : 0;
    if (hz <= 0) return;
    g_sampleHz = static_cast<uint32_t>(hz > 5000 ? 5000 : hz);
    g_samples = static_cast<Sample*>(std::calloc(kSampleCap, sizeof(Sample)));
    if (!g_samples) return;
    void* warm[4];
    backtrace(warm, 4);  // loads the unwinder before the first signal

    struct sigaction sample = {};
    sample.sa_sigaction = OnSample;
    sample.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sample.sa_mask);
    sigaction(SIGRTMIN, &sample, nullptr);
    struct sigaction dump = {};
    dump.sa_handler = OnDumpSignal;
    sigemptyset(&dump.sa_mask);
    sigaction(SIGUSR1, &dump, nullptr);

    clockid_t clock;
    if (pthread_getcpuclockid(pthread_self(), &clock) != 0) return;
    struct sigevent event = {};
    event.sigev_notify = SIGEV_THREAD_ID;
    event.sigev_signo = SIGRTMIN;
    event._sigev_un._tid = static_cast<int>(syscall(SYS_gettid));
    timer_t timer;
    if (timer_create(clock, &event, &timer) != 0) return;
    struct itimerspec spec = {};
    spec.it_interval.tv_nsec = 1000000000L / g_sampleHz;
    spec.it_value = spec.it_interval;
    timer_settime(timer, 0, &spec, nullptr);
    std::fprintf(stderr, "WarcraftXL profiler: sampling the render thread at %u Hz; kill -USR1 %d writes Logs/samples.txt\n",
                 g_sampleHz, static_cast<int>(getpid()));
}

void DumpSamples() {
    g_dumpRequested = 0;
    FILE* file = std::fopen("Logs/samples.txt", "w");
    if (!file) return;
    std::fprintf(file, "# zones");
    for (uint32_t i = 0; i < Count; ++i) std::fprintf(file, " %u=%s", i, kNames[i]);
    std::fprintf(file, "\n");
    if (FILE* maps = std::fopen("/proc/self/maps", "r")) {
        char line[1024];
        while (std::fgets(line, sizeof(line), maps)) {
            unsigned long long start = 0, end = 0, offset = 0;
            char perms[8] = {}, path[900] = {};
            if (std::sscanf(line, "%llx-%llx %7s %llx %*s %*s %899[^\n]", &start, &end, perms, &offset, path) >= 4 &&
                path[0] == '/')
                std::fprintf(file, "M %llx %llx %llx %s\n", start, end, offset, path);
        }
        std::fclose(maps);
    }
    const uint32_t count = g_sampleCount < kSampleCap ? g_sampleCount : kSampleCap;
    std::fprintf(file, "# samples %u at %u Hz\n", count, g_sampleHz);
    for (uint32_t i = 0; i < count; ++i) {
        const Sample& sample = g_samples[i];
        std::fprintf(file, "S %d", sample.zone);
        for (int k = 0; k < sample.depth; ++k) std::fprintf(file, " %llx", reinterpret_cast<unsigned long long>(sample.pc[k]));
        std::fprintf(file, "\n");
    }
    std::fclose(file);
    std::fprintf(stderr, "WarcraftXL profiler: wrote %u samples to Logs/samples.txt\n", count);
}
#endif

}  // namespace

Scope::Scope(Id zone) : m_zone(-1), m_start(0), m_child(0), m_parent(nullptr) {
    if (!g_enabled.load(std::memory_order_relaxed) || zone >= Count || !OnOwnerThread()) return;
    m_zone = static_cast<int32_t>(zone);
    m_start = NowNs();
    m_parent = g_current;
    g_current = this;
}

Scope::~Scope() {
    if (m_zone < 0) return;
    const uint64_t elapsed = NowNs() - m_start;
    const uint64_t self = elapsed > m_child ? elapsed - m_child : 0;
    g_open.self[m_zone] += self;
    g_open.total[m_zone] += elapsed;
    ++g_open.calls[m_zone];
    g_current = m_parent;
    if (m_parent) m_parent->m_child += elapsed;
}

bool UiDetailEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("WXL_PROFILE_UI_DETAIL");
        return value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}

Id UiDetailZone(Id detail, Id parent) {
    if (!UiDetailEnabled() || !Enabled() || !OnOwnerThread()) return Count;
    for (const auto* scope = g_current; scope; scope = scope->m_parent) {
        const auto zone = scope->Zone();
        // Nested scroll batches are already included in the outer callback group.
        if (detail == UiCallbacks && zone == static_cast<int32_t>(UiCallbacks)) return Count;
        if (zone == static_cast<int32_t>(parent)) return detail;
    }
    return Count;
}

Id SpikeZone(Id zone) {
    return SpikeLoggingEnabled() ? zone : Count;
}

Id ProbeChildZone(Id zone, Id parent) {
    if (!SpikeLoggingEnabled() || !Enabled()) return Count;
    for (const Scope* scope = OnOwnerThread() ? g_current : nullptr; scope; scope = scope->m_parent) {
        if (scope->Zone() == static_cast<int32_t>(parent)) return zone;
    }
    return Count;
}

Id WorkZone(Id zone) {
    return SpikeLoggingEnabled() || PeriodicLoggingEnabled() ? zone : Count;
}

Id PortWorkZone(Id zone) {
    return PortWorkEnabled() ? WorkZone(zone) : SpikeZone(zone);
}

CharacterWorkerScope::CharacterWorkerScope()
    : m_started(PortWorkEnabled() && Enabled() && PeriodicLoggingEnabled() ? NowNs() : 0) {}

CharacterWorkerScope::~CharacterWorkerScope() {
    if (!m_started || !Enabled()) return;
    const auto elapsed = NowNs() - m_started;
    std::lock_guard<std::mutex> lock(g_characterWorkerLock);
    g_characterWorkerNs += elapsed;
    ++g_characterWorkerRequests;
}

Id NightLightsZone(Id zone) {
    static const bool enabled = [] {
        const char* value = std::getenv("WXL_PROFILE_NIGHT_LIGHTS");
        return value && std::strcmp(value, "1") == 0;
    }();
    return enabled ? WorkZone(zone) : Count;
}

thread_local MovementWorkScope::Counters* MovementWorkScope::s_active = nullptr;

MovementWorkScope::MovementWorkScope() {
    m_collecting = WorkZone(MoveUnitUpdates) != Count && Enabled() && OnOwnerThread() && !s_active;
    if (m_collecting) s_active = &m_counters;
}

MovementWorkScope::~MovementWorkScope() {
    if (!m_collecting) return;
    s_active = nullptr;
    g_open.movement.units += m_counters.units;
    g_open.movement.changed += m_counters.changed;
    g_open.movement.relinks += m_counters.relinks;
    g_open.movement.positionChanged += m_counters.positionChanged;
    g_open.movement.facingChanged += m_counters.facingChanged;
    g_open.movement.pitchChanged += m_counters.pitchChanged;
    g_open.movement.rayTriangleTests += m_counters.rayTriangleTests;
    g_open.movement.linkTriangleTests += m_counters.linkTriangleTests;
    g_open.movement.linkBspFaces += m_counters.linkBspFaces;
    g_open.movement.candidateHit += m_counters.candidateHit;
    g_open.movement.candidateMiss += m_counters.candidateMiss;
    g_open.movement.candidateDeferred += m_counters.candidateDeferred;
    g_open.movement.candidateUnsupportedRay += m_counters.candidateUnsupportedRay;
    g_open.movement.candidateSmallLeaf += m_counters.candidateSmallLeaf;
    for (uint32_t i = 0; i < MovementDisplacementBins; ++i) {
        g_open.movement.displacement[i] += m_counters.displacement[i];
        g_open.movement.relinkDisplacement[i] += m_counters.relinkDisplacement[i];
    }
}

void MovementWorkScope::Counters::ObserveDisplacement(double squared, bool relinked) {
    constexpr double limits[] = {0.002 * 0.002, 0.01 * 0.01, 0.05 * 0.05};
    uint32_t bin = MovementDisplacementBins - 1;
    if (std::isfinite(squared) && squared >= 0.0) {
        bin = 0;
        while (bin < std::size(limits) && squared >= limits[bin]) ++bin;
    }
    ++displacement[bin];
    if (relinked) ++relinkDisplacement[bin];
}

Id BufferLockZone() {
    return BufferWritesEnabled() ? D3dBufferLock : SpikeZone(D3dBufferLock);
}

void CountD3dBufferLock(uint32_t bytes) {
    if (!BufferWritesEnabled() || !Enabled() || !OnOwnerThread()) return;
    ++g_open.d3dBufferLocks;
    g_open.d3dBufferMappedBytes += bytes;
}

void CountM2Animation(const void* model, M2AnimationStage stage) {
    if (!M2AnimationEnabled() || !Enabled()) return;
    uintptr_t hash = reinterpret_cast<uintptr_t>(model) >> 4;
    hash ^= hash >> 13;
    std::lock_guard<std::mutex> lock(g_animationMutex);
    for (uint32_t probe = 0; probe < kAnimationProbes; ++probe) {
        const uint32_t index = (hash + probe) & (kAnimationSlots - 1);
        auto& entry = g_animation[index];
        if (!entry.model) {
            entry.model = model;
            g_animationTouched[g_animationUsed++] = index;
        }
        if (entry.model == model) {
            ++entry.calls[static_cast<uint32_t>(stage)];
            return;
        }
    }
    ++g_animationDropped;
}

void ObserveCullCamera(int32_t mapId, const C3Vector& position, const C44Matrix& view, const C44Matrix& projection) {
    if (!CullStabilityEnabled()) return;
    const auto frameId = CurrentFrameId();
    if (!frameId || g_open.cullCameraSample) return;

    static_assert(sizeof(C3Vector) == 3 * sizeof(float));
    static_assert(sizeof(C44Matrix) == 16 * sizeof(float));
    static uint64_t previousFrame = 0;
    static int32_t previousMap = 0;
    static unsigned char previousPosition[sizeof(position)];
    static unsigned char previousView[sizeof(view)];
    static unsigned char previousProjection[sizeof(projection)];
    g_open.cullCameraSample = true;
    if (previousFrame && previousFrame + 1 == frameId && previousMap == mapId) {
        g_open.cullCameraComparison = true;
        g_open.cullCameraUnchanged = std::memcmp(&position, previousPosition, sizeof(position)) == 0 &&
            std::memcmp(&view, previousView, sizeof(view)) == 0 &&
            std::memcmp(&projection, previousProjection, sizeof(projection)) == 0;
    }
    previousFrame = frameId;
    previousMap = mapId;
    std::memcpy(previousPosition, &position, sizeof(position));
    std::memcpy(previousView, &view, sizeof(view));
    std::memcpy(previousProjection, &projection, sizeof(projection));
}

void RecordStaticCullReuse(uint32_t hits, uint32_t misses, uint32_t projectionHits) {
    if (!PeriodicLoggingEnabled() || !Enabled() || !OnOwnerThread()) return;
    ++g_open.staticCullPasses;
    g_open.staticCullHits += hits;
    g_open.staticCullMisses += misses;
    g_open.staticCullProjectionHits += projectionHits;
}

bool M2BatchProbeEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("WXL_PROFILE_M2_BATCHING");
        return PeriodicLoggingEnabled() && value && std::strcmp(value, "1") == 0;
    }();
    return enabled && Enabled();
}

void CountM2BatchOpportunity(M2BatchOpportunity kind) {
    if (OnOwnerThread()) ++g_open.m2BatchCounts[static_cast<uint32_t>(kind)];
}

bool M2DiagnosticsEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("WXL_PROFILE_M2_DRAWS");
        return value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}

const char* M2DrawTypeName(M2DrawType type) {
    const auto index = static_cast<uint32_t>(type);
    return index < kM2DrawTypes ? kM2DrawNames[index] : "";
}

M2DrawScope::M2DrawScope(M2DrawType type) : m_previous(g_m2DrawType) { g_m2DrawType = type; }
M2DrawScope::~M2DrawScope() { g_m2DrawType = m_previous; }

void CountDraw(uint32_t primitives) {
    if (!g_enabled.load(std::memory_order_relaxed) || !OnOwnerThread()) return;
    ++g_open.draws;
    const auto m2Type = static_cast<uint32_t>(g_m2DrawType);
    if (m2Type < kM2DrawTypes) ++g_open.m2DrawsByType[m2Type];
    g_open.triangles += primitives;
    if (g_current) ++g_open.selfDraws[g_current->Zone()];
}

void EndFrame() {
    if (!g_enabled.load(std::memory_order_relaxed) || !OnOwnerThread()) return;
    const uint64_t now = NowNs();
    FinishAnimationFrame(&g_open);
    TakeCharacterWorkerSample(&g_open);
    g_open.frameId = g_frameId++;
    uint64_t medianNs = 0;
    if (g_lastFrameEnd) {
        g_open.frameNs = now - g_lastFrameEnd;
        if (SpikeLoggingEnabled()) medianNs = SpikeBaseline();
        g_ring[g_next] = g_open;
        g_next = (g_next + 1) % kKeptFrames;
        if (g_count < kKeptFrames) ++g_count;
    }
    g_lastFrameEnd = now;
    if (medianNs && g_open.frameNs > 2.5 * medianNs) LogSpike(g_open, medianNs);
    std::memset(&g_open, 0, sizeof(g_open));
    if (!g_logResolved) ResolveLog();
#if defined(WOWDECOMP_LINUX)
    if (!g_samplerResolved) StartSampler();
    if (g_dumpRequested) DumpSamples();
#endif
    if (g_logIntervalNs && now - g_lastLog >= g_logIntervalNs) {
        g_lastLog = now;
        LogSummary();
    }
}

bool GpuTimingEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("WXL_PROFILE_GPU");
        return value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}

uint64_t CurrentFrameId() {
    return Enabled() && OnOwnerThread() ? g_frameId : 0;
}

void RecordGpuRenderTime(uint64_t frameId, double milliseconds) {
    const uint64_t current = CurrentFrameId();
    if (!frameId || frameId >= current || current - frameId > g_count) return;

    const uint32_t age = static_cast<uint32_t>(current - frameId);
    FrameRecord& frame = g_ring[(g_next + kKeptFrames - age) % kKeptFrames];
    if (frame.frameId == frameId) {
        frame.gpuRenderMs = milliseconds;
        frame.gpuRenderSample = true;
    }
}

uint32_t Summarize(uint32_t frameCount, Summary& out) {
    std::memset(&out, 0, sizeof(out));
    for (uint32_t i = 0; i < Count; ++i) out.zones[i].name = kNames[i];
    if (!g_count || !frameCount) return 0;
    if (frameCount > g_count) frameCount = g_count;
    double frameNs = 0.0, draws = 0.0, triangles = 0.0;
    double self[Count] = {}, total[Count] = {}, calls[Count] = {}, selfDraws[Count] = {};
    const bool m2Diagnostics = M2DiagnosticsEnabled();
    for (uint32_t k = 0; k < frameCount; ++k) {
        const FrameRecord& r = g_ring[(g_next + kKeptFrames - 1 - k) % kKeptFrames];
        frameNs += static_cast<double>(r.frameNs);
        draws += r.draws;
        for (uint32_t i = 0; i < M2BatchOpportunities; ++i) out.m2BatchCounts[i] += r.m2BatchCounts[i];
        out.cullCameraSamples += r.cullCameraSample;
        out.cullCameraComparisons += r.cullCameraComparison;
        out.cullCameraUnchanged += r.cullCameraUnchanged;
        out.staticCullPasses += r.staticCullPasses;
        out.staticCullHits += r.staticCullHits;
        out.staticCullMisses += r.staticCullMisses;
        out.staticCullProjectionHits += r.staticCullProjectionHits;
        out.characterWorkerMs += r.characterWorkerNs * 1.0e-6;
        out.characterWorkerRequests += r.characterWorkerRequests;
        out.m2AnimationDropped += r.m2AnimationDropped;
        for (uint32_t i = 0; i < M2AnimationStages; ++i) {
            out.m2AnimationCalls[i] += r.m2AnimationCalls[i];
            out.m2RepeatedModels[i] += r.m2RepeatedModels[i];
            out.m2MaxRepeats[i] = std::max(out.m2MaxRepeats[i], r.m2MaxRepeats[i]);
        }
        out.d3dBufferLocks += r.d3dBufferLocks;
        out.d3dBufferMappedBytes += r.d3dBufferMappedBytes;
        triangles += static_cast<double>(r.triangles);
        out.movement.units += r.movement.units;
        out.movement.changed += r.movement.changed;
        out.movement.relinks += r.movement.relinks;
        out.movement.positionChanged += r.movement.positionChanged;
        out.movement.facingChanged += r.movement.facingChanged;
        out.movement.pitchChanged += r.movement.pitchChanged;
        out.movement.rayTriangleTests += r.movement.rayTriangleTests;
        out.movement.linkTriangleTests += r.movement.linkTriangleTests;
        out.movement.linkBspFaces += r.movement.linkBspFaces;
        out.movement.candidateHit += r.movement.candidateHit;
        out.movement.candidateMiss += r.movement.candidateMiss;
        out.movement.candidateDeferred += r.movement.candidateDeferred;
        out.movement.candidateUnsupportedRay += r.movement.candidateUnsupportedRay;
        out.movement.candidateSmallLeaf += r.movement.candidateSmallLeaf;
        for (uint32_t i = 0; i < MovementDisplacementBins; ++i) {
            out.movement.displacement[i] += r.movement.displacement[i];
            out.movement.relinkDisplacement[i] += r.movement.relinkDisplacement[i];
        }
        if (m2Diagnostics) {
            for (uint32_t i = 0; i < kM2DrawTypes; ++i) out.m2DrawsByType[i] += r.m2DrawsByType[i];
        }
        if (r.gpuRenderSample) {
            out.gpuRenderMs += r.gpuRenderMs;
            ++out.gpuRenderSamples;
        }
        for (uint32_t i = 0; i < Count; ++i) {
            self[i] += static_cast<double>(r.self[i]);
            total[i] += static_cast<double>(r.total[i]);
            calls[i] += r.calls[i];
            selfDraws[i] += r.selfDraws[i];
        }
    }
    const double n = static_cast<double>(frameCount);
    out.frames = frameCount;
    out.frameMs = frameNs / n * 1.0e-6;
    out.fps = out.frameMs > 0.0 ? 1000.0 / out.frameMs : 0.0;
    out.drawCalls = draws / n;
    for (double& count : out.m2BatchCounts) count /= n;
    out.cullCameraSamples /= n;
    out.cullCameraComparisons /= n;
    out.cullCameraUnchanged /= n;
    out.staticCullPasses /= n;
    out.staticCullHits /= n;
    out.staticCullMisses /= n;
    out.staticCullProjectionHits /= n;
    out.characterWorkerMs /= n;
    out.characterWorkerRequests /= n;
    out.m2AnimationDropped /= n;
    for (uint32_t i = 0; i < M2AnimationStages; ++i) {
        out.m2AnimationCalls[i] /= n;
        out.m2RepeatedModels[i] /= n;
    }
    out.d3dBufferLocks /= n;
    out.d3dBufferMappedBytes /= n;
    if (m2Diagnostics) {
        for (double& count : out.m2DrawsByType) count /= n;
    }
    out.triangles = triangles / n;
    out.movement.units /= n;
    out.movement.changed /= n;
    out.movement.relinks /= n;
    out.movement.positionChanged /= n;
    out.movement.facingChanged /= n;
    out.movement.pitchChanged /= n;
    out.movement.rayTriangleTests /= n;
    out.movement.linkTriangleTests /= n;
    out.movement.linkBspFaces /= n;
    out.movement.candidateHit /= n;
    out.movement.candidateMiss /= n;
    out.movement.candidateDeferred /= n;
    out.movement.candidateUnsupportedRay /= n;
    out.movement.candidateSmallLeaf /= n;
    for (auto& bin : out.movement.displacement) bin /= n;
    for (auto& bin : out.movement.relinkDisplacement) bin /= n;
    if (out.gpuRenderSamples) out.gpuRenderMs /= out.gpuRenderSamples;
    for (uint32_t i = 0; i < Count; ++i) {
        out.zones[i].selfMs = self[i] / n * 1.0e-6;
        out.zones[i].totalMs = total[i] / n * 1.0e-6;
        out.zones[i].calls = calls[i] / n;
        out.zones[i].selfDraws = selfDraws[i] / n;
    }
    return frameCount;
}

bool Enabled() { return g_enabled.load(std::memory_order_relaxed); }

void SetEnabled(bool enabled) {
    g_enabled.store(enabled, std::memory_order_relaxed);
    if (!enabled) Reset();
}

void Reset() {
    FinishAnimationFrame(nullptr);
    TakeCharacterWorkerSample(nullptr);
    g_count = 0;
    g_next = 0;
    g_lastFrameEnd = 0;
    g_lastSpikeLog = 0;
    g_suppressedSpikes = 0;
    ++g_frameId;
    std::memset(&g_open, 0, sizeof(g_open));
    g_current = nullptr;
}

const char* ZoneName(uint32_t id) { return id < Count ? kNames[id] : ""; }

}  // namespace Profiler
