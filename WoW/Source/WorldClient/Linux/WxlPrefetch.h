// Cache prefetch helpers for the world culling loops. Culling walks objects scattered through memory
// (chunks, doodad definitions, their models); the time is mostly waiting for those loads, so the
// loops ask for the next objects' cache lines while they work on the current one. A prefetch never
// changes a result: it is a hint, it cannot fault, and it may be dropped.
#pragma once

#if defined(__GNUC__) || defined(__clang__)
#define WXL_PREFETCH(address) __builtin_prefetch(static_cast<const void*>(address))
#else
#define WXL_PREFETCH(address) ((void)0)
#endif

// The cache lines of `bytes` bytes at `address` (64-byte lines).
#define WXL_PREFETCH_RANGE(address, bytes)                                                          \
    do {                                                                                            \
        const char* wxlPrefetchBase = reinterpret_cast<const char*>(address);                       \
        for (unsigned wxlPrefetchOffset = 0; wxlPrefetchOffset < (bytes); wxlPrefetchOffset += 64)  \
            WXL_PREFETCH(wxlPrefetchBase + wxlPrefetchOffset);                                      \
    } while (0)
