#ifndef ENGINE_SOURCE_STORM_SOURCE_MAC_SYSTEMTIME_H
#define ENGINE_SOURCE_STORM_SOURCE_MAC_SYSTEMTIME_H

// SYSTEMTIME (opaque in Platform.h) for the macOS Storm error and machine-state logs, which pass the local time around
// as the Windows code does.

#include <cstdint>
#include <ctime>
#include <sys/time.h>

struct _SYSTEMTIME {
    uint16_t wYear;
    uint16_t wMonth;
    uint16_t wDayOfWeek;
    uint16_t wDay;
    uint16_t wHour;
    uint16_t wMinute;
    uint16_t wSecond;
    uint16_t wMilliseconds;
};

inline void GetLocalTime(SYSTEMTIME* time) {
    timeval now;
    gettimeofday(&now, nullptr);

    tm parts;
    localtime_r(&now.tv_sec, &parts);

    time->wYear = static_cast<uint16_t>(parts.tm_year + 1900);
    time->wMonth = static_cast<uint16_t>(parts.tm_mon + 1);
    time->wDayOfWeek = static_cast<uint16_t>(parts.tm_wday);
    time->wDay = static_cast<uint16_t>(parts.tm_mday);
    time->wHour = static_cast<uint16_t>(parts.tm_hour);
    time->wMinute = static_cast<uint16_t>(parts.tm_min);
    time->wSecond = static_cast<uint16_t>(parts.tm_sec);
    time->wMilliseconds = static_cast<uint16_t>(now.tv_usec / 1000);
}

#endif
