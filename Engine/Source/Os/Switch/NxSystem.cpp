// libnx side of Os/Switch/NxSystem.h.

#include <switch.h>
#include <cstring>
#include "Engine/Source/Os/Switch/NxSystem.h"

uint64_t NxGetTotalMemory() {
    uint64_t total = 0;

    if (R_FAILED(svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0))) {
        return 0;
    }

    return total;
}

int32_t NxGetTimeZoneMinutes() {
    uint64_t now = 0;
    TimeCalendarTime calendar;
    TimeCalendarAdditionalInfo info;

    if (R_FAILED(timeGetCurrentTime(TimeType_Default, &now)) ||
        R_FAILED(timeToCalendarTimeWithMyRule(now, &calendar, &info))) {
        return 0;
    }

    return info.offset / 60;
}

int32_t NxOpenURL(const char* url) {
    WebCommonConfig config;

    if (R_FAILED(webPageCreate(&config, url))) {
        return 0;
    }

    return R_SUCCEEDED(webConfigShow(&config, nullptr));
}
