// macOS implementation of Os/Win64/System.cpp.
//
// OsGetProcessorId / OsGetOsId keep the Windows values ('x86', 'Win'): they go into the login challenge, and this
// client speaks the Windows client's protocol (version checks, Warden), not the Mac client's.

#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <pwd.h>
#include <spawn.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include "Engine/Source/BlizzardCore/Source/System/Lock/Mac/Waitable.h"
#include "Engine/Source/Os/OsFile.h"
#include "Engine/Source/Os/Processor.h"
#include "Engine/Source/Os/System.h"
#include "Engine/Source/Os/TimeManager.h"
#include "Engine/Source/Storm/H/storm.h"

extern char** environ;

static int32_t s_sleepInBackground = 1;
static const char* s_osTypeNames[] = { "Unknown", "Win95", "Win95OSR2", "Win98", "Win98SE", "WinME", "WinNT4", "Win2000", "WinXP", "Win2003", "Win9X_Other", "WinNT_Other", "MacOS9", "MacOSX", "Linux", "WinVista" };
static int32_t s_haveProcessorFeatures;
static int32_t s_processorVendor;
static uint32_t s_processorFeatures;
static int32_t s_processorCores;
static int32_t s_processorCount;
static uintptr_t s_textStart;
static uintptr_t s_textEnd;
static int32_t s_cpuLogEnabled;
static uint32_t s_backgroundSleepMs;
static uint64_t s_cpuTicksPerSecond;

namespace {

uint32_t SysctlCount(const char* name) {
    uint32_t value = 0;
    size_t size = sizeof(value);
    return sysctlbyname(name, &value, &size, nullptr, 0) == 0 ? value : 0;
}

uint8_t HexValue(char c) {
    if (c >= '0' && c <= '9') {
        return static_cast<uint8_t>(c - '0');
    }

    if (c >= 'a' && c <= 'f') {
        return static_cast<uint8_t>(c - 'a' + 10);
    }

    if (c >= 'A' && c <= 'F') {
        return static_cast<uint8_t>(c - 'A' + 10);
    }

    return 0;
}

}

int32_t IOsGetProcessorFeatures(CpuIdInfo* info) {
    // No CPUID on arm64: report the brand string and leave the CPUID leaves empty.
    memset(info, 0, sizeof(CpuIdInfo));
    size_t size = sizeof(info->m_brand) - 1;
    sysctlbyname("machdep.cpu.brand_string", info->m_brand, &size, nullptr, 0);

    return 0;
}

void OsSystemEnableCpuLog() {
    s_cpuLogEnabled = 1;
}

void IOsSystemCpuLog(const CpuIdInfo* info) {
    if (!s_cpuLogEnabled) {
        return;
    }

    OsCreateDirectory("Logs", 0);

    HSLOG log;
    SLogCreate("Logs\\cpu.log", 0, &log);
    SLogWrite(log, "vendor: %d", s_processorVendor);
    SLogWrite(log, "features: %08X", s_processorFeatures & 0x7FFFFFFF);
    SLogWrite(log, "cores: %d", s_processorCores);
    SLogWrite(log, "processors: %d", s_processorCount);
    SLogWrite(log, "processor brand string= %s", info->m_brand);
    SLogClose(log);
}

uint32_t OsGetNumberOfProcessors() {
    uint32_t count = SysctlCount("hw.logicalcpu");
    return count ? count : 1;
}

int32_t OsSleepInBackground() {
    return s_sleepInBackground;
}

void OsSetSleepInBackground(int32_t sleep) {
    s_sleepInBackground = sleep;
}

uint32_t OsGetBackgroundSleepMs() {
    return s_backgroundSleepMs;
}

void OsSetBackgroundSleepMs(uint32_t milliseconds) {
    s_backgroundSleepMs = milliseconds;
}

OsType OsGetVersion() {
    return OsType_MacOSX;
}

void OsGetVersionString(char* buffer, int32_t size) {
    SStrCopy(buffer, s_osTypeNames[OsGetVersion()], size);
}

uint32_t OsGetProcessorId() {
    return 0x783836;
}

uint32_t OsGetOsId() {
    return 0x57696E;
}

int32_t OsGetComputerName(char* buffer, uint32_t* size) {
    if (gethostname(buffer, *size) != 0) {
        return 0;
    }

    buffer[*size - 1] = '\0';
    *size = static_cast<uint32_t>(strlen(buffer));

    return 1;
}

