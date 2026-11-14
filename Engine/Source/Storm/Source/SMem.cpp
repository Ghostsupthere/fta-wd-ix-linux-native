#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifndef WOWDECOMP_LINUX
#include <winerror.h>
#endif
#include <malloc.h>
#include "Engine/Source/Storm/H/storm.h"
#if defined(WOWDECOMP_LINUX)
#include <atomic>
#include <dlfcn.h>
#include <pthread.h>
#endif

// STATIC: 0x00ADE850
static int32_t s_exitonfailure = 1;

// STATIC: 0x00ADE854
static int32_t s_heaptracking = 1;

// VERIFY: 56e6df56 auto
// OFFSET: 0x0076E4A0
void STORMAPI SMemSetDebugFlags(uint32_t flags, uint8_t changeMask) {
    if (changeMask & SMEM_DEBUG_NOEXITONFAILURE) {
        s_exitonfailure = (flags & SMEM_DEBUG_NOEXITONFAILURE) == 0;
    }

    if (changeMask & SMEM_DEBUG_HEAPTRACKING) {
        s_heaptracking = (flags & SMEM_DEBUG_HEAPTRACKING) != 0;
    }
}

// VERIFY: cd1f46b7 manual
// OFFSET: 0x0076E4E0
static void FatalError(size_t bytes, const char* filename, int32_t linenumber) {
    char buffer[128];
    sprintf(buffer, "Requested %u bytes of memory", static_cast<uint32_t>(bytes));

    SErrDisplayError(STORM_ERROR_NOT_ENOUGH_MEMORY, filename, linenumber, buffer, s_exitonfailure == 0, 1, 0x11111111);

    if (s_exitonfailure) {
        StormExitProcess(1);
    }
}


