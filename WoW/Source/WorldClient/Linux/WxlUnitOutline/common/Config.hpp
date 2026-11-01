// Native stand-in for the core's configuration lookup: WarcraftXL.cfg / environment switches.
#pragma once

bool WxlProfileEnabled(const char* key, bool fallback);

namespace wxl::config {
// The supplied code passes a null default meaning "off unless the profile turns it on".
inline bool Flag(const char* key, const char*) { return WxlProfileEnabled(key, false); }
}  // namespace wxl::config
