#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <utility>
#include "Engine/Source/Model2/M2Cache.h"
#include "Engine/Source/Base/ObjectAlloc.h"
#include "Engine/Source/BlizzardCore/SFile2-Core.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Model2/M2Scene.h"
#include "Engine/Source/Model2/WxlModelMirror.h"
#include "Engine/Source/Os/Processor.h"
#include "Engine/Source/Os/System.h"
#include "Engine/Source/Os/TimeManager.h"
#include "Engine/Source/Services/GfxSingletonManager.h"
#include "Engine/Source/Services/ModelBlob.h"
#include "Engine/Source/Services/ParticleSystem2.h"
#include "Engine/Source/Storm/H/storm.h"
#include "Engine/Source/Tempest/CRandom.h"

// STATIC: 0x00D3FCE8
CM2ModelHeap* g_modelPool;

// STATIC: 0x00D3FCEC
uint32_t CM2Cache::s_processorFeatures;

// STATIC: 0x00D3FCF0
static CM2Cache s_cache;

// STATIC: 0x00DCE690
static float s_randomTable[128];

// VERIFY: d79458b6 auto
// OFFSET: 0x0081BF50
void CM2Cache::ClearSharedHash() {
    for (uint32_t i = 0; i < sizeof(this->m_shared) / sizeof(this->m_shared[0]); i++) {
        CM2Shared* shared = this->m_shared[i];

        while (shared) {
            CM2Shared* next = shared->m_next;
            shared->m_previous = nullptr;
            shared->m_next = nullptr;
            shared = next;
        }

        this->m_shared[i] = nullptr;
    }
}

// VERIFY: de6dcef8 auto
// OFFSET: 0x0081BFA0
void CM2Cache::BeginThread(void (*func)(void*), void* param) {
    this->m_threadFunc = func;
    this->m_threadParam = param;
    for (uint32_t i = 0; i < this->m_workerCount; ++i) {
        this->m_threadStart[i].Set();
    }
}

// VERIFY: b68bbc6f auto
// OFFSET: 0x0081BFD0
void CM2Cache::WaitThread() {
    for (uint32_t i = 0; i < this->m_workerCount; ++i) {
        this->m_threadDone[i].Wait(0xFFFFFFFF);
    }
}

// VERIFY: 7d58516f manual
// OFFSET: 0x0081BFE0
uint32_t STORMAPI CM2Cache::ThreadProc(void* param) {
    WorkerContext* ctx = static_cast<WorkerContext*>(param);
    CM2Cache* cache = ctx->cache;
    const uint32_t idx = ctx->workerIndex;

    PropSelectContext(cache->m_propContext);
    cache->m_activeWorkers.fetch_add(1, std::memory_order_relaxed);
    cache->m_flags.fetch_or(0x400, std::memory_order_relaxed);
    cache->m_threadStart[idx].Wait(0xFFFFFFFF);

    while (!(cache->m_flags.load(std::memory_order_relaxed) & 0x800)) {
        if (cache->m_threadFunc) {
            cache->m_threadFunc(cache->m_threadParam);
        }
        cache->m_threadDone[idx].Set();
        cache->m_threadStart[idx].Wait(0xFFFFFFFF);
    }

    if (cache->m_activeWorkers.fetch_sub(1, std::memory_order_relaxed) == 1) {
        cache->m_flags.fetch_and(~0x400u, std::memory_order_relaxed);
    }

    return 0;
}

// VERIFY: c0d77014 auto
// OFFSET: 0x0081C060
void M2SetGlobalOptFlags(uint32_t flags) {
    uint32_t cur = s_cache.m_flags.load(std::memory_order_relaxed);
    while (!s_cache.m_flags.compare_exchange_weak(cur, (cur & ~0xE000u) | (flags & 0xE000u), std::memory_order_relaxed)) {}
    CM2Scene::s_optFlags = flags & 0xE000u;
}

// VERIFY: 01e775e8 manual
// OFFSET: 0x0081C080
CM2Scene* M2CreateScene() {
    return new (SMemAlloc(sizeof(CM2Scene), __FILE__, __LINE__, 0)) CM2Scene(&s_cache);
}

// VERIFY: 3f7a837f auto
// OFFSET: 0x0081C0B0
uint32_t M2GetCacheFlags() {
    return s_cache.m_flags.load(std::memory_order_relaxed);
}