#if defined(WOWDECOMP_LINUX)
// ---------------------------------------------------------------------------
// WXL_HEAP_CHECK=1: a debugging heap for tracking down heap corruption.
//
// Off by default (the switch is read once, from the environment). When on, every SMemAlloc / operator new block (the
// engine, Lua's allocations through it, and the FMOD callbacks all come through here) gets a 48-byte header and a
// 16-byte redzone after its (8-byte rounded) size:
//   * a block whose redzone was written to is reported when it is freed (heap overflow), with the file/line or the
//     calling address of the allocation;
//   * freed blocks are filled with 0xDD and parked in a quarantine (up to 256 MB); a block whose fill changed before it
//     leaves the quarantine is reported (write after free);
//   * freeing a block twice, or a pointer that has no header, is reported instead of corrupting the heap.
// Reports go to stderr (so into Logs/native-launch-*.log) as "WXL_HEAP_CHECK: ..." lines, at most 60 of them. Caller
// addresses are printed as module+offset: symbolize with addr2line against the same build.
// ---------------------------------------------------------------------------
namespace {

constexpr uint32_t kHeapLive = 0xA110CA7Eu;
constexpr uint32_t kHeapFreed = 0xF4EED00Du;
constexpr size_t kHeapRedzone = 16;
constexpr size_t kQuarantineMaxBytes = 256u << 20;
constexpr size_t kQuarantineMaxBlocks = 1u << 18;

struct HeapHdr {
    uint32_t magic;
    uint32_t line;
    uint64_t size;   // usable bytes (the request rounded up to 8)
    const char* file;
    void* caller;
    void* freeCaller;
    uint64_t pad;
};
static_assert(sizeof(HeapHdr) == 48, "keep user pointers 16-byte aligned");

std::atomic<int> s_heapCheck{-1};
std::atomic<int> s_heapReports{0};
pthread_mutex_t s_quarantineLock = PTHREAD_MUTEX_INITIALIZER;
HeapHdr* s_quarantine[kQuarantineMaxBlocks];
size_t s_quarantineHead = 0;   // oldest
size_t s_quarantineCount = 0;
size_t s_quarantineBytes = 0;

bool HeapCheckEnabled() {
    int mode = s_heapCheck.load(std::memory_order_relaxed);

    if (mode < 0) {
        const char* value = getenv("WXL_HEAP_CHECK");
        mode = (value && *value && *value != '0') ? 1 : 0;
        s_heapCheck.store(mode, std::memory_order_relaxed);

        if (mode) {
            fprintf(stderr, "WXL_HEAP_CHECK: heap checker active (redzones, 0xDD fill, 256 MB quarantine)\n");
        }
    }

    return mode == 1;
}

void DescribeAddress(char* out, size_t outSize, const void* address) {
    Dl_info info;

    if (address && dladdr(address, &info) && info.dli_fname) {
        const char* name = strrchr(info.dli_fname, '/');
        snprintf(out, outSize, "%s+0x%lx", name ? name + 1 : info.dli_fname,
            static_cast<unsigned long>(static_cast<const char*>(address) - static_cast<const char*>(info.dli_fbase)));
    } else {
        snprintf(out, outSize, "%p", address);
    }
}

void HeapReport(const char* what, const HeapHdr* hdr, const char* detail) {
    if (s_heapReports.fetch_add(1) >= 60) {
        return;
    }

    char allocAt[160];
    char freeAt[160];
    DescribeAddress(allocAt, sizeof(allocAt), hdr->caller);
    DescribeAddress(freeAt, sizeof(freeAt), hdr->freeCaller);
    fprintf(stderr, "WXL_HEAP_CHECK: %s: block %p, %llu bytes, allocated by %s (%s:%u), freed by %s%s%s\n", what,
        static_cast<const void*>(hdr + 1), static_cast<unsigned long long>(hdr->size), allocAt, hdr->file ? hdr->file : "?", hdr->line,
        hdr->freeCaller ? freeAt : "-", detail ? " -- " : "", detail ? detail : "");
    fflush(stderr);
}

bool RedzoneIntact(const HeapHdr* hdr, size_t* badOffset) {
    const uint8_t* zone = reinterpret_cast<const uint8_t*>(hdr + 1) + hdr->size;

    for (size_t i = 0; i < kHeapRedzone; i++) {
        if (zone[i] != 0xFD) {
            *badOffset = i;
            return false;
        }
    }

    return true;
}

// Verifies the 0xDD fill of a freed block and the redzone, then releases it for real.
void QuarantineRelease(HeapHdr* hdr) {
    char detail[96];
    size_t bad;
    const uint8_t* body = reinterpret_cast<const uint8_t*>(hdr + 1);

    if (hdr->magic != kHeapFreed) {
        HeapReport("freed block's header was overwritten", hdr, nullptr);
    } else {
        for (size_t i = 0; i < hdr->size; i++) {
            if (body[i] != 0xDD) {
                snprintf(detail, sizeof(detail), "write after free, first changed byte at offset %zu (0x%02x)", i, body[i]);
                HeapReport("USE AFTER FREE", hdr, detail);
                break;
            }
        }

        if (!RedzoneIntact(hdr, &bad)) {
            snprintf(detail, sizeof(detail), "redzone byte %zu changed after free", bad);
            HeapReport("USE AFTER FREE", hdr, detail);
        }
    }

    free(hdr);
}

void* HeapCheckAlloc(size_t bytes, const char* filename, int32_t linenumber, uint32_t flags, void* caller) {
    size_t size = (bytes + 7) & ~static_cast<size_t>(7);
    HeapHdr* hdr = static_cast<HeapHdr*>(malloc(sizeof(HeapHdr) + size + kHeapRedzone));

    if (!hdr) {
        return nullptr;
    }

    hdr->magic = kHeapLive;
    hdr->line = static_cast<uint32_t>(linenumber);
    hdr->size = size;
    hdr->file = filename;
    hdr->caller = caller;
    hdr->freeCaller = nullptr;
    hdr->pad = 0;

    uint8_t* body = reinterpret_cast<uint8_t*>(hdr + 1);

    if (flags & SMEM_FLAG_ZEROMEMORY) {
        memset(body, 0, size);
    }

    memset(body + size, 0xFD, kHeapRedzone);
    return body;
}

void HeapCheckFree(void* ptr, void* caller) {
    HeapHdr* hdr = static_cast<HeapHdr*>(ptr) - 1;

    if (hdr->magic == kHeapFreed) {
        hdr->freeCaller = caller;
        HeapReport("DOUBLE FREE", hdr, nullptr);
        return;
    }

    if (hdr->magic != kHeapLive) {
        if (s_heapReports.fetch_add(1) < 60) {
            char at[160];
            DescribeAddress(at, sizeof(at), caller);
            fprintf(stderr, "WXL_HEAP_CHECK: BAD FREE of %p by %s: no valid header (not from SMemAlloc, or the header was overwritten); leaked on purpose\n", ptr, at);
            fflush(stderr);
        }

        return;
    }

    hdr->freeCaller = caller;
    size_t bad;

    if (!RedzoneIntact(hdr, &bad)) {
        char detail[64];
        snprintf(detail, sizeof(detail), "wrote %zu byte(s) past the end", bad + 1);
        HeapReport("HEAP OVERFLOW", hdr, detail);
    }

    hdr->magic = kHeapFreed;
    memset(hdr + 1, 0xDD, hdr->size);

    HeapHdr* release[4];
    size_t releaseCount = 0;
    pthread_mutex_lock(&s_quarantineLock);
    s_quarantine[(s_quarantineHead + s_quarantineCount) % kQuarantineMaxBlocks] = hdr;
    s_quarantineCount++;
    s_quarantineBytes += hdr->size;

    while (releaseCount < 4 && (s_quarantineBytes > kQuarantineMaxBytes || s_quarantineCount >= kQuarantineMaxBlocks)) {
        HeapHdr* old = s_quarantine[s_quarantineHead];
        s_quarantineHead = (s_quarantineHead + 1) % kQuarantineMaxBlocks;
        s_quarantineCount--;
        s_quarantineBytes -= old->size;
        release[releaseCount++] = old;
    }

    pthread_mutex_unlock(&s_quarantineLock);

    for (size_t i = 0; i < releaseCount; i++) {
        QuarantineRelease(release[i]);
    }
}

void HeapCheckDrain() {
    pthread_mutex_lock(&s_quarantineLock);
    size_t count = s_quarantineCount;
    pthread_mutex_unlock(&s_quarantineLock);

    for (size_t i = 0; i < count; i++) {
        pthread_mutex_lock(&s_quarantineLock);

        if (!s_quarantineCount) {
            pthread_mutex_unlock(&s_quarantineLock);
            break;
        }

        HeapHdr* old = s_quarantine[s_quarantineHead];
        s_quarantineHead = (s_quarantineHead + 1) % kQuarantineMaxBlocks;
        s_quarantineCount--;
        s_quarantineBytes -= old->size;
        pthread_mutex_unlock(&s_quarantineLock);
        QuarantineRelease(old);
    }
}

}  // namespace
#endif

