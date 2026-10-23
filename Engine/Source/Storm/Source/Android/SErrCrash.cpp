// Android side of Storm error reporting: the crash signal handler that turns a fault into an SErr crash report, and
// the text SErr shows its messages as.
//
// It follows the Switch's: the handler copies the faulting thread's registers out of the signal's ucontext into a
// SwitchCrashContext (Storm/Source/Switch/CrashContext.h, shared), and SErr's Switch path logs it to
// Errors/<date> Crash.txt with the register dump and stack of Android/MachineState.cpp.

#include <android/log.h>
#include <asm/sigcontext.h>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ucontext.h>
#include "Engine/Source/Storm/Source/Switch/CrashContext.h"

namespace {

// Crash types other than data aborts: the signal number above this base.
const uint32_t kSignalCrashBase = 0x200;

SwitchCrashContext s_context;

// The fault's exception syndrome (ESR), which tells a faulting write from a read: the kernel puts it in the signal
// frame's extension records for faults it took from user space.
uint32_t FindEsr(const ucontext_t* context) {
    const uint8_t* cursor = reinterpret_cast<const uint8_t*>(context->uc_mcontext.__reserved);
    const uint8_t* end = cursor + sizeof(context->uc_mcontext.__reserved);

    while (cursor + sizeof(_aarch64_ctx) <= end) {
        const _aarch64_ctx* header = reinterpret_cast<const _aarch64_ctx*>(cursor);

        if (!header->magic || !header->size) {
            break;
        }

        if (header->magic == ESR_MAGIC) {
            return static_cast<uint32_t>(reinterpret_cast<const esr_context*>(header)->esr);
        }

        cursor += header->size;
    }

    return 0;
}

void CrashHandler(int signal, siginfo_t* info, void* param) {
    const ucontext_t* context = static_cast<const ucontext_t*>(param);

    s_context.type = signal == SIGSEGV || signal == SIGBUS ? SWITCH_CRASH_DATA_ABORT : kSignalCrashBase + signal;
    s_context.esr = FindEsr(context);
    s_context.far = reinterpret_cast<uintptr_t>(info->si_addr);

    for (int32_t i = 0; i < 29; i++) {
        s_context.x[i] = context->uc_mcontext.regs[i];
    }

    s_context.fp = context->uc_mcontext.regs[29];
    s_context.lr = context->uc_mcontext.regs[30];
    s_context.sp = context->uc_mcontext.sp;
    s_context.pc = context->uc_mcontext.pc;

    SwitchReportCrash(&s_context);

    // SA_RESETHAND restored the default action; returning re-runs the fault, which ends the process with the signal and
    // leaves Android's own tombstone too.
}

}

const char* SwitchCrashTypeName(uint32_t type) {
    switch (type) {
        case SWITCH_CRASH_DATA_ABORT:
            return "data abort";
        case kSignalCrashBase + SIGILL:
            return "illegal instruction";
        case kSignalCrashBase + SIGFPE:
            return "floating point";
        case kSignalCrashBase + SIGTRAP:
            return "trap";
        case kSignalCrashBase + SIGABRT:
            return "abort";
        default:
            return "unknown exception";
    }
}

void SwitchEnableCrashReports() {
    // The handler runs on its own stack so a stack overflow can still be reported; threads the game starts get one of
    // their own (MacThreadSignalStack).
    static uint8_t s_alternateStack[256 * 1024];
    stack_t stack = {};
    stack.ss_sp = s_alternateStack;
    stack.ss_size = sizeof(s_alternateStack);
    sigaltstack(&stack, nullptr);

    struct sigaction action = {};
    action.sa_sigaction = CrashHandler;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    sigemptyset(&action.sa_mask);

    sigaction(SIGSEGV, &action, nullptr);
    sigaction(SIGBUS, &action, nullptr);
    sigaction(SIGILL, &action, nullptr);
    sigaction(SIGFPE, &action, nullptr);
    sigaction(SIGTRAP, &action, nullptr);
    sigaction(SIGABRT, &action, nullptr);
}

void SwitchShowErrorText(const char* title, const char* text) {
    // In the log (adb logcat) and Logs/Console.txt; the error file has it too.
    __android_log_print(ANDROID_LOG_ERROR, "WoW", "%s\n\n%s", title ? title : "", text ? text : "");
    fprintf(stderr, "%s\n\n%s\n", title ? title : "", text ? text : "");
}
