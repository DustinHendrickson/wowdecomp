// libnx side of Storm error reporting on the Switch: the exception handler that turns a CPU fault into an SErr crash
// report, and the error applet SErr shows its messages in.

#include <switch.h>
#include <cstdio>
#include <cstring>
#include "Engine/Source/Storm/Source/Switch/CrashContext.h"

// libnx runs __libnx_exception_handler on this stack, so a stack overflow can still be reported. With
// __nx_exception_ignoredebug set, it runs even when the process was started without a debugger attached.
extern "C" {
alignas(16) u8 __nx_exception_stack[0x40000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);
u32 __nx_exception_ignoredebug = 1;
}

namespace {

volatile bool s_crashReports;
volatile bool s_crashing;
SwitchCrashContext s_context;

}

const char* SwitchCrashTypeName(uint32_t type) {
    switch (type) {
        case ThreadExceptionDesc_InstructionAbort:
            return "instruction abort";
        case SWITCH_CRASH_DATA_ABORT:
            return "data abort";
        case ThreadExceptionDesc_MisalignedPC:
            return "misaligned PC";
        case ThreadExceptionDesc_MisalignedSP:
            return "misaligned SP";
        case ThreadExceptionDesc_SError:
            return "SError";
        case ThreadExceptionDesc_BadSVC:
            return "bad SVC";
        case ThreadExceptionDesc_Trap:
            return "trap";
        default:
            return "unknown exception";
    }
}

void SwitchEnableCrashReports() {
    s_crashReports = true;
}

void SwitchShowErrorText(const char* title, const char* text) {
    ErrorApplicationConfig config;
    char message[2048];

    snprintf(message, sizeof(message), "%s\n\n%s", title ? title : "", text ? text : "");

    // From inside the exception handler a library applet can take the whole system down; the crash is in
    // Errors/<date> Crash.txt and on stderr, and the game exits back to the Homebrew Menu.
    if (s_crashing) {
        fprintf(stderr, "%s\n", message);
        return;
    }

    // The applet's short text holds 0x800 bytes; the details field repeats the message for the "Details" page.
    if (R_SUCCEEDED(errorApplicationCreate(&config, message, message))) {
        errorApplicationShow(&config);
    } else {
        fprintf(stderr, "%s\n", message);
    }
}

extern "C" void __libnx_exception_handler(ThreadExceptionDump* dump) {
    if (!s_crashReports) {
        // Before SErr is set up: let the system's fatal handling take it.
        fatalThrow(MAKERESULT(Module_HomebrewAbi, 0x100 + (dump->error_desc & 0xFF)));
    }

    s_crashing = true;
    wowdecomp_switch_console_sync();
    s_context.type = dump->error_desc;
    s_context.esr = dump->esr;
    s_context.far = dump->far.x;

    for (int i = 0; i < 29; i++) {
        s_context.x[i] = dump->cpu_gprs[i].x;
    }

    s_context.fp = dump->fp.x;
    s_context.lr = dump->lr.x;
    s_context.sp = dump->sp.x;
    s_context.pc = dump->pc.x;

    SwitchReportCrash(&s_context);
}