// VERIFY: 40919c5c auto
// OFFSET: 0x0081C0C0
void M2SetCacheFlags(uint32_t flags) {
    s_cache.m_flags.store(flags, std::memory_order_relaxed);
}

// VERIFY: e915f95e manual
// OFFSET: 0x0081C0D0
int32_t CM2Cache::Initialize(uint32_t flags) {
    if (this->m_initialized) {
        DebugLog("Model2: M2Initialize called more than once\n");
        return 1;
    }

    CM2Cache::s_processorFeatures = OsGetProcessorFeatures();

    if (OsGetNumberOfProcessors() > 1) {
        this->m_flags.fetch_or(flags & 4, std::memory_order_relaxed);
    }

    if (flags & 8) {
        int32_t vertexTarget = g_theGxDevicePtr->Caps().m_shaderTargets[0];
        int32_t pixelTarget = g_theGxDevicePtr->Caps().m_shaderTargets[4];

        if (vertexTarget > 0 && pixelTarget > 0) {
            this->m_flags.fetch_or(8, std::memory_order_relaxed);
        }
    }

    if (!(this->m_flags.load(std::memory_order_relaxed) & 8) && !(CM2Cache::s_processorFeatures & 4)) {
        this->m_flags.fetch_or(0x40, std::memory_order_relaxed);
    }

    if (g_theGxDevicePtr->Caps().m_colorWrite) {
        this->m_flags.fetch_or(flags & 1, std::memory_order_relaxed);
    }

    if (g_theGxDevicePtr->Caps().m_maxClipPlanes) {
        uint32_t vertexTarget = g_theGxDevicePtr->Caps().m_shaderTargets[0];

        if (vertexTarget - 6 > 1) {
            this->m_flags.fetch_or(flags & 2, std::memory_order_relaxed);
        } else if (!(this->m_flags.load(std::memory_order_relaxed) & 8)) {
            this->m_flags.fetch_or(flags & 2, std::memory_order_relaxed);
        }
    }

    this->m_flags.fetch_or(flags & 0x1A0, std::memory_order_relaxed);

    if (this->m_flags.load(std::memory_order_relaxed) & 4) {
        this->m_flags.fetch_and(~0xC00u, std::memory_order_relaxed);
        this->m_propContext = PropGetSelectedContext();
        uint32_t nCpus = OsGetNumberOfProcessors();
        this->m_workerCount = std::min<uint32_t>(kMaxWorkerThreads, std::max<uint32_t>(1u, nCpus > 1 ? nCpus - 1 : 1));
        for (uint32_t i = 0; i < this->m_workerCount; ++i) {
            this->m_workerCtx[i].cache = this;
            this->m_workerCtx[i].workerIndex = i;
            this->m_threadStart[i].Reset();
            this->m_threadDone[i].Reset();
            SThread::Create(CM2Cache::ThreadProc, &this->m_workerCtx[i], this->m_threads[i], const_cast<char*>("Model2"), 0);
        }
    } else {
        this->m_workerCount = 0;
    }

    uint32_t high = rand() << 16;
    CRndSeed seed(high | static_cast<uint16_t>(rand()));

    for (uint32_t i = 0; i < sizeof(s_randomTable) / sizeof(s_randomTable[0]); i++) {
        uint32_t bits = (CRandom::uint32_(seed) & 0x7FFFFF) | 0x3F800000;
        s_randomTable[i] = *reinterpret_cast<float*>(&bits) - 1.0f;
    }

    CParticleEmitter2::Init();
    this->m_initialized = 1;

    return 1;
}

// VERIFY: 7d576eb4 manual
// OFFSET: 0x0081C290
void CM2Cache::GarbageCollect(int32_t force) {
    uint32_t now = OsGetAsyncTimeMs();

    while (this->m_freeList) {
        CM2Shared* shared = this->m_freeList;

        if (!force && static_cast<int32_t>(now - shared->m_freeTime) < 10000) {
            break;
        }

        this->m_freeList = shared->m_freeNext;

        if (shared->m_freeNext) {
            shared->m_freeNext->m_freePrev = &this->m_freeList;
        } else {
            this->m_freeListTail = &this->m_freeList;
        }

        shared->m_freePrev = nullptr;
        shared->m_freeNext = nullptr;
        delete shared;
    }
}

