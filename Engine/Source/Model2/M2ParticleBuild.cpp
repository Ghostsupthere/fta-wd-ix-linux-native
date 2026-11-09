#include "Engine/Source/Model2/M2ParticleBuild.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

// Main-thread state except where noted. The build tasks (BuildTask) run on worker threads and touch only their own
// entry, their own slice of the arena and the atomic timing counters.
namespace M2ParticleBuild {
namespace {

struct Entry {
    CParticleEmitter2* emitter;  // null once Forget() has dropped it
    C44Matrix view;
    C44Matrix frame;
    bool hasFrame;
    EGxVertexBufferFormat format;
    uint32_t count;              // particles requested
    uint32_t live;               // live particles when the block was planned
    uint32_t stride;
    size_t offset;               // into the arena
    size_t bytes;                // reserved: count * vertices per particle * stride
    CParticleEmitter2::VertexBlock block;
    bool built;
};

struct Store {
    std::vector<Entry> entries;
    std::vector<uint8_t> arena;
    std::vector<uint32_t> table;  // open addressing over entries, by emitter pointer; 0 = empty, else index + 1
    uint32_t mask = 0;
};

struct Run {
    Entry* entries;
    uint32_t count;
    uint8_t* arena;
    std::atomic<uint32_t> next;
};

std::unordered_map<const void*, Store> g_stores;
Stats g_stats = {};
uint64_t g_wallNs = 0;                // main-thread time in Prebuild
std::atomic<uint64_t> g_busyNs{0};    // time inside BuildVertexBlock, summed over threads
uint64_t g_maxBlocks = 0;

uint32_t HashOf(const CParticleEmitter2* emitter) {
    return static_cast<uint32_t>((reinterpret_cast<uintptr_t>(emitter) >> 4) * 0x9E3779B1u);
}

void BuildTask(void* param) {
    Run* run = static_cast<Run*>(param);
    const bool timed = M2ParticleJobs::LoggingEnabled();

    for (;;) {
        const uint32_t i = run->next.fetch_add(1, std::memory_order_relaxed);

        if (i >= run->count) {
            break;
        }

        Entry& entry = run->entries[i];
        const uint64_t start = timed ? M2ParticleJobs::NowNs() : 0;
        entry.emitter->BuildVertexBlock(entry.hasFrame ? &entry.frame : nullptr, entry.view, entry.format, entry.count, run->arena + entry.offset, entry.block);
        entry.built = true;

        if (timed) {
            g_busyNs.fetch_add(M2ParticleJobs::NowNs() - start, std::memory_order_relaxed);
        }
    }
}

Entry* Find(Store& store, const CParticleEmitter2* emitter) {
    if (store.table.empty()) {
        return nullptr;
    }

    for (uint32_t slot = HashOf(emitter) & store.mask;; slot = (slot + 1) & store.mask) {
        const uint32_t value = store.table[slot];

        if (!value) {
            return nullptr;
        }

        if (store.entries[value - 1].emitter == emitter) {
            return &store.entries[value - 1];
        }
    }
}

void Insert(Store& store, uint32_t index) {
    for (uint32_t slot = HashOf(store.entries[index].emitter) & store.mask;; slot = (slot + 1) & store.mask) {
        if (!store.table[slot]) {
            store.table[slot] = index + 1;
            return;
        }
    }
}

}  // namespace

bool Enabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("WXL_PARTICLE_BUILD_THREADS");
        return !(value && value[0] == '0');
    }();
    return enabled;
}

void Clear(const void* scene) {
    g_stores.erase(scene);
}

