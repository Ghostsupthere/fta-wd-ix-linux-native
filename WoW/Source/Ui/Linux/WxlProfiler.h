#ifndef WOW_SOURCE_UI_LINUX_WXLPROFILER_H
#define WOW_SOURCE_UI_LINUX_WXLPROFILER_H

// Lua access to the frame profiler (Engine/Source/Base/Profiler.h):
//   GetProfilerZones([frames])  ->  table { frames, frameMs, fps, drawCalls, triangles, zones = { {name, selfMs, totalMs, calls}, ... } },
//                                   averaged over the last `frames` frames (default 120, at most 240); nil when there is no data yet
//   SetProfilerEnabled(bool)    ->  turns recording on or off (off also clears it)
//   IsProfilerEnabled()         ->  boolean

#if defined(WOWDECOMP_LINUX)

void WxlRegisterProfilerLua();

#endif  // WOWDECOMP_LINUX

#endif
