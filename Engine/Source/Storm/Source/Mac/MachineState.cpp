// macOS implementation of the Storm machine-state log (Win64/MachineState.cpp), used by SErr's error and crash logs.
//
// The header lines (Exe, Time, User, Computer) keep the Windows format. The state section logs the arm64 registers of
// a crash context and a backtrace() stack with symbol names; there is no dbghelp, module list or minidump.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cxxabi.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <mach-o/dyld.h>
#include <pwd.h>
#include <sys/ucontext.h>
#include <unistd.h>
#include "Engine/Source/Storm/Source/Mac/SystemTime.h"
#include "Engine/Source/Storm/Source/MachineState.h"
#include "Engine/Source/Storm/H/storm.h"

int32_t g_memFullError;

namespace {

typedef void (*LogLineProc)(void*, const char*, ...);

const char s_monthNames[12][4] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

volatile LONG s_recursionLevel;

struct MEMDUMP {
    LogLineProc logLineProc;
    void* logLineProcParam;
};

void LogSeparatorLine(LogLineProc logLineProc, void* logLineProcParam, char character, int32_t longLine) {
    char line[80];
    uint32_t length = longLine ? 78 : 40;
    memset(line, character, length);
    line[length] = 0;
    logLineProc(logLineProcParam, "%s", line);
}

void LogExeFile(LogLineProc logLineProc, void* logLineProcParam) {
    char path[1024];
    uint32_t size = sizeof(path);

    if (_NSGetExecutablePath(path, &size) != 0) {
        strcpy(path, "<unknown>");
    }

    logLineProc(logLineProcParam, "%-10s%s", "Exe:", path);
}

void LogDateTimeString(LogLineProc logLineProc, void* logLineProcParam, const SYSTEMTIME* time) {
    uint16_t hour = time->wHour;
    char ampm = hour < 12 ? 'A' : 'P';

    if (hour > 12) {
        hour -= 12;
    } else if (!hour) {
        hour = 12;
    }

    logLineProc(logLineProcParam, "%-10s%3s %2d, %4d %2d:%02d:%02d.%03d %cM", "Time:", s_monthNames[time->wMonth - 1], time->wDay, time->wYear, hour, time->wMinute, time->wSecond, time->wMilliseconds, ampm);
}

void LogUserName(LogLineProc logLineProc, void* logLineProcParam) {
    passwd* user = getpwuid(getuid());
    logLineProc(logLineProcParam, "%-10s%s", "User:", user && user->pw_name ? user->pw_name : "<unknown>");
}

void LogComputerName(LogLineProc logLineProc, void* logLineProcParam) {
    char computerName[256];

    if (gethostname(computerName, sizeof(computerName)) != 0) {
        strcpy(computerName, "<unknown>");
    }

    computerName[sizeof(computerName) - 1] = 0;
    logLineProc(logLineProcParam, "%-10s%s", "Computer:", computerName);
}

void LogRegisters(LogLineProc logLineProc, void* logLineProcParam, const ucontext_t* context) {
    const __darwin_arm_thread_state64& state = context->uc_mcontext->__ss;

    logLineProc(logLineProcParam, "Registers:");

    for (int32_t i = 0; i < 29; i += 4) {
        char line[160];
        int32_t length = 0;

        for (int32_t j = i; j < i + 4 && j < 29; j++) {
            length += snprintf(line + length, sizeof(line) - length, "  X%-2d=%016llx", j, static_cast<unsigned long long>(state.__x[j]));
        }

        logLineProc(logLineProcParam, "%s", line);
    }

    logLineProc(logLineProcParam, "  FP =%016llx  LR =%016llx  SP =%016llx  PC =%016llx", static_cast<unsigned long long>(arm_thread_state64_get_fp(state)), static_cast<unsigned long long>(arm_thread_state64_get_lr(state)), static_cast<unsigned long long>(arm_thread_state64_get_sp(state)), static_cast<unsigned long long>(arm_thread_state64_get_pc(state)));
    logLineProc(logLineProcParam, "");
}

void LogStack(LogLineProc logLineProc, void* logLineProcParam, uint32_t framesToSkip) {
    void* frames[64];
    int32_t count = backtrace(frames, 64);

    logLineProc(logLineProcParam, "Stack Trace:");

    for (int32_t i = static_cast<int32_t>(framesToSkip); i < count; i++) {
        Dl_info info;

        if (dladdr(frames[i], &info) && info.dli_sname) {
            const char* module = info.dli_fname ? strrchr(info.dli_fname, '/') : nullptr;
            int status = 0;
            char* demangled = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
            logLineProc(logLineProcParam, "  %016llx  %s+0x%llx  (%s)", static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(frames[i])), demangled ? demangled : info.dli_sname, static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(frames[i]) - reinterpret_cast<uintptr_t>(info.dli_saddr)), module ? module + 1 : "?");
            free(demangled);
        } else {
            logLineProc(logLineProcParam, "  %016llx", static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(frames[i])));
        }
    }

    logLineProc(logLineProcParam, "");
}