// VERIFY: a5777d4c auto
// OFFSET: 0x0076E540
void* STORMAPI SMemAlloc(size_t bytes, const char* filename, int32_t linenumber, uint32_t flags) {
    size_t size = (bytes + 7) & ~static_cast<size_t>(7);
#if defined(WOWDECOMP_LINUX)
    if (HeapCheckEnabled()) {
        void* checked = HeapCheckAlloc(bytes, filename, linenumber, flags, __builtin_return_address(0));

        if (!checked) {
            FatalError(bytes, filename, linenumber);
        }

        return checked;
    }
#endif
    void* ptr = (flags & SMEM_FLAG_ZEROMEMORY) ? calloc(1, size) : malloc(size);

    if (!ptr) {
        FatalError(bytes, filename, linenumber);
        return nullptr;
    }

    _msize(ptr);

    return ptr;
}

// VERIFY: fab6c035 auto
// OFFSET: 0x0076E5A0
int32_t STORMAPI SMemFree(void* ptr, const char* filename, int32_t linenumber, uint32_t flags) {
#if defined(WOWDECOMP_LINUX)
    if (HeapCheckEnabled()) {
        if (ptr) {
            HeapCheckFree(ptr, __builtin_return_address(0));
        }

        return 1;
    }
#endif
    if (ptr) {
        _msize(ptr);
        free(ptr);
    }

    return 1;
}

