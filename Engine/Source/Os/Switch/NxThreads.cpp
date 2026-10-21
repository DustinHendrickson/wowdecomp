// Thread placement on the Switch (wowdecomp_switch_thread_start in Base/Platform.h).
//
// libnx's pthread_create puts every thread on the process's default core, and Horizon only runs a thread on the cores
// in its affinity mask. Without this the game, FMOD's mixer, the audio feeder and the loaders all share core 0 while the
// other two application cores idle: audio stutters whenever the game is busy, and the frame rate suffers. Each thread
// the ports start calls this first. All three application cores are allowed, so the kernel can move threads to an idle
// core; the ideal core and the priority depend on the thread's role.

#include <switch.h>
#include "Engine/Source/Base/Platform.h"

namespace {

// Horizon priorities: lower is more urgent. The main thread runs at 0x2C.
const s32 kNormalPriority = 0x2C;
const s32 kAudioPriority = 0x24;
// Above FMOD's mixer: Horizon doesn't time-slice threads of equal priority, so a long mix could keep the thread that
// hands sound to the renderer waiting until its queue ran dry.
const s32 kAudioOutputPriority = 0x20;
const u32 kApplicationCores = 0x7;

}

extern "C" void wowdecomp_switch_thread_start(int role) {
    s32 core;
    s32 priority = kNormalPriority;

    switch (role) {
        case WOWDECOMP_THREAD_GAME:
            core = 0;
            break;
        case WOWDECOMP_THREAD_AUDIO:
            core = 2;
            priority = kAudioPriority;
            break;
        case WOWDECOMP_THREAD_AUDIO_OUTPUT:
            core = 2;
            priority = kAudioOutputPriority;
            break;
        default:
            core = 1;
            break;
    }

    svcSetThreadCoreMask(CUR_THREAD_HANDLE, core, kApplicationCores);
    svcSetThreadPriority(CUR_THREAD_HANDLE, priority);
}
