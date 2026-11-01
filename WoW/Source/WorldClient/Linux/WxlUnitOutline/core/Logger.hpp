// Native stand-in for the core's logger: the supplied code logs state changes with WLOG_INFO; here they
// go to the opt-in WXL_NATIVE_DIAGNOSTICS trace.
#pragma once

#include "Engine/Source/ModernAssets/NativeDiagnostic.h"

#define WLOG_INFO(...) WxlNativeDiagnostic(__VA_ARGS__)
