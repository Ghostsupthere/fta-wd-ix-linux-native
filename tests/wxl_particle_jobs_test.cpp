// Particle emitter updates on worker threads (Engine/Source/Model2/M2ParticleJobs.*).
//
// The claim to prove: queueing emitter updates and running them on several threads leaves every emitter in exactly the
// state the original serial Update() calls leave it in. Each emitter owns its random seed and arrays, so the result
// must match bit for bit. The test also checks the queue's rules (window, eligibility, threshold, nesting).
//
// Build with ThreadSanitizer to check the emitter code for data races as well:
//   see the command in the header of tools/run-particle-jobs-test.sh
#include "Engine/Source/Model2/M2ParticleJobs.h"
#include "Engine/Source/Services/ParticleSystem2.h"

#include <atomic>
#include <chrono>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <cstdlib>
#include <thread>
#include <vector>

namespace {

constexpr uint32_t kSeedBase = 0x1234;

struct Config {
    int kind;  // 0 plane, 1 sphere, 2 spline
    float rate, rateVariation, life, lifeVariation, speed, variation, gravity, drag;
    uint32_t flags;
    bool child;
};

// The engine allocates emitters with SMemAlloc and a constructor that leaves some fields (m_parentVelocity,
// m_burstOffset, m_xform) unset, so a fresh object holds whatever the heap had. Zeroed memory makes two emitters
// start identical, which is what a bit-for-bit comparison needs.
struct EmitterDeleter {
    void operator()(CParticleEmitter2* e) const {
        e->~CParticleEmitter2();
        std::free(e);
    }
};
typedef std::unique_ptr<CParticleEmitter2, EmitterDeleter> EmitterPtr;

template <class T>
T* NewZeroed() {
    return new (std::calloc(1, sizeof(T))) T();
}

EmitterPtr MakeEmitter(const Config& c, uint32_t index) {
    EmitterPtr e;

    if (c.kind == 0) {
        auto* plane = NewZeroed<CPlaneParticleEmitter>();
        plane->SetWidth(1.5f);
        plane->SetHeight(0.75f);
        plane->SetLatitude(0.6f);
        plane->SetLongitude(3.1f);
        e.reset(plane);
    } else if (c.kind == 1) {
        auto* sphere = NewZeroed<CSphereParticleEmitter>();
        sphere->SetWidth(2.0f);
        sphere->SetHeight(0.5f);
        sphere->SetLatitude(1.1f);
        sphere->SetLongitude(6.0f);
        e.reset(sphere);
    } else {
        auto* spline = NewZeroed<CSplineParticleEmitter>();
        const C3Vector points[4] = {C3Vector(0.0f, 0.0f, 0.0f), C3Vector(1.0f, 0.5f, 0.0f), C3Vector(2.0f, -0.5f, 1.0f), C3Vector(3.0f, 0.0f, 0.5f)};
        spline->SetSpline(points, 4);
        spline->SetLatitude(0.4f);
        spline->SetLongitude(1.0f);
        e.reset(spline);
    }

    e->m_random.SetSeed(kSeedBase + index * 7919u);  // the constructor seeds from rand(); both runs must match
    e->m_params.m_emissionRate = c.rate;
    e->m_params.m_emissionRateVariation = c.rateVariation;
    e->m_params.m_life = c.life;
    e->m_params.m_lifeVariation = c.lifeVariation;
    e->m_params.m_speed = c.speed;
    e->m_params.m_variation = c.variation;
    e->m_params.m_gravity = c.gravity;
    e->m_drag = c.drag;
    e->m_flags |= c.flags | 0x1;  // the constructor sets 0x6; 0x1 is "emitting"
    return e;
}

C44Matrix FrameMatrix(uint32_t frame, uint32_t index) {
    const float t = frame * 0.05f + index * 0.3f;
    const float c = std::cos(t);
    const float s = std::sin(t);
    return C44Matrix(c, s, 0.0f, 0.0f, -s, c, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 5.0f * c + index, 5.0f * s, 0.25f * frame, 1.0f);
}

// A dispatcher that works like CM2Cache::BeginThread/WaitThread: n helper threads plus the caller.
struct ThreadPool {
    uint32_t helpers;
    std::atomic<uint32_t> sessions{0};
};

void ThreadDispatch(void* context, void (*work)(void*), void* param) {
    ThreadPool* pool = static_cast<ThreadPool*>(context);
    std::vector<std::thread> threads;

    for (uint32_t i = 0; i < pool->helpers; ++i) {
        threads.emplace_back(work, param);
    }

    work(param);

    for (auto& t : threads) {
        t.join();
    }

    pool->sessions.fetch_add(1);
}

bool SameBits(float a, float b) {
    return std::memcmp(&a, &b, sizeof(float)) == 0;
}

bool SameVec(const C3Vector& a, const C3Vector& b) {
    return SameBits(a.x, b.x) && SameBits(a.y, b.y) && SameBits(a.z, b.z);
}

bool SameEmitter(CParticleEmitter2& a, CParticleEmitter2& b, const char** why) {
    if (a.m_liveList.Count() != b.m_liveList.Count()) { *why = "live count"; return false; }
    if (a.m_freeList.Count() != b.m_freeList.Count()) { *why = "free count"; return false; }
    if (a.m_particles.Count() != b.m_particles.Count()) { *why = "particle count"; return false; }
    if (a.m_flags != b.m_flags) { *why = "flags"; return false; }
    if (!SameBits(a.m_emitAccumulator, b.m_emitAccumulator)) { *why = "accumulator"; return false; }
    if (!SameVec(a.m_viewPos, b.m_viewPos) || !SameVec(a.m_parentPosition, b.m_parentPosition)) { *why = "positions"; return false; }
    if (std::memcmp(&a.m_xform, &b.m_xform, sizeof(C44Matrix)) != 0) { *why = "xform"; return false; }

    for (uint32_t i = 0; i < a.m_liveList.Count(); ++i) {
        if (a.m_liveList[i] != b.m_liveList[i]) { *why = "live index"; return false; }
        const CParticle2& p = a.m_particles[a.m_liveList[i]];
        const CParticle2& q = b.m_particles[b.m_liveList[i]];
        if (!SameBits(p.m_age, q.m_age) || !SameVec(p.m_position, q.m_position) || !SameVec(p.m_velocity, q.m_velocity) || p.m_randomSeed != q.m_randomSeed || std::memcmp(&p.m_lifeSeed, &q.m_lifeSeed, sizeof(p.m_lifeSeed)) != 0) {
            std::fprintf(stderr, "  age %g/%g pos (%g %g %g)/(%g %g %g) vel (%g %g %g)/(%g %g %g) seed %u/%u life %d/%d\n", p.m_age, q.m_age,
                         p.m_position.x, p.m_position.y, p.m_position.z, q.m_position.x, q.m_position.y, q.m_position.z,
                         p.m_velocity.x, p.m_velocity.y, p.m_velocity.z, q.m_velocity.x, q.m_velocity.y, q.m_velocity.z,
                         p.m_randomSeed, q.m_randomSeed, p.m_lifeSeed.m_value, q.m_lifeSeed.m_value);
            *why = "particle state";
            return false;
        }
    }

    for (uint32_t i = 0; i < a.m_freeList.Count(); ++i) {
        if (a.m_freeList[i] != b.m_freeList[i]) { *why = "free index"; return false; }
    }

    return true;
}

std::vector<Config> BuildConfigs(uint32_t count) {
    std::vector<Config> configs;

    for (uint32_t i = 0; i < count; ++i) {
        Config c = {};
        c.kind = static_cast<int>(i % 3);
        c.rate = 40.0f + 25.0f * (i % 5);
        c.rateVariation = (i % 4) * 3.0f;
        c.life = 0.8f + 0.2f * (i % 6);
        c.lifeVariation = (i % 2) ? 0.3f : 0.0f;  // exercises both StepUpdate branches
        c.speed = 1.0f + (i % 7) * 0.5f;
        c.variation = 0.2f;
        c.gravity = (i % 3) * 1.5f;
        c.drag = (i % 5 == 0) ? 0.4f : 0.0f;
        c.flags = (i % 4 == 1) ? 0x80000 : 0;  // follow offsets
        c.child = false;
        configs.push_back(c);
    }

    return configs;
}

void TestEquivalence(uint32_t emitterCount, uint32_t helpers, bool allowThreads, const char* label) {
    const std::vector<Config> configs = BuildConfigs(emitterCount);
    std::vector<EmitterPtr> serial;
    std::vector<EmitterPtr> queued;

    for (uint32_t i = 0; i < emitterCount; ++i) {
        serial.push_back(MakeEmitter(configs[i], i));
        queued.push_back(MakeEmitter(configs[i], i));
        // Sync() sizes the arrays on the first update that emits; both sets start empty.
    }

    ThreadPool pool;
    pool.helpers = helpers;
    const C3Vector view(0.0f, 0.0f, 2.0f);
    C44Matrix frame(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 3.0f, -2.0f, 0.5f, 1.0f);

    for (uint32_t f = 0; f < 90; ++f) {
        const float dt = (f % 11 == 10) ? 0.35f : 0.016f;  // a long frame takes InternalUpdate's multi-step path

        for (uint32_t i = 0; i < emitterCount; ++i) {
            const C44Matrix m = FrameMatrix(f, i);
            serial[i]->Update(dt, m, view, (i % 3 == 0) ? &frame : nullptr);
        }

        M2ParticleJobs::Begin(allowThreads);
        for (uint32_t i = 0; i < emitterCount; ++i) {
            const C44Matrix m = FrameMatrix(f, i);
            // The frame of reference changes after the call, as a later model in the pass could change it.
            C44Matrix scratch = frame;
            const bool queuedOk = M2ParticleJobs::Defer(queued[i].get(), dt, m, view, (i % 3 == 0) ? &scratch : nullptr);
            assert(queuedOk);
            scratch = C44Matrix();
            (void)queuedOk;
        }
        const uint32_t ran = M2ParticleJobs::End(&ThreadDispatch, &pool);
        assert(ran == emitterCount);
        (void)ran;

        for (uint32_t i = 0; i < emitterCount; ++i) {
            const char* why = "";
            if (!SameEmitter(*serial[i], *queued[i], &why)) {
                std::fprintf(stderr, "%s: emitter %u differs after frame %u (%s); live %u vs %u\n", label, i, f, why,
                             serial[i]->m_liveList.Count(), queued[i]->m_liveList.Count());
                assert(false);
            }
        }
    }

    uint32_t liveTotal = 0;

    for (uint32_t i = 0; i < emitterCount; ++i) {
        const char* why = "";
        if (!SameEmitter(*serial[i], *queued[i], &why)) {
            std::fprintf(stderr, "%s: emitter %u differs (%s)\n", label, i, why);
            assert(false);
        }
        liveTotal += serial[i]->m_liveList.Count();
    }

    if (liveTotal <= emitterCount) {
        std::fprintf(stderr, "%s: only %u live particles across %u emitters\n", label, liveTotal, emitterCount);
    }
    assert(liveTotal > emitterCount);  // the run produced real particles, not empty emitters
    std::printf("%s: %u emitters, %u live particles at the end, identical\n", label, emitterCount, liveTotal);
}

void TestRules() {
    // No window: nothing queues.
    {
        auto e = MakeEmitter(BuildConfigs(1)[0], 0);
        const C44Matrix m = FrameMatrix(0, 0);
        assert(!M2ParticleJobs::Active());
        assert(!M2ParticleJobs::Defer(e.get(), 0.016f, m, C3Vector(0, 0, 0), nullptr));
    }

    // Ineligible emitters (terrain projection flag, or a child that has it) stay in place and are counted.
    {
        M2ParticleJobs::ResetStats();
        Config c = BuildConfigs(1)[0];
        auto projected = MakeEmitter(c, 0);
        projected->m_flags |= 0x40000;
        assert(!projected->CanUpdateOffThread());

        auto parent = MakeEmitter(c, 1);
        auto child = MakeEmitter(c, 2);
        child->m_flags |= 0x40000;
        parent->m_childEmitters[0] = child.get();
        parent->m_childEmitterCount = 1;
        assert(!parent->CanUpdateOffThread());
        parent->m_childEmitterCount = 0;
        assert(parent->CanUpdateOffThread());

        auto modelEmitter = MakeEmitter(c, 3);
        modelEmitter->m_params.m_hasModel = CParticleEmitter2::PT_MODEL;
        assert(!modelEmitter->CanUpdateOffThread());

        const C44Matrix m = FrameMatrix(0, 0);
        M2ParticleJobs::Begin(true);
        assert(M2ParticleJobs::Active());
        assert(!M2ParticleJobs::Defer(projected.get(), 0.016f, m, C3Vector(0, 0, 0), nullptr));
        assert(!M2ParticleJobs::Defer(modelEmitter.get(), 0.016f, m, C3Vector(0, 0, 0), nullptr));
        assert(M2ParticleJobs::Defer(parent.get(), 0.016f, m, C3Vector(0, 0, 0), nullptr));
        assert(M2ParticleJobs::End(nullptr, nullptr) == 1);
        assert(!M2ParticleJobs::Active());
        assert(M2ParticleJobs::GetStats().inlineUpdates == 2);
        assert(M2ParticleJobs::GetStats().parallelFrames == 0);  // one job, and no dispatcher
        parent->m_childEmitterCount = 0;
    }

    // The dispatcher is used only at or above the threshold, and only when threads are allowed.
    {
        M2ParticleJobs::ResetStats();
        const std::vector<Config> configs = BuildConfigs(M2ParticleJobs::kMinParallelJobs);
        ThreadPool pool;
        pool.helpers = 2;

        for (int pass = 0; pass < 3; ++pass) {
            const uint32_t n = pass == 0 ? M2ParticleJobs::kMinParallelJobs - 1 : M2ParticleJobs::kMinParallelJobs;
            std::vector<EmitterPtr> list;
            M2ParticleJobs::Begin(pass != 2);
            for (uint32_t i = 0; i < n; ++i) {
                list.push_back(MakeEmitter(configs[i], i));
                assert(M2ParticleJobs::Defer(list.back().get(), 0.016f, FrameMatrix(0, i), C3Vector(0, 0, 0), nullptr));
            }
            assert(M2ParticleJobs::End(&ThreadDispatch, &pool) == n);
        }

        assert(pool.sessions.load() == 1);  // pass 1 only
        assert(M2ParticleJobs::GetStats().frames == 3 && M2ParticleJobs::GetStats().parallelFrames == 1);
    }

    // Nested windows: only the outermost End runs the queue.
    {
        auto e = MakeEmitter(BuildConfigs(1)[0], 0);
        M2ParticleJobs::Begin(false);
        M2ParticleJobs::Begin(false);
        assert(M2ParticleJobs::Defer(e.get(), 0.016f, FrameMatrix(0, 0), C3Vector(0, 0, 0), nullptr));
        assert(M2ParticleJobs::End(nullptr, nullptr) == 0);
        assert(M2ParticleJobs::Active());
        assert(M2ParticleJobs::End(nullptr, nullptr) == 1);
        assert(!M2ParticleJobs::Active());
    }

    std::printf("queue rules: ok\n");
}

// Informational (run with "bench"): the same emitter set updated serially and through the queue on helper threads.
void Benchmark(uint32_t emitterCount, uint32_t helpers) {
    std::vector<Config> configs = BuildConfigs(emitterCount);
    for (Config& c : configs) {
        c.rate *= 6.0f;  // a busy scene: a few hundred live particles per emitter
    }

    std::vector<EmitterPtr> serial;
    std::vector<EmitterPtr> queued;

    for (uint32_t i = 0; i < emitterCount; ++i) {
        serial.push_back(MakeEmitter(configs[i], i));
        queued.push_back(MakeEmitter(configs[i], i));
    }

    ThreadPool pool;
    pool.helpers = helpers;
    const C3Vector view(0.0f, 0.0f, 2.0f);
    typedef std::chrono::steady_clock Clock;
    double serialMs = 0.0;
    double queuedMs = 0.0;
    const uint32_t frames = 240;

    for (uint32_t f = 0; f < frames; ++f) {
        Clock::time_point t0 = Clock::now();
        for (uint32_t i = 0; i < emitterCount; ++i) {
            serial[i]->Update(0.008f, FrameMatrix(f, i), view, nullptr);
        }
        Clock::time_point t1 = Clock::now();
        M2ParticleJobs::Begin(true);
        for (uint32_t i = 0; i < emitterCount; ++i) {
            M2ParticleJobs::Defer(queued[i].get(), 0.008f, FrameMatrix(f, i), view, nullptr);
        }
        M2ParticleJobs::End(&ThreadDispatch, &pool);
        Clock::time_point t2 = Clock::now();

        if (f >= 40) {  // let the particle counts settle
            serialMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
            queuedMs += std::chrono::duration<double, std::milli>(t2 - t1).count();
        }
    }

    uint32_t live = 0;
    for (uint32_t i = 0; i < emitterCount; ++i) live += serial[i]->m_liveList.Count();
    const double n = frames - 40;
    std::printf("bench: %u emitters (%u live particles), %u helper threads + caller: serial %.3f ms/frame, queued %.3f ms/frame (%.2fx)\n",
                emitterCount, live, helpers, serialMs / n, queuedMs / n, serialMs / queuedMs);
    std::printf("       (the queued time includes creating threads each frame; the engine reuses a standing pool, so it is a pessimistic figure)\n");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "bench") == 0) {
        for (uint32_t n : {16u, 48u, 150u}) {
            Benchmark(n, 5);
        }
        return 0;
    }

    TestRules();
    TestEquivalence(24, 3, true, "parallel, 3 helpers");
    TestEquivalence(40, 5, true, "parallel, 5 helpers");
    TestEquivalence(24, 3, false, "threads not allowed (serial queue)");
    TestEquivalence(5, 3, true, "below threshold");
    std::printf("wxl_particle_jobs_test: ok\n");
    return 0;
}
