#include "Engine/Source/Model2/M2ParticleJobs.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "Engine/Source/Model2/M2ParticleBuild.h"
#include "Engine/Source/Services/ParticleSystem2.h"

// All queue state belongs to the main thread: Begin, Defer and End are called from CM2Scene::Animate and the
// AnimateST pass it runs. Only RunChunk() runs elsewhere, and it touches nothing here except the job array it is given
// and the atomic timing counters.
namespace M2ParticleJobs {
namespace {

struct Job {
    CParticleEmitter2* emitter;
    float deltaTime;
    C44Matrix transform;
    C3Vector viewPos;
    C44Matrix frameOfReference;
    bool hasFrame;
};

struct Run {
    Job* jobs;
    uint32_t count;
    std::atomic<uint32_t> next;
};

constexpr uint32_t kChunk = 2;  // emitters vary a lot in cost, so hand them out a couple at a time

std::vector<Job> g_jobs;
uint32_t g_depth = 0;
bool g_allowThreads = false;
Stats g_stats = {};

// Timing for WXL_PROFILE_LOG. Counters are summed over the log interval and reset when the line is printed.
// The atomic ones are written by worker threads.
struct Timing {
    std::atomic<uint64_t> busyNs{0};         // time inside Update, summed over every thread (queued updates)
    std::atomic<uint64_t> longestJobNs{0};   // the single slowest queued update seen
    std::atomic<uint64_t> liveAfter{0};      // live particles after each queued update
    uint64_t runWallNs = 0;                  // wall time of End()'s run of the queue
    uint64_t passWallNs = 0;                 // wall time from Begin to the end of End (the AnimateST pass plus the run)
    uint64_t inlineNs = 0;                   // main-thread updates that ran in place
    uint64_t inlineCount = 0;
    uint64_t inlineLive = 0;
    uint64_t boneNs = 0;                     // bone-animation phase
    uint64_t passStartNs = 0;
};
Timing g_timing;

bool Enabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("WXL_PARTICLE_THREADS");
        return !(value && value[0] == '0');
    }();
    return enabled;
}

uint32_t LiveParticles(const CParticleEmitter2* emitter) {
    return emitter->m_liveList.Count();
}

void RunOne(Job& job) {
    if (!LoggingEnabled()) {
        job.emitter->Update(job.deltaTime, job.transform, job.viewPos, job.hasFrame ? &job.frameOfReference : nullptr);
        return;
    }

    const uint64_t start = NowNs();
    job.emitter->Update(job.deltaTime, job.transform, job.viewPos, job.hasFrame ? &job.frameOfReference : nullptr);
    const uint64_t took = NowNs() - start;

    g_timing.busyNs.fetch_add(took, std::memory_order_relaxed);
    g_timing.liveAfter.fetch_add(LiveParticles(job.emitter), std::memory_order_relaxed);

    uint64_t longest = g_timing.longestJobNs.load(std::memory_order_relaxed);
    while (took > longest && !g_timing.longestJobNs.compare_exchange_weak(longest, took, std::memory_order_relaxed)) {
    }
}

void RunChunk(void* param) {
    Run* run = static_cast<Run*>(param);

    for (;;) {
        const uint32_t first = run->next.fetch_add(kChunk, std::memory_order_relaxed);

        if (first >= run->count) {
            break;
        }

        const uint32_t last = first + kChunk < run->count ? first + kChunk : run->count;

        for (uint32_t i = first; i < last; ++i) {
            RunOne(run->jobs[i]);
        }
    }
}

}  // namespace

bool LoggingEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("WXL_PROFILE_LOG");
        return value && std::atof(value) > 0.0;
    }();
    return enabled;
}

uint64_t NowNs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

void NoteBonePhase(uint64_t ns) {
    g_timing.boneNs += ns;
}

InlineTimer::InlineTimer(const CParticleEmitter2* emitter) : m_emitter(emitter), m_start(LoggingEnabled() ? NowNs() : 0) {
}

InlineTimer::~InlineTimer() {
    if (m_start) {
        g_timing.inlineNs += NowNs() - m_start;
        ++g_timing.inlineCount;
        g_timing.inlineLive += LiveParticles(m_emitter);
    }
}

bool Active() {
    return g_depth != 0;
}

void Begin(bool allowThreads) {
    if (g_depth++ == 0) {
        g_allowThreads = allowThreads;
        g_jobs.clear();

        if (LoggingEnabled()) {
            g_timing.passStartNs = NowNs();
        }
    }
}

bool Defer(CParticleEmitter2* emitter, float deltaTime, const C44Matrix& transform, const C3Vector& viewPos,
           const C44Matrix* frameOfReference) {
    if (g_depth == 0 || !Enabled()) {
        return false;
    }

    if (!emitter->CanUpdateOffThread()) {
        ++g_stats.inlineUpdates;
        return false;
    }

    Job job;
    job.emitter = emitter;
    job.deltaTime = deltaTime;
    job.transform = transform;
    job.viewPos = viewPos;
    job.hasFrame = frameOfReference != nullptr;

    if (frameOfReference) {
        job.frameOfReference = *frameOfReference;
    }

    g_jobs.push_back(job);
    return true;
}

