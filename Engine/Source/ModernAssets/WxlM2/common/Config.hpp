// Minimal native-client configuration adapter for the portable WarcraftXL M2 converter.
#pragma once

#include <cstdlib>

namespace wxl::config {
inline bool Env(const char* name, bool fallback) {
    const char* value = std::getenv(name);
    if (!value || !*value) {
        return fallback;
    }
    return value[0] != '0' && value[0] != 'n' && value[0] != 'N' &&
           value[0] != 'f' && value[0] != 'F';
}
} // namespace wxl::config
