#ifndef ENGINE_SOURCE_MODEL2_M2PARTICLEBUILD_H
#define ENGINE_SOURCE_MODEL2_M2PARTICLEBUILD_H

#include <cstdint>

#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Gx/Types.h"
#include "Engine/Source/Model2/M2ParticleJobs.h"
#include "Engine/Source/Services/ParticleSystem2.h"
#include "Engine/Source/Tempest/C44Matrix.h"

// Native Linux: builds the vertices of particle emitters ahead of drawing, on the model worker threads.
//
// The original builds an emitter's quads (CParticleEmitter2::BuildVertex, interpolation, spin) inside the draw call
// for that emitter, straight into a locked GPU vertex buffer, one emitter after another on the main thread. In a busy
// scene that is a large part of the model draw time.
//
// How it fits: at the end of CM2Scene::Animate, once the draw elements exist, Prebuild() works out which emitters will
// be drawn, builds all of their vertices in parallel into one scratch arena, and keeps the results. When
// CParticleEmitter2::RenderParticles later has a buffer to fill, Apply() copies the prebuilt bytes in instead of
// building them, and sets the emitter's bounds and rendered count exactly as the build would have.
//
// Why the output is the same as building in place: a block is used only if it was built for the same view matrix,
// frame of reference, vertex format, particle count and live-particle count that the draw has. Any difference (another
// camera, a second pass with a different view, an emitter changed since) means no block is applied and the draw
// builds the vertices itself, as before. Blocks are dropped when the emitter is destroyed. Each emitter is built by
// exactly one thread, and the per-build transforms and sort queue are thread_local in ParticleSystem2.cpp.
// Switch: WXL_PARTICLE_BUILD_THREADS=0 turns it off.
namespace M2ParticleBuild {

struct Item {
    CParticleEmitter2* emitter;
    const C44Matrix* frameOfReference;  // the model's frame of reference at draw time, or null
};

// Builds blocks for `items` as seen from `view` (the matrix RenderParticles will read from the device) and replaces
// whatever was kept for `scene` before. Runs the builds through `dispatcher` when there is enough work.
// Returns the number of blocks built.
uint32_t Prebuild(const void* scene, const Item* items, uint32_t itemCount, const C44Matrix& view,
                  M2ParticleJobs::Dispatcher dispatcher, void* context, bool allowThreads);

// Copies the block built for `emitter` into `buffer` and fills the emitter's bounds and rendered count; sets
// `vertex.m_count`. Returns false, changing nothing, when there is no block or it does not match this draw.
bool Apply(CParticleEmitter2* emitter, const C44Matrix* frameOfReference, const C44Matrix& view, EGxVertexBufferFormat format,
           uint32_t count, void* buffer, CParticleEmitter2::ParticleVertex& vertex);

// Drops the block for an emitter that is being destroyed.
void Forget(CParticleEmitter2* emitter);

// Drops every block kept for `scene`.
void Clear(const void* scene);

bool Enabled();

// Prebuild runs only when the blocks add up to at least this many particles; under it the hand-off costs more.
constexpr uint32_t kMinParticles = 256;

struct Stats {
    uint64_t prebuilds;       // Prebuild calls that built blocks
    uint64_t blocks;          // blocks built
    uint64_t particles;       // particles in them
    uint64_t applied;         // blocks used at draw time
    uint64_t rejected;        // a block existed but did not match the draw
    uint64_t missed;          // no block for the emitter (built in place)
};
Stats GetStats();
void ResetStats();

// For the WXL_PROFILE_LOG line: prints the counters and timings gathered since the last call, per frame, then resets.
void LogAndReset(uint64_t frames);

}  // namespace M2ParticleBuild

#endif
