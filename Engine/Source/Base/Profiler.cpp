#include "Engine/Source/Base/Profiler.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace Profiler {
namespace {

constexpr uint32_t kKeptFrames = 240;
constexpr const char* kNames[Count] = {"World render", "World scene", "Terrain", "M2 animate", "M2 draw", "Present"};

struct FrameRecord {
    uint64_t self[Count];
    uint64_t total[Count];
    uint32_t calls[Count];
    uint32_t draws;
    uint64_t triangles;
    uint64_t frameNs;
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
std::thread::id g_owner;
bool g_haveOwner = false;
Scope* g_current = nullptr;
FrameRecord g_open = {};       // the frame being recorded
FrameRecord g_ring[kKeptFrames] = {};
uint32_t g_count = 0;          // frames stored (<= kKeptFrames)
uint32_t g_next = 0;           // ring slot for the next frame
uint64_t g_lastFrameEnd = 0;
uint64_t g_logIntervalNs = 0;
uint64_t g_lastLog = 0;
bool g_logResolved = false;

bool OnOwnerThread() {
    if (!g_haveOwner) {
        g_owner = std::this_thread::get_id();
        g_haveOwner = true;
    }
    return g_owner == std::this_thread::get_id();
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
    for (uint32_t i = 0; i < Count; ++i)
        std::fprintf(stderr, " %s %.2f/%.2f ms", s.zones[i].name, s.zones[i].selfMs, s.zones[i].totalMs);
    std::fprintf(stderr, "\n");
}

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

void CountDraw(uint32_t primitives) {
    if (!g_enabled.load(std::memory_order_relaxed) || !OnOwnerThread()) return;
    ++g_open.draws;
    g_open.triangles += primitives;
}

void EndFrame() {
    if (!g_enabled.load(std::memory_order_relaxed) || !OnOwnerThread()) return;
    const uint64_t now = NowNs();
    if (g_lastFrameEnd) {
        g_open.frameNs = now - g_lastFrameEnd;
        g_ring[g_next] = g_open;
        g_next = (g_next + 1) % kKeptFrames;
        if (g_count < kKeptFrames) ++g_count;
    }
    g_lastFrameEnd = now;
    std::memset(&g_open, 0, sizeof(g_open));
    if (!g_logResolved) ResolveLog();
    if (g_logIntervalNs && now - g_lastLog >= g_logIntervalNs) {
        g_lastLog = now;
        LogSummary();
    }
}

uint32_t Summarize(uint32_t frameCount, Summary& out) {
    std::memset(&out, 0, sizeof(out));
    for (uint32_t i = 0; i < Count; ++i) out.zones[i].name = kNames[i];
    if (!g_count || !frameCount) return 0;
    if (frameCount > g_count) frameCount = g_count;
    double frameNs = 0.0, draws = 0.0, triangles = 0.0;
    double self[Count] = {}, total[Count] = {}, calls[Count] = {};
    for (uint32_t k = 0; k < frameCount; ++k) {
        const FrameRecord& r = g_ring[(g_next + kKeptFrames - 1 - k) % kKeptFrames];
        frameNs += static_cast<double>(r.frameNs);
        draws += r.draws;
        triangles += static_cast<double>(r.triangles);
        for (uint32_t i = 0; i < Count; ++i) {
            self[i] += static_cast<double>(r.self[i]);
            total[i] += static_cast<double>(r.total[i]);
            calls[i] += r.calls[i];
        }
    }
    const double n = static_cast<double>(frameCount);
    out.frames = frameCount;
    out.frameMs = frameNs / n * 1.0e-6;
    out.fps = out.frameMs > 0.0 ? 1000.0 / out.frameMs : 0.0;
    out.drawCalls = draws / n;
    out.triangles = triangles / n;
    for (uint32_t i = 0; i < Count; ++i) {
        out.zones[i].selfMs = self[i] / n * 1.0e-6;
        out.zones[i].totalMs = total[i] / n * 1.0e-6;
        out.zones[i].calls = calls[i] / n;
    }
    return frameCount;
}

bool Enabled() { return g_enabled.load(std::memory_order_relaxed); }

void SetEnabled(bool enabled) {
    g_enabled.store(enabled, std::memory_order_relaxed);
    if (!enabled) Reset();
}

void Reset() {
    g_count = 0;
    g_next = 0;
    g_lastFrameEnd = 0;
    std::memset(&g_open, 0, sizeof(g_open));
    g_current = nullptr;
}

const char* ZoneName(uint32_t id) { return id < Count ? kNames[id] : ""; }

}  // namespace Profiler
