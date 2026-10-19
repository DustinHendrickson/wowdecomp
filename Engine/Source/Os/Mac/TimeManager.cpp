// macOS implementation of Os/Win64/TimeManager.cpp.
//
// SystemMethod2 ("QueryPerformanceCounter" on Windows) is a nanosecond monotonic clock here, so there is nothing to
// calibrate: it is monotonic across cores and never drifts from the tick count. SystemMethod1 stays the millisecond
// tick count.

#include <cstdint>
#include <ctime>
#include <new>
#include <unistd.h>
#include "Engine/Source/Os/TimeManager.h"
#include "Engine/Source/Storm/H/storm.h"

static OsTimeManager* s_OsTimeMgr;

namespace {

const int64_t kClocksPerSecond = 1000000000;

int64_t Clocks() {
    return static_cast<int64_t>(clock_gettime_nsec_np(CLOCK_UPTIME_RAW));
}

int64_t TickCount() {
    return Clocks() / 1000000;
}

}

OsTimeManager::TimingMethod OsTimeManager::Calibrate() {
    this->m_performanceFrequency = kClocksPerSecond;
    this->m_timingTestError = 0;

    return SystemMethod2;
}

uint32_t OsTimeGetTestError() {
    return s_OsTimeMgr->m_timingTestError;
}

OsTimeManager::TimingMethod OsTimeGetTimingMethod() {
    return s_OsTimeMgr->m_timingMethod;
}

const char* GetTimingMethodName(OsTimeManager::TimingMethod method) {
    switch (method) {
        case OsTimeManager::NotSet:
            return "[Not Set]";
        case OsTimeManager::BestAvailable:
            return "[Best Available]";
        case OsTimeManager::SystemMethod1:
            return "GetTickCount";
        case OsTimeManager::SystemMethod2:
            return "mach_absolute_time";
        default:
            return "[Unknown]";
    }
}

uint64_t OsTimeManager::Snapshot() {
    int64_t clocks = this->m_timingMethod == SystemMethod2 ? Clocks() : TickCount();
    return static_cast<int64_t>(static_cast<double>(clocks) * this->m_scaleToMs + this->m_timeBegin);
}

uint32_t OsGetAsyncTimeMs() {
    return static_cast<uint32_t>(s_OsTimeMgr->Snapshot());
}

uint64_t OsGetAsyncTimeClocks() {
    return s_OsTimeMgr->m_timingMethod == OsTimeManager::SystemMethod2 ? Clocks() : TickCount();
}

int64_t OsGetAsyncClocksPerSecond() {
    return s_OsTimeMgr->m_timingMethod == OsTimeManager::SystemMethod2 ? s_OsTimeMgr->m_performanceFrequency : 1000;
}

OsTimeManager::OsTimeManager(OsTimeManager::TimingMethod method) {
    this->m_timingMethod = NotSet;
    OsTimeManager::TimingMethod calibrated = this->Calibrate();

    if (method != BestAvailable && calibrated != method) {
        calibrated = method;
    }

    this->m_timingMethod = calibrated;
    int64_t clocksPerSecond = calibrated == SystemMethod2 ? this->m_performanceFrequency : 1000;
    this->m_scaleToMs = 1000.0 / static_cast<double>(clocksPerSecond);
    this->m_timeBegin = 0.0;
}

void OsTimeManagerCreate(OsTimeManager::TimingMethod method) {
    s_OsTimeMgr = new (SMemAlloc(sizeof(OsTimeManager), __FILE__, __LINE__, SMEM_FLAG_ZEROMEMORY)) OsTimeManager(method);
}

void OsTimeManagerDestroy() {
    if (s_OsTimeMgr) {
        delete s_OsTimeMgr;
        s_OsTimeMgr = nullptr;
    }
}

void OsSleep(uint32_t milliseconds) {
    usleep(static_cast<useconds_t>(milliseconds) * 1000);
}
