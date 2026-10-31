#pragma once
#include <d3d9.h>
#include <chrono>
#include <stdexcept>
inline int QueryPerformanceFrequency(LARGE_INTEGER* value) { value->QuadPart=1000000000;return 1; }
inline int QueryPerformanceCounter(LARGE_INTEGER* value) { value->QuadPart=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();return 1; }
inline void RaiseException(DWORD,DWORD,DWORD,const ULONG_PTR*) {throw std::runtime_error("injected native visual fault");}
