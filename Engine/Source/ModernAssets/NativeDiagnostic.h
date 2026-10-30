#ifndef ENGINE_SOURCE_MODERNASSETS_NATIVEDIAGNOSTIC_H
#define ENGINE_SOURCE_MODERNASSETS_NATIVEDIAGNOSTIC_H

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

// DebugLog is compiled out in Release. This opt-in trace remains available
// when checking asset and equipment paths in the production native executable.
#if defined(__GNUC__)
__attribute__((format(printf, 1, 2)))
#endif
inline void WxlNativeDiagnostic(const char* format, ...) {
    static const bool enabled = [] {
        const char* value = std::getenv("WXL_NATIVE_DIAGNOSTICS");
        return value && value[0] == '1';
    }();
    if (!enabled) return;
    std::fputs("WarcraftXL: ", stderr);
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::fputc('\n', stderr);
}
#endif
