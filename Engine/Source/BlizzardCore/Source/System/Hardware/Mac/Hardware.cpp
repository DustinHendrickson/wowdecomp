// macOS implementation of Blizzard::Hardware (Win64/Hardware.cpp).
//
// There is no CPUID on arm64. The feature bits keep their x86 meaning for the code that tests them: SSE and SSE2 are
// reported because the SSE paths run through sse2neon, MMX and 3DNow! are not. Vendor stays 0 (unknown).

#include <cstdint>
#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif
#include <unistd.h>
#include "Engine/Source/BlizzardCore/Source/System/Hardware/Hardware.h"
#include "Engine/Source/BlizzardCore/Source/System/Lock/Lock.h"

Blizzard::Hardware::ProcessorFeatures System_Hardware::s_processorFeatures;
bool System_Hardware::s_initialized;
Blizzard::Lock::Atomic System_Hardware::s_initCount;

namespace {

const uint32_t kFeatureTimestampCounter = 0x1;
const uint32_t kFeatureSSE = 0x4;
const uint32_t kFeatureSSE2 = 0x10;
const uint32_t kFeatureMultiCore = 0x40;

uint32_t SysctlCount(const char* name) {
#if defined(__APPLE__)
    uint32_t value = 0;
    size_t size = sizeof(value);

    return sysctlbyname(name, &value, &size, nullptr, 0) == 0 ? value : 0;
#elif defined(__ANDROID__)
    // Logical and physical alike: the cores the kernel knows, big and little.
    long count = sysconf(_SC_NPROCESSORS_CONF);
    return count > 0 ? static_cast<uint32_t>(count) : 0;
#else
    return 0;
#endif
}

}

bool System_Hardware::ReadCPUID(CPUIDInfo* info) {
    return false;
}

void System_Hardware::InitProcessorFeatures() {
    uint32_t logical = SysctlCount("hw.logicalcpu");
    uint32_t physical = SysctlCount("hw.physicalcpu");

    s_processorFeatures.m_vendor = 0;
    s_processorFeatures.m_processorCount = logical ? logical : 1;
    s_processorFeatures.m_coreCount = physical ? physical : 1;
    s_processorFeatures.m_features = kFeatureTimestampCounter | kFeatureSSE | kFeatureSSE2;

    if (s_processorFeatures.m_coreCount > 1) {
        s_processorFeatures.m_features |= kFeatureMultiCore;
    }
}

uint32_t Blizzard::Hardware::GetProcessorFeatures() {
    if (!System_Hardware::s_initialized) {
        if (System_Hardware::s_initCount.Increment() == 1) {
            System_Hardware::InitProcessorFeatures();
            System_Hardware::s_initialized = true;
            return System_Hardware::s_processorFeatures.m_features;
        }

        do {
            usleep(1000);
        } while (!System_Hardware::s_initialized);
    }

    return System_Hardware::s_processorFeatures.m_features;
}
