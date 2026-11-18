#ifndef WOW_SOURCE_OBJECT_LINUX_WXLJUMPTRACE_H
#define WOW_SOURCE_OBJECT_LINUX_WXLJUMPTRACE_H

// Opt-in trace (FTA_TRACE_JUMP=1) for "cannot jump after a root/stun ended" reports.
// Logs, to stderr (the launch log): every refused jump with the movement flags that blocked it,
// every root/unroot the client applies, and jump events dropped while rooted. Off by default; costs nothing then.

#if defined(WOWDECOMP_LINUX)

#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

inline bool FtaJumpTraceEnabled() {
    static const bool enabled = [] {
        const char* v = std::getenv("FTA_TRACE_JUMP");
        return v && v[0] == '1';
    }();
    return enabled;
}

#if defined(__GNUC__)
__attribute__((format(printf, 1, 2)))
#endif
inline void FtaJumpTrace(const char* format, ...) {
    if (!FtaJumpTraceEnabled()) return;
    static const auto start = std::chrono::steady_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    std::fprintf(stderr, "FTA_TRACE_JUMP [%lld.%03lld] ", static_cast<long long>(ms / 1000), static_cast<long long>(ms % 1000));
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::fputc('\n', stderr);
}

// Refused jumps repeat on every key press; keep one line per 400 ms so a held key does not flood the log.
inline bool FtaJumpTraceRefusalDue() {
    static auto last = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    if (now - last < std::chrono::milliseconds(400)) return false;
    last = now;
    return true;
}

#endif
#endif
