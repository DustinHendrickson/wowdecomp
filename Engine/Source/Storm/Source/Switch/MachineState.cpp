// Nintendo Switch implementation of the Storm machine-state log (Win64/MachineState.cpp), used by SErr's error and
// crash logs.
//
// The header lines (Exe, Time, User, Computer) keep the Windows format. The state section logs the registers of a crash
// (a SwitchCrashContext) and a stack walked along the frame pointers. There are no symbols on the console, so addresses
// are given as offsets into the executable as well: `aarch64-none-elf-addr2line -Cfe Wow.elf <offset>` names them.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "Engine/Source/Storm/Source/Mac/SystemTime.h"
#include "Engine/Source/Storm/Source/MachineState.h"
#include "Engine/Source/Storm/Source/Switch/CrashContext.h"
#include "Engine/Source/Storm/H/storm.h"

extern "C" int __system_argc;
extern "C" char** __system_argv;
extern "C" char __code_start[];
extern "C" char __rodata_start[];

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
    const char* path = __system_argc > 0 && __system_argv[0] ? __system_argv[0] : "<unknown>";
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

bool IsCode(uint64_t address) {
    return address >= reinterpret_cast<uintptr_t>(__code_start) && address < reinterpret_cast<uintptr_t>(__rodata_start);
}

void LogAddress(LogLineProc logLineProc, void* logLineProcParam, uint64_t address) {
    if (IsCode(address)) {
        logLineProc(logLineProcParam, "  %016llx  Wow.elf+0x%llx", static_cast<unsigned long long>(address), static_cast<unsigned long long>(address - reinterpret_cast<uintptr_t>(__code_start)));
    } else {
        logLineProc(logLineProcParam, "  %016llx", static_cast<unsigned long long>(address));
    }
}

void LogRegisters(LogLineProc logLineProc, void* logLineProcParam, const SwitchCrashContext* context) {
    logLineProc(logLineProcParam, "Registers:");

    for (int32_t i = 0; i < 29; i += 4) {
        char line[160];
        int32_t length = 0;

        for (int32_t j = i; j < i + 4 && j < 29; j++) {
            length += snprintf(line + length, sizeof(line) - length, "  X%-2d=%016llx", j, static_cast<unsigned long long>(context->x[j]));
        }

        logLineProc(logLineProcParam, "%s", line);
    }

    logLineProc(logLineProcParam, "  FP =%016llx  LR =%016llx  SP =%016llx  PC =%016llx", static_cast<unsigned long long>(context->fp), static_cast<unsigned long long>(context->lr), static_cast<unsigned long long>(context->sp), static_cast<unsigned long long>(context->pc));
    logLineProc(logLineProcParam, "  ESR=%08x  FAR=%016llx  Code base=%016llx", context->esr, static_cast<unsigned long long>(context->far), static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(__code_start)));
    logLineProc(logLineProcParam, "");
}

// Frame records are {previous fp, return address}. Clang keeps frame pointers on AArch64, so the chain is complete
// through the game's code; it stops at the first record that doesn't look like one.
void LogStack(LogLineProc logLineProc, void* logLineProcParam, uint64_t pc, uint64_t lr, uint64_t fp, uint32_t framesToSkip) {
    logLineProc(logLineProcParam, "Stack Trace:");

    if (pc) {
        LogAddress(logLineProc, logLineProcParam, pc);

        if (lr) {
            LogAddress(logLineProc, logLineProcParam, lr);
        }
    }

    for (int32_t depth = 0; fp && !(fp & 0xF) && depth < 64; depth++) {
        const uint64_t* record = reinterpret_cast<const uint64_t*>(fp);
        uint64_t next = record[0];
        uint64_t ret = record[1];

        if (!ret) {
            break;
        }

        if (framesToSkip) {
            framesToSkip--;
        } else {
            LogAddress(logLineProc, logLineProcParam, ret);
        }

        if (next <= fp) {
            break;
        }

        fp = next;
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
            logLineProc(logLineProcParam, "%-10s%s", "User:", "Player");
        }

        if (logOptions & LOG_OPTION_COMPUTERNAME) {
            logLineProc(logLineProcParam, "%-10s%s", "Computer:", "Nintendo Switch");
        }

        LogSeparatorLine(logLineProc, logLineProcParam, '-', 1);
    }

    InterlockedDecrement(&s_recursionLevel);
}

// On the Switch the context is a SwitchCrashContext (see SErr), or nullptr for the current thread.
void LogMachineState(uint32_t logOptions, void (*logLineProc)(void*, const char*, ...), void* logLineProcParam, uint32_t stackFramesToSkip, CONTEXT* context) {
    if (InterlockedIncrement(&s_recursionLevel) == 1) {
        LogSeparatorLine(logLineProc, logLineProcParam, '-', 1);

        if (context) {
            const SwitchCrashContext* crash = reinterpret_cast<const SwitchCrashContext*>(context);
            LogRegisters(logLineProc, logLineProcParam, crash);
            LogStack(logLineProc, logLineProcParam, crash->pc, crash->lr, crash->fp, 0);
        } else {
            // Skip this function as well.
            LogStack(logLineProc, logLineProcParam, 0, 0, reinterpret_cast<uintptr_t>(__builtin_frame_address(0)), stackFramesToSkip);
        }

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
