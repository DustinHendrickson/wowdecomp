#ifndef ENGINE_SOURCE_STORM_SOURCE_SWITCH_CRASHCONTEXT_H
#define ENGINE_SOURCE_STORM_SOURCE_SWITCH_CRASHCONTEXT_H

// A crash on the Switch, as SErr and MachineState log it: the registers from libnx's ThreadExceptionDump, copied out so
// Storm doesn't need <switch.h> (whose names collide with the game's). Stands in for the ucontext_t of the macOS crash
// signal.

#include <cstdint>

enum {
    SWITCH_CRASH_DATA_ABORT = 0x101,
};

struct SwitchCrashContext {
    uint32_t type;      // ThreadExceptionDesc; data aborts arrive as 0x101
    uint32_t esr;
    uint64_t far;
    uint64_t x[29];
    uint64_t fp;
    uint64_t lr;
    uint64_t sp;
    uint64_t pc;
};

const char* SwitchCrashTypeName(uint32_t type);

// SErr: logs the crash and shows it, as the macOS crash signal handler does. Doesn't return.
void SwitchReportCrash(SwitchCrashContext* context);

// SErrCrash.cpp: lets libnx's exception handler report crashes (SErrCatchUnhandledExceptions).
void SwitchEnableCrashReports();

// SErrCrash.cpp: the error applet with a title and message, for SErr's dialogs.
void SwitchShowErrorText(const char* title, const char* text);

#endif