uint32_t Prebuild(const void* scene, const Item* items, uint32_t itemCount, const C44Matrix& view,
                  M2ParticleJobs::Dispatcher dispatcher, void* context, bool allowThreads) {
    Store& store = g_stores[scene];
    store.entries.clear();
    store.table.clear();

    if (!Enabled() || !itemCount) {
        return 0;
    }

    const bool timed = M2ParticleJobs::LoggingEnabled();
    const uint64_t wallStart = timed ? M2ParticleJobs::NowNs() : 0;

    // Serial planning on the main thread: which emitters will build vertices, how many and in what format.
    uint64_t particles = 0;
    size_t arenaBytes = 0;
    store.entries.reserve(itemCount);

    // An emitter listed twice must not be built by two threads at once, nor planned twice.
    uint32_t tableSize = 16;
    while (tableSize < itemCount * 2) {
        tableSize <<= 1;
    }
    store.table.assign(tableSize, 0);
    store.mask = tableSize - 1;

    for (uint32_t i = 0; i < itemCount; ++i) {
        CParticleEmitter2* emitter = items[i].emitter;

        if (!emitter || Find(store, emitter)) {
            continue;
        }

        EGxVertexBufferFormat format;
        uint32_t count;

        if (!emitter->PlanVertexBuild(format, count)) {
            continue;
        }

        Entry entry;
        entry.emitter = emitter;
        entry.view = view;
        entry.hasFrame = items[i].frameOfReference != nullptr;

        if (entry.hasFrame) {
            entry.frame = *items[i].frameOfReference;
        }

        entry.format = format;
        entry.count = count;
        entry.live = emitter->m_liveList.Count();
        entry.stride = GxVertexSize(format);
        entry.offset = arenaBytes;
        entry.bytes = static_cast<size_t>(count) * emitter->m_vertsPerParticle * entry.stride;
        entry.built = false;
        arenaBytes += (entry.bytes + 15) & ~static_cast<size_t>(15);
        particles += count;

        store.entries.push_back(entry);
        Insert(store, static_cast<uint32_t>(store.entries.size() - 1));
    }

    const uint32_t blocks = static_cast<uint32_t>(store.entries.size());

    if (particles < kMinParticles) {
        store.entries.clear();
        store.table.clear();
        return 0;
    }

    if (store.arena.size() < arenaBytes) {
        store.arena.resize(arenaBytes);
    }

    Run run;
    run.entries = store.entries.data();
    run.count = blocks;
    run.arena = store.arena.data();
    run.next.store(0, std::memory_order_relaxed);

    if (dispatcher && allowThreads && blocks > 1) {
        dispatcher(context, BuildTask, &run);
    } else {
        BuildTask(&run);
    }

    ++g_stats.prebuilds;
    g_stats.blocks += blocks;
    g_stats.particles += particles;

    if (timed) {
        g_wallNs += M2ParticleJobs::NowNs() - wallStart;
    }

    return blocks;
}

bool Apply(CParticleEmitter2* emitter, const C44Matrix* frameOfReference, const C44Matrix& view, EGxVertexBufferFormat format,
           uint32_t count, void* buffer, CParticleEmitter2::ParticleVertex& vertex) {
    if (!buffer) {
        return false;
    }

    for (auto& pair : g_stores) {
        Store& store = pair.second;
        Entry* entry = Find(store, emitter);

        if (!entry) {
            continue;
        }

        const bool frameMatches = entry->hasFrame == (frameOfReference != nullptr) &&
                                  (!entry->hasFrame || std::memcmp(&entry->frame, frameOfReference, sizeof(C44Matrix)) == 0);

        if (!entry->built || entry->format != format || entry->count != count || entry->live != emitter->m_liveList.Count() ||
            !frameMatches || std::memcmp(&entry->view, &view, sizeof(C44Matrix)) != 0 ||
            static_cast<size_t>(entry->block.vertexCount) * entry->stride > entry->bytes) {
            ++g_stats.rejected;
            return false;
        }

        std::memcpy(buffer, store.arena.data() + entry->offset, static_cast<size_t>(entry->block.vertexCount) * entry->stride);
        vertex.m_count = entry->block.vertexCount;
        emitter->ApplyVertexBlock(entry->block);
        ++g_stats.applied;
        return true;
    }

    ++g_stats.missed;
    return false;
}

void Forget(CParticleEmitter2* emitter) {
    for (auto& pair : g_stores) {
        Entry* entry = Find(pair.second, emitter);

        if (entry) {
            entry->emitter = nullptr;  // the slot stays in the table; a null emitter never matches a lookup
        }
    }
}

Stats GetStats() {
    return g_stats;
}

void ResetStats() {
    g_stats = Stats();
}

void LogAndReset(uint64_t frames) {
    if (!frames) {
        return;
    }

    const double perFrame = 1.0 / static_cast<double>(frames);
    const double ms = 1.0e-6;
    const double wall = g_wallNs * ms * perFrame;
    const double busy = g_busyNs.load() * ms * perFrame;

    std::fprintf(stderr,
                 "WarcraftXL particle build %s, per frame: prebuild wall %.3f ms (busy all threads %.3f ms, speedup %.2fx) | "
                 "%.1f blocks, %.0f particles prebuilt | at draw: %.1f applied, %.1f rejected, %.1f built in place\n",
                 Enabled() ? "on" : "off (WXL_PARTICLE_BUILD_THREADS=0)", wall, busy, wall > 0.0 ? busy / wall : 0.0,
                 g_stats.blocks * perFrame, g_stats.particles * perFrame, g_stats.applied * perFrame,
                 g_stats.rejected * perFrame, g_stats.missed * perFrame);

    g_stats = Stats();
    g_wallNs = 0;
    g_busyNs.store(0);
}

}  // namespace M2ParticleBuild
