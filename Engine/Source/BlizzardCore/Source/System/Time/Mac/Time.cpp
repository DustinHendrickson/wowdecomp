// macOS implementation of Blizzard::Time (Win64/Time.cpp).
//
// Timestamps are nanoseconds since 2000-01-01 00:00:00 UTC. As on Windows, GetTimestamp reads the wall clock once and
// then advances it with a monotonic counter, so the clock changing under the game doesn't make time jump.
// CLOCK_MONOTONIC_RAW keeps counting during sleep, like QueryPerformanceCounter.

#include <cstdint>
#include <ctime>
#include <sched.h>
#include "Engine/Source/BlizzardCore/Source/System/Time/Time.h"

const int32_t System_Time::s_daysBeforeMonth[13] = { 0, 31, 59, 90, 120, 151, 182, 212, 243, 273, 304, 334, 365 };
uint64_t System_Time::timeScaleCycles = 0xFFFFFFFFFFFFFFFF;
bool System_Time::s_usePerformanceCounter;
Blizzard::Lock::Atomic System_Time::s_tickLock;
int64_t System_Time::s_absBegin;
uint32_t System_Time::s_lastTickCount;
double System_Time::timeScaleMilliseconds;
Blizzard::Time::Timestamp System_Time::s_gmBegin;
uint64_t System_Time::s_tickCountHigh;
double System_Time::timeScaleNanoseconds;
double System_Time::timeScaleSeconds;
double System_Time::timeScaleMicroseconds;

namespace {

// Seconds from the Unix epoch to the Blizzard epoch (2000-01-01).
const int64_t kEpochOffsetSeconds = 946684800;

int64_t MonotonicNanoseconds() {
    return static_cast<int64_t>(clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW));
}

Blizzard::Time::Timestamp WallClock() {
    timespec now;
    clock_gettime(CLOCK_REALTIME, &now);

    return (static_cast<int64_t>(now.tv_sec) - kEpochOffsetSeconds) * 1000000000 + now.tv_nsec;
}

}

Blizzard::Time::Timestamp Blizzard::Time::FromFileTime(uint64_t fileTime) {
    // Same conversion as Windows: FILETIME is 100 ns ticks since 1601, which some file formats still carry.
    if (fileTime <= 0x0077A5D6AA8BF851) {
        return static_cast<Timestamp>(0x8000000000000000);
    }

    if (fileTime <= 0x030701FFA04E87AE) {
        return static_cast<Timestamp>(fileTime * 100 + 0x5143382561530000);
    }

    return 0x7FFFFFFFFFFFFFFF;
}

uint64_t System_Time::GetTickCount64() {
    return static_cast<uint64_t>(MonotonicNanoseconds() / 1000000);
}

void Blizzard::Time::BreakTime(Timestamp time, TimeRec& rec) {
    int32_t nanosecond = static_cast<int32_t>(time % 1000000000);
    int64_t seconds = time / 1000000000;

    if (nanosecond < 0) {
        nanosecond += 1000000000;
        seconds--;
    }

    time_t unixTime = static_cast<time_t>(seconds + kEpochOffsetSeconds);
    tm parts;
    gmtime_r(&unixTime, &parts);

    rec.year = parts.tm_year + 1900;
    rec.month = parts.tm_mon + 1;
    rec.day = parts.tm_mday;
    rec.hour = parts.tm_hour;
    rec.minute = parts.tm_min;
    rec.second = parts.tm_sec;
    rec.nanosecond = nanosecond;
    rec.weekday = parts.tm_wday;
    rec.yearday = parts.tm_yday;
}

Blizzard::Time::Timestamp Blizzard::Time::MakeTime(const TimeRec& rec) {
    tm parts = {};
    parts.tm_year = rec.year - 1900;
    parts.tm_mon = rec.month - 1;
    parts.tm_mday = rec.day;
    parts.tm_hour = rec.hour;
    parts.tm_min = rec.minute;
    parts.tm_sec = rec.second;

    int64_t seconds = static_cast<int64_t>(timegm(&parts)) - kEpochOffsetSeconds;

    return seconds * 1000000000 + rec.nanosecond;
}

bool System_Time::TimeInit() {
    s_usePerformanceCounter = true;
    timeScaleCycles = 1000000000;
    s_absBegin = MonotonicNanoseconds();
    s_gmBegin = WallClock();

    double scale = 1.0 / static_cast<double>(timeScaleCycles);
    timeScaleNanoseconds = 1000000000.0 * scale;
    timeScaleMicroseconds = 1000000.0 * scale;
    timeScaleMilliseconds = 1000.0 * scale;
    timeScaleSeconds = scale;

    return true;
}

Blizzard::Time::Timestamp Blizzard::Time::GetTimestamp() {
    if (System_Time::timeScaleCycles == 0xFFFFFFFFFFFFFFFF) {
        System_Time::TimeInit();
    }

    return System_Time::s_gmBegin + (MonotonicNanoseconds() - System_Time::s_absBegin);
}

int64_t Blizzard::Time::Milliseconds() {
    if (System_Time::timeScaleCycles == 0xFFFFFFFFFFFFFFFF) {
        System_Time::TimeInit();
    }

    return MonotonicNanoseconds() / 1000000;
}