void STORMAPI CmdMemOutput(void* hOutput, const MEMREPORTENTRY* entry) {
    MEMDUMP* dump = static_cast<MEMDUMP*>(hOutput);
    dump->logLineProc(dump->logLineProcParam, "%5d %5d %s(%d)", entry->unk_0010, entry->unk_0014, entry->unk_0024, entry->unk_0020 >> 1);
}

void ShowOutOfMemory(LogLineProc logLineProc, void* logLineProcParam) {
    if (!g_memFullError) {
        return;
    }

    MEMDUMP dump;
    dump.logLineProc = logLineProc;
    dump.logLineProcParam = logLineProcParam;
    SMemGenerateReport(SMEM_REPORT_BY_CALLER, CmdMemOutput, &dump);
    LogSeparatorLine(logLineProc, logLineProcParam, '-', 1);
}

}

void SMemGenerateReport(SMEMREPORTTYPE type, void (STORMAPI* callback)(void*, const MEMREPORTENTRY*), void* context) {
    StormCallService(STORM_SERVICE_GENERATEREPORT, type, callback, context);
}

void LoadMachineStateSymbols() {
}

void UnloadMachineStateSymbols() {
}

void LogComputerInfoHeader(uint32_t logOptions, void (*logLineProc)(void*, const char*, ...), void* logLineProcParam, const char* headerTitleLine, const SYSTEMTIME* time) {
    if (InterlockedIncrement(&s_recursionLevel) == 1) {
        if (!logOptions || (logOptions & LOG_OPTION_DEFAULT)) {
            logOptions |= LOG_OPTION_EXEFILE | LOG_OPTION_DATETIME | LOG_OPTION_COMPUTERNAME | LOG_OPTION_USERNAME;

            if (headerTitleLine && *headerTitleLine) {
                logOptions |= LOG_OPTION_TITLE;
            }
        }

        LogSeparatorLine(logLineProc, logLineProcParam, '=', 1);

        if ((logOptions & LOG_OPTION_TITLE) && headerTitleLine && *headerTitleLine) {
            logLineProc(logLineProcParam, "%s", headerTitleLine);
            logLineProc(logLineProcParam, "");
        }

        if (logOptions & LOG_OPTION_EXEFILE) {
            LogExeFile(logLineProc, logLineProcParam);
        }

        if (logOptions & LOG_OPTION_DATETIME) {
            SYSTEMTIME currentTime;

            if (!time) {
                GetLocalTime(&currentTime);
                time = &currentTime;
            }

            LogDateTimeString(logLineProc, logLineProcParam, time);
        }

        if (logOptions & LOG_OPTION_USERNAME) {
            LogUserName(logLineProc, logLineProcParam);
        }

        if (logOptions & LOG_OPTION_COMPUTERNAME) {
            LogComputerName(logLineProc, logLineProcParam);
        }

        LogSeparatorLine(logLineProc, logLineProcParam, '-', 1);
    }

    InterlockedDecrement(&s_recursionLevel);
}

// On macOS the context is the ucontext_t of a crash signal (see SErr), or nullptr for the current thread.
void LogMachineState(uint32_t logOptions, void (*logLineProc)(void*, const char*, ...), void* logLineProcParam, uint32_t stackFramesToSkip, CONTEXT* context) {
    if (InterlockedIncrement(&s_recursionLevel) == 1) {
        LogSeparatorLine(logLineProc, logLineProcParam, '-', 1);

        if (context) {
            LogRegisters(logLineProc, logLineProcParam, reinterpret_cast<const ucontext_t*>(context));
        }

        // Skip this function as well; a crash keeps the signal frames, which show where the fault came in.
        LogStack(logLineProc, logLineProcParam, context ? 0 : stackFramesToSkip + 1);
        LogSeparatorLine(logLineProc, logLineProcParam, '-', 1);
        ShowOutOfMemory(logLineProc, logLineProcParam);
    }

    InterlockedDecrement(&s_recursionLevel);
}

int32_t LogMiniDump(void* logfile, EXCEPTION_POINTERS* exceptionpointers, uint32_t count, const char** strings) {
    return 0;
}

int32_t LogMiniDumpIsAvailable() {
    return 0;
}
