// The converter is deliberately silent in Release builds. Loader failures are reported by CM2Shared.
#pragma once

#define WLOG_INFO(...) do { } while (0)
#define WLOG_DEBUG(...) do { } while (0)

namespace wxl::core::log {
template <typename... Args>
inline void Printf(const char*, Args&&...) {
}
} // namespace wxl::core::log