// VERIFY: 370cb080 auto
// OFFSET: 0x0081C300
void CM2Cache::Destroy() {
    if (this->m_flags.load(std::memory_order_relaxed) & 4) {
        this->m_flags.fetch_or(0x800, std::memory_order_relaxed);
        for (uint32_t i = 0; i < this->m_workerCount; ++i) {
            this->m_threadStart[i].Set();
        }
        for (uint32_t i = 0; i < this->m_workerCount; ++i) {
            this->m_threads[i].Wait(0xFFFFFFFF);
        }
        this->m_workerCount = 0;
    }

    while (this->m_freeList) {
        CM2Shared* shared = this->m_freeList;

        if (shared->m_freePrev) {
            *shared->m_freePrev = shared->m_freeNext;
        }

        if (shared->m_freeNext) {
            shared->m_freeNext->m_freePrev = shared->m_freePrev;
        } else {
            this->m_freeListTail = shared->m_freePrev;
        }

        shared->m_freePrev = nullptr;
        shared->m_freeNext = nullptr;
        delete shared;
    }

    this->ClearSharedHash();
    CParticleEmitter2::Destroy();
    this->m_initialized = 0;
}

// VERIFY: ac4bc72b manual
// OFFSET: 0x0081C390
CM2Shared* CM2Cache::CreateShared(const char* name, uint32_t flags) {
    char path[260];
    SStrCopy(path, name, sizeof(path));
    SStrLower(path);

    char* ext = strrchr(path, '.');

    if (!(flags & 0x1000)) {
        if (!ext) {
            DebugLog("Model2: Invalid file extension: %s\n", path);
            return nullptr;
        }

        if (!strcmp(ext, ".mdl") || !strcmp(ext, ".mdx")) {
            strcpy(ext, ".m2");
        } else if (strcmp(ext, ".m2")) {
            DebugLog("Model2: Invalid file extension: %s\n", path);
            return nullptr;
        }
    }

    if (ext) {
        *ext = '\0';
    }

    CAaBox boundingBox;
    boundingBox.b = C3Vector();
    boundingBox.t = C3Vector();
    ModelBlobQuery(path, boundingBox.b, boundingBox.t);

    if (ext) {
        *ext = '.';
    }

    uint32_t fullPath = flags & 0x10;
    char* key;

    if (fullPath) {
        key = path;
    } else {
        key = ext;

        if (ext > path) {
            while (true) {
                if (*key == '\\' || *key == '/') {
                    key++;
                    break;
                }

                if (--key <= path) {
                    break;
                }
            }
        }
    }

    uint32_t hash = 0;

    for (const char* c = key; *c; c++) {
        hash = *c + 19 * hash;
    }

#if defined(WOWDECOMP_LINUX)
    // A mirrored copy (weapon sidecar offhand) is a separate cache entry of the same file.
    const bool mirrorY = (flags & WxlModelMirror::kCreateFlag) != 0;
    if (mirrorY) {
        hash = hash * 31 + 0x6d69;
        boundingBox.b.y = -boundingBox.b.y;
        boundingBox.t.y = -boundingBox.t.y;
        std::swap(boundingBox.b.y, boundingBox.t.y);
    }
#endif

    CM2Shared** link = &this->m_shared[hash % (sizeof(this->m_shared) / sizeof(this->m_shared[0]))];
    int32_t cmp = 1;

    for (CM2Shared* shared = *link; shared; link = &shared->m_next, shared = shared->m_next) {
        if (shared->m_fileNameHash < hash) {
            continue;
        }

        if (shared->m_fileNameHash > hash) {
            break;
        }

        cmp = strcmp(fullPath ? shared->m_fileName : shared->m_fileNameWithoutPath, key);

        if (cmp >= 0) {
            break;
        }
    }

#if defined(WOWDECOMP_LINUX)
    if (*link && cmp == 0 && (((*link)->m_flags & WxlModelMirror::kSharedFlag) != 0) != mirrorY) {
        cmp = 1;
    }
#endif

    if (*link && cmp == 0) {
        (*link)->AddRef();
        return *link;
    }

    SFileImpl* file;

    if (!SFile::OpenEx(nullptr, path, (flags >> 2) & 1, &file)) {
        DebugLog("Model2: File not found: %s\n", path);
        return nullptr;
    }

    CM2Shared* shared = new (SMemAlloc(sizeof(CM2Shared), __FILE__, __LINE__, 0)) CM2Shared(this);

    if (!shared) {
        SFileCloseFile(file);
        return nullptr;
    }

#if defined(WOWDECOMP_LINUX)
    // Set before Load: the asynchronous load may finish before CreateShared returns.
    if (mirrorY) {
        shared->m_flags |= WxlModelMirror::kSharedFlag;
    }
#endif

    if (!shared->Load(file, flags & 4, &boundingBox)) {
        SFileCloseFile(file);
        delete shared;
        return nullptr;
    }

    strcpy(shared->m_fileName, path);
    shared->m_fileNameHash = hash;
    shared->m_fileNameWithoutPath = strrchr(shared->m_fileName, '.');

    if (shared->m_fileNameWithoutPath > shared->m_fileName) {
        while (true) {
            if (*shared->m_fileNameWithoutPath == '\\' || *shared->m_fileNameWithoutPath == '/') {
                shared->m_fileNameWithoutPath++;
                break;
            }

            if (--shared->m_fileNameWithoutPath <= shared->m_fileName) {
                break;
            }
        }
    }

    if (!(flags & 8)) {
        shared->m_previous = link;
        shared->m_next = *link;

        if (*link) {
            (*link)->m_previous = &shared->m_next;
        }

        *link = shared;
    }

    if (flags & 0x40) {
        shared->m_flags |= 0x40;
    }

    return shared;
}

