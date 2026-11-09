#ifndef ENGINE_SOURCE_MODEL2_M2PARTICLEJOBS_H
#define ENGINE_SOURCE_MODEL2_M2PARTICLEJOBS_H

#include <cstdint>

#include "Engine/Source/Tempest/C3Vector.h"
#include "Engine/Source/Tempest/C44Matrix.h"

class CParticleEmitter2;

// Native Linux: runs particle emitter updates (CParticleEmitter2::Update, the simulation step) on the model worker
// threads. The original updates every emitter one after another on the main thread inside CM2Model::AnimateST.
//
// How it fits: CM2Scene::Animate opens a window with Begin() before its AnimateST pass. While the window is open,
// CM2Model::AnimateParticleST hands each eligible emitter to Defer() instead of updating it in place. End() then runs
// the queued updates, in parallel when it pays, and returns once all are done, before anything reads the particles.
//
// Why this is safe and gives the same result as the serial order:
//  - An emitter's update touches only that emitter (and its child emitters): its particle arrays, its own random seed,
//    its transform. The queued job carries copies of the transform, view position and frame of reference, so a later
//    model in the same pass cannot change what it reads.
//  - Emitters that are not self-contained (particle models, terrain projection) are never queued
//    (CParticleEmitter2::CanUpdateOffThread), and update in place as before.
//  - Nothing in the AnimateST pass reads an emitter's particles between Defer() and End().
// Switch: WXL_PARTICLE_THREADS=0 disables the queue (every emitter updates in place).
namespace M2ParticleJobs {

// Runs `work(param)` on every available thread and returns when all of them have returned. `work` must be called
// once per participating thread and share nothing but `param`. A null dispatcher runs the jobs on the caller alone.
typedef void (*Dispatcher)(void* context, void (*work)(void*), void* param);

// True while Begin()'s window is open.
bool Active();

// Opens the window. `allowThreads` is false when the worker pool is off, so jobs are queued but run on the caller.
void Begin(bool allowThreads);

// Queues one update. Returns false (nothing queued) when no window is open or the emitter must update in place.
bool Defer(CParticleEmitter2* emitter, float deltaTime, const C44Matrix& transform, const C3Vector& viewPos,
           const C44Matrix* frameOfReference);

// Runs the queued updates and closes the window. Returns the number of updates run.
uint32_t End(Dispatcher dispatcher, void* context);

// Minimum queued updates before End() uses the dispatcher; below this the thread hand-off costs more than it saves.
constexpr uint32_t kMinParallelJobs = 8;

struct Stats {
    uint64_t frames;           // windows closed
    uint64_t parallelFrames;   // of those, run through the dispatcher
    uint64_t jobs;             // updates run from the queue
    uint64_t inlineUpdates;    // eligible-looking updates that ran in place because a window was closed or ineligible
};
Stats GetStats();
void ResetStats();

// True when WXL_PROFILE_LOG is set. The timing below costs two clock reads per update, so it runs only then.
bool LoggingEnabled();
uint64_t NowNs();

// Adds the wall time of the bone-animation phase of one CM2Scene::Animate to the log line.
void NoteBonePhase(uint64_t ns);

// Times an emitter update that runs in place on the main thread (the original path, WXL_PARTICLE_THREADS=0, and the
// emitters that are never queued), so the log shows its cost next to the queued run.
class InlineTimer {
  public:
    explicit InlineTimer(const CParticleEmitter2* emitter);
    ~InlineTimer();

  private:
    const CParticleEmitter2* m_emitter;
    uint64_t m_start;
};

// Diagnostics for WXL_PROFILE_LOG: counts a CM2Scene::Animate call and how it split its models, and prints one line
// to stderr every WXL_PROFILE_LOG seconds (nothing when it is unset).
void NoteAnimate(uint32_t rootModels, uint32_t workerCount, bool animationUsedWorkers);

}  // namespace M2ParticleJobs

#endif
