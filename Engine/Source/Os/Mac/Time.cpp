// macOS implementation of Os/Win64/Time.cpp.

#include <cstdint>
#include <ctime>
#include "Engine/Source/Os/Time.h"

int32_t OsGetTimeZone() {
    // Minutes east of UTC, like -TIME_ZONE_INFORMATION::Bias.
    time_t now = time(nullptr);
    tm local;
    localtime_r(&now, &local);

    return static_cast<int32_t>(local.tm_gmtoff / 60);
}

void OsGetTimeStamp(char* buffer, uint32_t size) {
    time_t now = time(nullptr);
    tm local;
    localtime_r(&now, &local);
    strftime(buffer, size, "%m%d%y_%H%M%S", &local);
}