// VERIFY: 928376c1 manual
// OFFSET: 0x0081C6E0
void M2Initialize(uint32_t flags, uint32_t modelCount) {
    s_cache.Initialize(flags);

    if (!modelCount) {
        modelCount = 2048;
    }

    g_modelPool = new (SMemAlloc(sizeof(CM2ModelHeap), __FILE__, __LINE__, 0)) CM2ModelHeap;

    if (g_modelPool) {
        g_modelPool->m_heapId = ObjectAllocAddHeap(sizeof(CM2Model), modelCount, "CM2Model", true);
    }
}

// VERIFY: 7fec2348 manual
// OFFSET: 0x0081C750
void M2Destroy() {
    s_cache.Destroy();
    ParticleSystemManager::Destroy();
    RibbonManager::Destroy();
    delete g_modelPool;
    g_modelPool = nullptr;
}

// VERIFY: 5ec5f7ac auto
// OFFSET: 0x0081C790
void CM2Cache::UpdateShared() {
    this->m_updateTime = OsGetAsyncTimeMs();

    CM2Shared* shared = this->m_sharedUpdateList.Head();

    while (!this->m_sharedUpdateList.IsEnd(shared)) {
        CM2Shared* next = this->m_sharedUpdateList.RawNext(shared);

        if (this->m_updateTime - shared->m_lastUsedTime < 10000) {
            break;
        }

        shared->DestroyBuffers();
        shared->m_updateLink.Unlink();
        shared = next;
    }
}

// VERIFY: 570b2300 manual
// OFFSET: 0x0081C840
void CM2Cache::LinkToSharedUpdateList(CM2Shared* shared) {
    shared->m_lastUsedTime = this->m_updateTime;
    this->m_sharedUpdateList.LinkToTail(shared);
}

// VERIFY: 1f730c93 manual
// OFFSET: 0x0081C870
CM2Cache::~CM2Cache() {
    if (this->m_initialized) {
        DebugLog("Model2: M2Destroy never called\n");
    }
}

// VERIFY: 902f5653 manual
// OFFSET: 0x0081C900
CM2Cache::CM2Cache() :
    m_threadStart{
        SEvent(0, 0), SEvent(0, 0), SEvent(0, 0),
        SEvent(0, 0), SEvent(0, 0), SEvent(0, 0)
    },
    m_threadDone{
        SEvent(0, 0), SEvent(0, 0), SEvent(0, 0),
        SEvent(0, 0), SEvent(0, 0), SEvent(0, 0)
    } {
    this->m_freeList = nullptr;
    this->m_freeListTail = &this->m_freeList;
    this->m_initialized = 0;
    this->m_flags.store(0, std::memory_order_relaxed);
    memset(this->m_shared, 0, sizeof(this->m_shared));
    this->m_workerCount = 0;
    this->m_threadFunc = nullptr;
    this->m_threadParam = nullptr;
}
