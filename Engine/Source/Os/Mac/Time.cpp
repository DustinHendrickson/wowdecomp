// macOS implementation of Os/Win64/Time.cpp. The Switch build shares it.

#include <cstdint>
#include <ctime>
#include "Engine/Source/Os/Time.h"
#if defined(__SWITCH__)
#include "Engine/Source/Os/Switch/NxSystem.h"
#endif

int32_t OsGetTimeZone() {
    // Minutes east of UTC, like -TIME_ZONE_INFORMATION::Bias.
#if defined(__SWITCH__)
    return NxGetTimeZoneMinutes();
#else
    time_t now = time(nullptr);
    tm local;
    localtime_r(&now, &local);

    return static_cast<int32_t>(local.tm_gmtoff / 60);
#endif
}

void OsGetTimeStamp(char* buffer, uint32_t size) {
    time_t now = time(nullptr);
    tm local;
    localtime_r(&now, &local);
    strftime(buffer, size, "%m%d%y_%H%M%S", &local);
}
