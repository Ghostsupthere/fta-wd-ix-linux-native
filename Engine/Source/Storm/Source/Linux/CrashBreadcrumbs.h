#ifndef ENGINE_SOURCE_STORM_SOURCE_LINUX_CRASHBREADCRUMBS_H
#define ENGINE_SOURCE_STORM_SOURCE_LINUX_CRASHBREADCRUMBS_H

#include <cstdint>

// Crash breadcrumbs: a fixed ring of the last events (map loads, model loads, rejected model data) that the crash handler
// prints next to the backtrace. Ported from the Windows client's native_crash (Engine/Source/Storm/Source/CrashDiagnostics) with the
// Windows parts (SEH, dbghelp, PDB matching) left out. Recording never allocates, waits or touches the disk.
namespace native_crash
{
    enum class Event : uint32_t { Backend, MapLoad, MapUnload, ModelLoad, ModelFailed, IndexGuard };
    enum class Backend : uint32_t { Unknown, Vulkan, D3d9, D3d9Ex, Other, D3d11 };

    void Record(Event event, char const* virtualAsset = nullptr, uint32_t value = 0, uint32_t extra = 0) noexcept;
    void SetBackend(Backend backend, uint32_t samples) noexcept;
    void RecordIndexGuard(char const* virtualAsset, uint32_t index, uint32_t start) noexcept;

    // Async-signal-safe: prints the backend and the recent events, oldest first, one line each, through write(line).
    using LineWriter = void (*)(char const* line);
    void WriteRecent(LineWriter write) noexcept;
}

#endif
