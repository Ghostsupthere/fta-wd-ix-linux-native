#include "Engine/Source/Base/Profiler.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
    "World update", "UI idle", "UI update", "UI render"
};

struct FrameRecord {
    uint64_t self[Count];
    uint64_t total[Count];
    uint32_t calls[Count];
    uint32_t selfDraws[Count];
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
    {
        std::fprintf(stderr, " %s %.2f/%.2f ms", s.zones[i].name, s.zones[i].selfMs, s.zones[i].totalMs);
        if (s.zones[i].selfDraws >= 0.5) std::fprintf(stderr, " (%.0f draws)", s.zones[i].selfDraws);
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

void CountDraw(uint32_t primitives) {
    if (!g_enabled.load(std::memory_order_relaxed) || !OnOwnerThread()) return;
    ++g_open.draws;
    g_open.triangles += primitives;
    if (g_current) ++g_open.selfDraws[g_current->Zone()];
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
#if defined(WOWDECOMP_LINUX)
    if (!g_samplerResolved) StartSampler();
    if (g_dumpRequested) DumpSamples();
#endif
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
    double self[Count] = {}, total[Count] = {}, calls[Count] = {}, selfDraws[Count] = {};
    for (uint32_t k = 0; k < frameCount; ++k) {
        const FrameRecord& r = g_ring[(g_next + kKeptFrames - 1 - k) % kKeptFrames];
        frameNs += static_cast<double>(r.frameNs);
        draws += r.draws;
        triangles += static_cast<double>(r.triangles);
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
    out.triangles = triangles / n;
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
    g_count = 0;
    g_next = 0;
    g_lastFrameEnd = 0;
    std::memset(&g_open, 0, sizeof(g_open));
    g_current = nullptr;
}

const char* ZoneName(uint32_t id) { return id < Count ? kNames[id] : ""; }

}  // namespace Profiler
