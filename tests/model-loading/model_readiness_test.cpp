#include <cassert>
#include <cstdint>
#include <functional>
#include <optional>
#ifdef NDEBUG
#error Assertions must be enabled
#endif
struct CAsyncObject;
struct ReadList { void LinkToHead(CAsyncObject*) {} };
struct Queue { ReadList m_readList; };
struct CAsyncObject {
    bool m_isProcessed = false;
    bool m_isCurrent = false;
    bool m_isRead = true;
    struct Link { void Unlink() {} } m_link;
    Queue* m_queue = nullptr;
    void* m_file = nullptr;
};
static struct Lock {
    int depth = 0;

    void Enter() {
        assert(depth == 0);
        ++depth;
    }

    void Leave() {
        assert(depth == 1);
        --depth;
    }
} g_queueLock;
static CAsyncObject* s_asyncWaitObject;
static void (*s_ingameStartCallback)();
static void (*s_ingameProgressCallback)(float, int);
static int reads, fatalCalls;
static std::function<void(CAsyncObject*)> onRead;
void SErrDisplayAppFatal(const char*) { ++fatalCalls; }
void DebugLog(const char*, const char*) {}
void OsSleep(int) {}
namespace SFile {
bool IsStreamingMode() { return false; }
void SetFilePriority(void*, int, int) {}
void GetActualFileName(void*, char* out, int) { *out = 0; }
}
void AsyncFileReadPollHandler(void*, void*) {
    CAsyncObject* read = s_asyncWaitObject;
    s_asyncWaitObject = nullptr;
    read->m_isProcessed = true;
    ++reads;
    onRead(read);
}
struct CM2Shared {
    uint32_t m_flags = 0;
    bool m_loadFailed = false;
    CAsyncObject* m_asyncObject = nullptr;
    int* m_data = nullptr;
    int* m_skinData = nullptr;
};
struct CM2Model {
    CM2Shared* m_shared = nullptr;
    CM2Model* m_model30 = nullptr;
    CM2Model* m_attachList = nullptr;
    CM2Model* m_attachNext = nullptr;
    uint32_t m_refCount = 1, m_fFlags = 0, m_flags = 0;
    bool m_instanceLoadFailed = false;
    bool failAllocation = false;
    int initializations = 0, unlinks = 0;
    std::function<void()> onInitialize;
    void Release() {
        assert(m_refCount > 0);
        --m_refCount;
    }
    bool IsLoaded() const { return (m_fFlags & 1) != 0; }
    uint32_t IsLoaded(int wait, int recursive);
    bool HasLoadFailed() const;
    bool WaitForLoad(const char*);
    int32_t InitializeLoaded();
    void UnlinkFromCallbackList() { ++unlinks; }
};

#include "async_wait.inc"
#include "model_wait.inc"
#include "model_is_loaded.inc"
#include "model_initialize.inc"

    ++initializations;
    if (failAllocation) {
        m_instanceLoadFailed = true;
        return 0;
    }
    m_fFlags |= 1;
    if (onInitialize) {
        onInitialize();
    }
    return 1;
}

int main() {
    int bytes = 1;
    CAsyncObject processed{true}, modelRead, skinRead;
    AsyncFileReadWait(&processed);
    AsyncFileReadWait(&processed);
    assert(reads == 0 && fatalCalls == 0 && !s_asyncWaitObject);
    CM2Model absent;
    assert(!absent.WaitForLoad(nullptr));

    CM2Shared share{0, false, &modelRead, nullptr, nullptr};
    CM2Model base;
    base.m_shared = &share;
    onRead = [&](CAsyncObject* read) {
        assert(base.m_refCount > 1);
        AsyncFileReadWait(read);
        if (read == &modelRead) {
            share.m_flags = 1;
            share.m_data = &bytes;
            share.m_asyncObject = &skinRead;
        } else {
            share.m_flags = 3;
            share.m_skinData = &bytes;
            assert(base.WaitForLoad(nullptr));
            share.m_asyncObject = nullptr;
        }
    };
    assert(base.WaitForLoad(nullptr));
    assert(base.initializations == 1 && base.unlinks == 1 && base.m_refCount == 1);
    assert(reads == 2 && fatalCalls == 0);
    assert(base.WaitForLoad(nullptr) && base.initializations == 1 && reads == 2);

    CM2Shared ready{3, false, &processed, &bytes, &bytes};
    CM2Model source, duplicate;
    source.m_shared = duplicate.m_shared = &ready;
    duplicate.m_model30 = &source;
    source.m_flags = 0x20;
    source.onInitialize = [&] {
        assert(source.m_refCount > 1);
        assert(duplicate.InitializeLoaded());
    };
    assert(source.IsLoaded(0, 0) && !source.IsLoaded());
    assert(duplicate.WaitForLoad(nullptr));
    assert(source.initializations == 1 && duplicate.initializations == 1 && fatalCalls == 0);
    assert(source.m_refCount == 1 && duplicate.m_refCount == 1);

    CM2Model broken, child;
    broken.m_shared = child.m_shared = &ready;
    child.m_model30 = &broken;
    broken.m_flags = 0x20;
    broken.failAllocation = true;
    assert(!child.WaitForLoad(nullptr));
    assert(broken.HasLoadFailed() && child.HasLoadFailed());
    assert(!broken.IsLoaded(0, 0) && !broken.IsLoaded(1, 0));
    assert(!child.WaitForLoad(nullptr) && broken.initializations == 1 && child.initializations == 0);

    CAsyncObject failedRead;
    CM2Shared failed{0, false, &failedRead, nullptr, nullptr};
    CM2Model failedModel;
    failedModel.m_shared = &failed;
    onRead = [&](CAsyncObject*) {
        failed.m_loadFailed = true;
        failed.m_asyncObject = &skinRead;
    };
    assert(!failedModel.WaitForLoad(nullptr));
    assert(failedModel.initializations == 0 && reads == 3);
    assert(g_queueLock.depth == 0);
}