int32_t OsGetUserName(char* buffer, uint32_t* size) {
    passwd* user = getpwuid(getuid());

    if (!user || !user->pw_name || strlen(user->pw_name) + 1 > *size) {
        return 0;
    }

    SStrCopy(buffer, user->pw_name, *size);
    *size = static_cast<uint32_t>(strlen(buffer) + 1);

    return 1;
}

uint64_t OsGetPhysicalMemory() {
    uint64_t memory = 0;
    size_t size = sizeof(memory);
    sysctlbyname("hw.memsize", &memory, &size, nullptr, 0);

    return memory;
}

void* CreateNamedEventA(const char* name, int32_t manualReset) {
    // Named events let Windows clients see each other; macOS keeps an unnamed one for the same callers.
    return MacWaitable::CreateEvent(manualReset != 0, false);
}

void OsFindTextSectionBounds() {
    unsigned long size = 0;
    const mach_header_64* header = reinterpret_cast<const mach_header_64*>(_dyld_get_image_header(0));
    uint8_t* text = getsectiondata(header, "__TEXT", "__text", &size);

    if (!text) {
        SErrDisplayAppFatal("Unable to find .text section");
    }

    s_textStart = reinterpret_cast<uintptr_t>(text);
    s_textEnd = s_textStart + size;
}

void OsValidateFunctionPointer(const void* ptr) {
    if (!s_textStart || !s_textEnd) {
        OsFindTextSectionBounds();
    }

    if (reinterpret_cast<uintptr_t>(ptr) < s_textStart || reinterpret_cast<uintptr_t>(ptr) >= s_textEnd) {
        char buffer[64];
        SStrPrintf(buffer, sizeof(buffer), "Invalid function pointer: %p", ptr);
        SErrDisplayAppFatal(buffer);
    }
}

int32_t IOsParseProcessorFrequency(const char* brand) {
    // Apple Silicon brand strings carry no clock speed.
    return 0;
}

int32_t IOsGetPowerProfFrequency() {
    return 0;
}

int32_t OsIsRemoteSession() {
    return 0;
}

int32_t OsLaunchURL(const char* url) {
    if (!url || !*url) {
        return 0;
    }

    if (SStrLen(url) >= 1024) {
        SErrSetLastError(STORM_ERROR_INVALID_PARAMETER);
        return 0;
    }

    // Percent escapes are decoded first, as on Windows, then /usr/bin/open hands the URL or file to its default app.
    char file[1024];
    char* dst = file;

    for (const char* src = url; *src; src++, dst++) {
        if (*src == '%' && src[1] && src[2]) {
            *dst = static_cast<char>((HexValue(src[1]) << 4) | HexValue(src[2]));
            src += 2;
        } else {
            *dst = *src;
        }
    }

    *dst = '\0';

    char* argv[] = { const_cast<char*>("/usr/bin/open"), file, nullptr };
    pid_t process;

    return posix_spawn(&process, "/usr/bin/open", nullptr, nullptr, argv, environ) == 0;
}

uint32_t OsGetProcessorFeaturesEx(int32_t& vendor) {
    if (s_haveProcessorFeatures) {
        vendor = s_processorVendor;
        return s_processorFeatures;
    }

    s_haveProcessorFeatures = 1;
    s_processorCount = static_cast<int32_t>(OsGetNumberOfProcessors());
    uint32_t physical = SysctlCount("hw.physicalcpu");
    s_processorCores = physical ? static_cast<int32_t>(physical) : 1;

    // The x86 feature bits that code tests: TSC (1) and SSE (4) / SSE2 (0x10), which sse2neon provides; 0x40 is
    // multi-core. The vendor is 4, "unidentified".
    s_processorVendor = 4;
    uint32_t features = 0x1 | 0x4 | 0x10;

    if (s_processorCores > 1) {
        features |= 0x40;
    }

    s_processorFeatures = features | 0x80000000;

    CpuIdInfo info;
    IOsGetProcessorFeatures(&info);
    IOsSystemCpuLog(&info);

    s_cpuTicksPerSecond = OsGetAsyncClocksPerSecond();

    vendor = s_processorVendor;
    return s_processorFeatures;
}

uint64_t OsGetProcessorTicksPerSecond() {
    if (!s_cpuTicksPerSecond) {
        int32_t vendor;
        OsGetProcessorFeaturesEx(vendor);
    }

    return s_cpuTicksPerSecond;
}