// VERIFY: 19b70aac auto
// OFFSET: 0x0076E5D0
int32_t STORMAPI SMemIsValidPointer(void* ptr, size_t bytes, uint32_t flags) {
    return 1;
}

// VERIFY: 26d3182e manual
// OFFSET: 0x0076E5E0
void* STORMAPI SMemReAlloc(void* ptr, size_t bytes, const char* filename, int32_t linenumber, uint32_t flags) {
    if (flags == SMEM_REALLOC_NULL) {
        return nullptr;
    }

    if (!ptr) {
        return SMemAlloc(bytes, filename, linenumber, flags);
    }

#if defined(WOWDECOMP_LINUX)
    if (HeapCheckEnabled()) {
        if (flags & SMEM_FLAG_NOREALLOC) {
            return nullptr;
        }

        const HeapHdr* old = static_cast<const HeapHdr*>(ptr) - 1;
        size_t checkedOld = old->magic == kHeapLive ? old->size : 0;
        void* moved = HeapCheckAlloc(bytes, filename, linenumber, flags & SMEM_FLAG_ZEROMEMORY, __builtin_return_address(0));

        if (!moved) {
            if (bytes) {
                FatalError(bytes, filename, linenumber);
            }

            return nullptr;
        }

        memcpy(moved, ptr, checkedOld < bytes ? checkedOld : bytes);
        HeapCheckFree(ptr, __builtin_return_address(0));
        return moved;
    }
#endif
    size_t oldsize = _msize(ptr);

    if (flags & SMEM_FLAG_NOREALLOC) {
        return nullptr;
    }

    size_t size = (bytes + 7) & ~static_cast<size_t>(7);
    void* newptr = realloc(ptr, size);

    if (!newptr) {
        if (size) {
            FatalError(bytes, filename, linenumber);
        }

        return nullptr;
    }

    size_t newsize = _msize(newptr);

    if (s_heaptracking && newptr != ptr) {
        DebugLog(-1, "[%08lx] REALLOC %10d bytes <-- [%08lx]/%d, requested=%d (%s: %d)\n", newptr, newsize, ptr, oldsize, bytes, filename, linenumber);
    }

    if (oldsize < bytes && (flags & SMEM_FLAG_ZEROMEMORY)) {
        memset(static_cast<uint8_t*>(newptr) + oldsize, 0, bytes - oldsize);
    }

    return newptr;
}

// VERIFY: a7926bc3 auto
// OFFSET: 0x00401010
void* operator new(size_t bytes) {
#if defined(WOWDECOMP_LINUX)
    if (HeapCheckEnabled()) {
        void* checked = HeapCheckAlloc(bytes, "new", SLOG_FUNCTION, 0, __builtin_return_address(0));

        if (!checked) {
            FatalError(bytes, "new", SLOG_FUNCTION);
        }

        return checked;
    }
#endif
    return SMemAlloc(bytes, "new", SLOG_FUNCTION, 0);
}

// VERIFY: 79127843 auto
// OFFSET: 0x0047CC90
void operator delete(void* ptr) noexcept {
#if defined(WOWDECOMP_LINUX)
    if (ptr && HeapCheckEnabled()) {
        HeapCheckFree(ptr, __builtin_return_address(0));
        return;
    }
#endif
    if (ptr) {
        SMemFree(ptr, "delete", SLOG_FUNCTION, 0);
    }
}

// VERIFY: 5c89935c auto
// OFFSET: 0x00401030
void operator delete[](void* ptr) noexcept {
#if defined(WOWDECOMP_LINUX)
    if (ptr && HeapCheckEnabled()) {
        HeapCheckFree(ptr, __builtin_return_address(0));
        return;
    }
#endif
    if (ptr) {
        SMemFree(ptr, "delete[]", SLOG_FUNCTION, 0);
    }
}

int32_t STORMAPI SMemDestroy() {
#if defined(WOWDECOMP_LINUX)
    if (HeapCheckEnabled()) {
        HeapCheckDrain();
    }
#endif
    return 1;
}

void STORMAPI SMemInitialize() {
}