uint32_t End(Dispatcher dispatcher, void* context) {
    if (g_depth == 0 || --g_depth != 0) {
        return 0;
    }

    const uint32_t count = static_cast<uint32_t>(g_jobs.size());
    const bool timed = LoggingEnabled();
    ++g_stats.frames;

    if (count) {
        const uint64_t runStart = timed ? NowNs() : 0;

        if (dispatcher && g_allowThreads && count >= kMinParallelJobs) {
            Run run;
            run.jobs = g_jobs.data();
            run.count = count;
            run.next.store(0, std::memory_order_relaxed);
            dispatcher(context, RunChunk, &run);
            ++g_stats.parallelFrames;
        } else {
            // Queue order is the original update order.
            for (uint32_t i = 0; i < count; ++i) {
                RunOne(g_jobs[i]);
            }
        }

        if (timed) {
            g_timing.runWallNs += NowNs() - runStart;
        }

        g_stats.jobs += count;
    }

    if (timed && g_timing.passStartNs) {
        g_timing.passWallNs += NowNs() - g_timing.passStartNs;
    }

    g_jobs.clear();
    return count;
}

Stats GetStats() {
    return g_stats;
}

void ResetStats() {
    g_stats = Stats();
}

void NoteAnimate(uint32_t rootModels, uint32_t workerCount, bool animationUsedWorkers) {
    static double intervalSeconds = -1.0;
    static std::chrono::steady_clock::time_point last;
    static uint64_t frames = 0;
    static uint64_t animationParallel = 0;
    static uint64_t rootTotal = 0;
    static Stats lastStats = {};

    if (intervalSeconds < 0.0) {
        const char* value = std::getenv("WXL_PROFILE_LOG");
        intervalSeconds = value ? std::atof(value) : 0.0;
        last = std::chrono::steady_clock::now();
    }

    if (intervalSeconds <= 0.0) {
        return;
    }

    ++frames;
    rootTotal += rootModels;

    if (animationUsedWorkers) {
        ++animationParallel;
    }

    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

    if (std::chrono::duration<double>(now - last).count() < intervalSeconds) {
        return;
    }

    const Stats stats = g_stats;
    const uint64_t windows = stats.frames - lastStats.frames;
    const uint64_t parallelWindows = stats.parallelFrames - lastStats.parallelFrames;
    const uint64_t jobs = stats.jobs - lastStats.jobs;
    const uint64_t inlineUpdates = stats.inlineUpdates - lastStats.inlineUpdates;
    const double perFrame = frames ? 1.0 / static_cast<double>(frames) : 0.0;
    const double ms = 1.0e-6;

    std::fprintf(stderr,
                 "WarcraftXL threads: workers %u, animate on workers %llu of %llu frames (avg %.0f root models), "
                 "particles %s: %llu of %llu frames parallel, avg %.1f queued updates/frame, %llu updates left in place\n",
                 workerCount, static_cast<unsigned long long>(animationParallel),
                 static_cast<unsigned long long>(frames), frames ? static_cast<double>(rootTotal) / frames : 0.0,
                 Enabled() ? "on" : "off (WXL_PARTICLE_THREADS=0)", static_cast<unsigned long long>(parallelWindows),
                 static_cast<unsigned long long>(windows), windows ? static_cast<double>(jobs) / windows : 0.0,
                 static_cast<unsigned long long>(inlineUpdates));

    const double queuedWall = g_timing.runWallNs * ms * perFrame;
    const double queuedBusy = g_timing.busyNs.load() * ms * perFrame;
    const double inPlace = g_timing.inlineNs * ms * perFrame;
    const double liveQueued = jobs ? static_cast<double>(g_timing.liveAfter.load()) / jobs : 0.0;
    const double liveInPlace = g_timing.inlineCount ? static_cast<double>(g_timing.inlineLive) / g_timing.inlineCount : 0.0;

    // "update total" is what the particle simulation costs the frame: the queue's wall time plus whatever ran in place.
    // For a like-for-like comparison, run once with WXL_PARTICLE_THREADS=0 (everything is in place) and once without.
    std::fprintf(stderr,
                 "WarcraftXL particle update, ms per frame: total %.3f = queued wall %.3f + in place %.3f | "
                 "queued busy (all threads) %.3f, speedup %.2fx, longest single update %.3f ms | "
                 "live particles per update: queued %.0f, in place %.0f (%llu in-place updates/frame) | "
                 "AnimateST pass incl. queue %.3f, bone animation %.3f\n",
                 queuedWall + inPlace, queuedWall, inPlace, queuedBusy, queuedWall > 0.0 ? queuedBusy / queuedWall : 0.0,
                 g_timing.longestJobNs.load() * ms, liveQueued, liveInPlace,
                 static_cast<unsigned long long>(frames ? g_timing.inlineCount / frames : 0),
                 g_timing.passWallNs * ms * perFrame, g_timing.boneNs * ms * perFrame);

    M2ParticleBuild::LogAndReset(frames);

    last = now;
    frames = 0;
    animationParallel = 0;
    rootTotal = 0;
    lastStats = stats;
    g_timing.busyNs.store(0);
    g_timing.longestJobNs.store(0);
    g_timing.liveAfter.store(0);
    g_timing.runWallNs = 0;
    g_timing.passWallNs = 0;
    g_timing.inlineNs = 0;
    g_timing.inlineCount = 0;
    g_timing.inlineLive = 0;
    g_timing.boneNs = 0;
}

}  // namespace M2ParticleJobs
