// CPU and GPU clocks for the game (NxInput.h). By default homebrew runs at the console's normal game clocks (CPU 1020
// MHz); the movie decoder and FMOD's software mixing are CPU bound, and on them the game played movies slowly and its
// sound stuttered. While it runs it sets the highest clocks Nintendo itself uses: the CPU at 1785 MHz (the boost games
// get during loading), the GPU at 768 MHz docked or 460.8 MHz handheld. Memory is left alone. The clocks it found are
// put back when the game exits; the system resets them on docking, sleep and the HOME menu, so Input.cpp applies them
// again then. Debug.txt with "boost=0" leaves the clocks alone.

#include <switch.h>
#include <cstdio>
#include <cstdlib>
#include "Engine/Source/Os/Switch/NxInput.h"

namespace {

const u32 kCpuHz = 1785000000;
const u32 kGpuDockedHz = 768000000;
const u32 kGpuHandheldHz = 460800000;

bool s_saved;
u32 s_originalCpuHz;
u32 s_originalGpuHz;

// clkrst on 8.0.0 and later, pcv before.
bool GetRate(PcvModuleId id, PcvModule module, u32* hz) {
    if (hosversionAtLeast(8, 0, 0)) {
        ClkrstSession session;

        if (R_FAILED(clkrstOpenSession(&session, id, 3))) {
            return false;
        }

        Result rc = clkrstGetClockRate(&session, hz);
        clkrstCloseSession(&session);
        return R_SUCCEEDED(rc);
    }

    return R_SUCCEEDED(pcvGetClockRate(module, hz));
}

bool SetRate(PcvModuleId id, PcvModule module, u32 hz) {
    if (hosversionAtLeast(8, 0, 0)) {
        ClkrstSession session;

        if (R_FAILED(clkrstOpenSession(&session, id, 3))) {
            return false;
        }

        Result rc = clkrstSetClockRate(&session, hz);
        clkrstCloseSession(&session);
        return R_SUCCEEDED(rc);
    }

    return R_SUCCEEDED(pcvSetClockRate(module, hz));
}

bool OpenService() {
    return R_SUCCEEDED(hosversionAtLeast(8, 0, 0) ? clkrstInitialize() : pcvInitialize());
}

void CloseService() {
    if (hosversionAtLeast(8, 0, 0)) {
        clkrstExit();
    } else {
        pcvExit();
    }
}

void Restore() {
    if (!s_saved || !OpenService()) {
        return;
    }

    SetRate(PcvModuleId_CpuBus, PcvModule_CpuBus, s_originalCpuHz);
    SetRate(PcvModuleId_GPU, PcvModule_GPU, s_originalGpuHz);
    CloseService();
    s_saved = false;
}

}

void NxClocksRestore() {
    Restore();
}

void NxClocksApply(bool docked) {
    if (!OpenService()) {
        fprintf(stderr, "Clocks: no clock service; running at the default clocks\n");
        return;
    }

    if (!s_saved) {
        s_saved = GetRate(PcvModuleId_CpuBus, PcvModule_CpuBus, &s_originalCpuHz) && GetRate(PcvModuleId_GPU, PcvModule_GPU, &s_originalGpuHz);

        if (s_saved) {
            atexit(Restore);
        }
    }

    u32 gpuHz = docked ? kGpuDockedHz : kGpuHandheldHz;
    bool cpu = SetRate(PcvModuleId_CpuBus, PcvModule_CpuBus, kCpuHz);
    bool gpu = SetRate(PcvModuleId_GPU, PcvModule_GPU, gpuHz);
    CloseService();

    fprintf(stderr, "Clocks: CPU %u MHz%s, GPU %u MHz%s (%s)\n", kCpuHz / 1000000, cpu ? "" : " refused", gpuHz / 1000000, gpu ? "" : " refused", docked ? "docked" : "handheld");
}
