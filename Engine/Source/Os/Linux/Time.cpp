#include <cstdint>
#include <ctime>
#include "Engine/Source/Base/OsTypes.h"
#include "Engine/Source/Os/Time.h"

int32_t OsGetTimeZone() {
    time_t now = time(nullptr);
    struct tm local;
    if (!localtime_r(&now, &local)) {
        return 0;
    }

    return static_cast<int32_t>(local.tm_gmtoff / 60);
}

// OFFSET: 0x0086D490
void OsGetTimeStamp(char* buffer, uint32_t size) {
    time_t now = time(nullptr);
    struct tm local;
    if (!localtime_r(&now, &local)) {
        if (size) {
            buffer[0] = '\0';
        }
        return;
    }
    strftime(buffer, size, "%m%d%y_%H%M%S", &local);
}

extern "C" {

// The frequency is fixed at 1e9 and the counter is nanoseconds, so the engine's
// counter/frequency division yields seconds exactly as it does on Windows.
// CLOCK_MONOTONIC rather than CLOCK_REALTIME: QueryPerformanceCounter never
// goes backwards, and neither does this.
BOOL QueryPerformanceCounter(LARGE_INTEGER* counter) {
    if (!counter) {
        return FALSE;
    }

    timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now)) {
        counter->QuadPart = 0;
        return FALSE;
    }

    counter->QuadPart = static_cast<int64_t>(now.tv_sec) * 1000000000 + now.tv_nsec;

    return TRUE;
}

BOOL QueryPerformanceFrequency(LARGE_INTEGER* frequency) {
    if (!frequency) {
        return FALSE;
    }

    frequency->QuadPart = 1000000000;

    return TRUE;
}

}  // extern "C"
