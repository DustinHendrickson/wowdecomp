// macOS implementation of Storm error reporting (Win64/SErr.cpp).
//
// The flow is the Windows one: an error is written to "Errors/<date> Error.txt" next to the game (or "Crash" for a
// fault), registered handlers get a chance to take it, and otherwise a dialog asks whether to keep running, break to
// the debugger or terminate. The dialog is a CFUserNotification alert, which works before (and without) an
// NSApplication. Crashes arrive through sigaction handlers instead of an unhandled-exception filter. There are no
// message-table resources, so error strings come from the built-in texts.

#include <CoreFoundation/CoreFoundation.h>
#include <cerrno>
#include <csignal>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/ucontext.h>
#include <unistd.h>
#include "Engine/Source/BlizzardCore/Source/System/Process/Process.h"
#include "Engine/Source/Storm/Source/Mac/SystemTime.h"
#include "Engine/Source/Storm/Source/MachineState.h"
#include "Engine/Source/Storm/Source/SOption.h"
#include "Engine/Source/Storm/H/storm.h"

#define SERR_STR_ERROR 0
#define SERR_STR_CRITICAL 1
#define SERR_STR_PROGRAM 2
#define SERR_STR_FILE_LINE 3
#define SERR_STR_FUNCTION 4
#define SERR_STR_OBJECT 5
#define SERR_STR_HANDLE 6
#define SERR_STR_EXPRESSION 7
#define SERR_STR_DESCRIPTION 8
#define SERR_STR_TERMINATE 9
#define SERR_STR_KEEP_RUNNING 10
#define SERR_STR_FILE 11
#define SERR_STR_BREAK 12
#define SERR_STR_BREAK_OR_RUN 13
#define SERR_STR_EXCEPTION 14
#define SERR_STR_ACCESS_VIOLATION 15
#define SERR_STR_READ 16
#define SERR_STR_WRITTEN 17

namespace {

// MessageBox results, as the Windows flow compares them.
const int32_t kIdOk = 1;
const int32_t kIdCancel = 2;
const int32_t kIdYes = 6;
const int32_t kIdNo = 7;

const char* const s_displaystr[20] = {
    "ERROR #%u (0x%08x) %s",
    "This application has encountered a critical error:\n\n%s\n",
    "Program:\t%s\n",
    "File:\t%s\nLine:\t%d\n",
    "Function:\t%s\n",
    "Object:\t%s\n",
    "Handle:\t%s\n",
    "Expr:\t%s\n\n",
    "\n%s\n\n",
    "Press OK to terminate the application.",
    "Do you wish to keep running anyway?\n\tOK:\tKeep running\n\tCancel:\tTerminate application",
    "File:\t%s\n",
    "Do you wish to break to the debugger?\n\tYes:\tBreak to debugger\n\tNo:\tTerminate application",
    "Do you wish to break to the debugger?\n\tYes:\tBreak to debugger\n\tNo:\tTerminate application\n\tCancel:\tKeep running",
    "Exception:\t%s\n",
    // macOS prints %p with its 0x prefix, which the Windows text spells out.
    "The instruction at \"%p\" referenced memory at \"%p\".\nThe memory could not be \"%s\".",
    "read",
    "written",
    "",
    "",
};

struct APPFATINFO {
    const char* filename;
    int32_t linenumber;
    uint32_t threadId;
};

typedef int32_t (STORMAPI* ErrorHandler)(uint32_t, const char*, const char*, int32_t, const char*);

// A fixed table instead of the Windows heap list: error reporting must not depend on the allocators it may be
// reporting on.
const int32_t kMaxHandlers = 16;
ErrorHandler s_handlers[kMaxHandlers];
int32_t s_handlerCount;

volatile LONG s_critsectinit = -1;
CRITICAL_SECTION s_critsect;
CRITICAL_SECTION s_exceptioncritsect;
int32_t s_displaying;
APPFATINFO s_appFatInfo;
uint32_t s_lasterror;
char s_logtitle[128];
int32_t (STORMAPI* s_logCallback)(char*, uint32_t);
uint32_t s_noMiniDumps;
char s_logLastFile[MAX_PATH];

// Set by the crash handler for the error it reports: the faulting thread's ucontext.
ucontext_t* s_crashContext;

const char* GetErrorString(uint32_t id) {
    return s_displaystr[id];
}

void InternalEnterCriticalSection(CRITICAL_SECTION* critsect) {
    if (InterlockedIncrement(&s_critsectinit)) {
        InterlockedDecrement(&s_critsectinit);
    } else {
        InitializeCriticalSection(&s_critsect);
        InitializeCriticalSection(&s_exceptioncritsect);
    }

    EnterCriticalSection(critsect);
}

void InternalLeaveCriticalSection(CRITICAL_SECTION* critsect) {
    LeaveCriticalSection(critsect);
}

int32_t CanBreakToDebugger() {
    kinfo_proc info = {};
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid() };
    size_t size = sizeof(info);

    return sysctl(mib, 4, &info, &size, nullptr, 0) == 0 && (info.kp_proc.p_flag & P_TRACED) != 0;
}

void Breakpoint() {
    if (CanBreakToDebugger()) {
        __builtin_debugtrap();
    }
}

void WriteLine(void* param, const char* format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer) - 2, format ? format : "", args);
    va_end(args);
    buffer[sizeof(buffer) - 2] = 0;
    SStrPack(buffer, "\n", sizeof(buffer));

    FILE* file = static_cast<FILE*>(param);
    fputs(buffer, file);
}

void WriteMessageToLog(FILE* logfile, const char* message) {
    size_t length = SStrLen(message);
    fputc('\n', logfile);
    fputs(message, logfile);

    if (!length || message[length - 1] != '\n') {
        fputc('\n', logfile);
    }
}

FILE* CreateErrorLogFile(const char* suffix, const char* ext, char* logpath, uint32_t logpathchars, const SYSTEMTIME* time) {
    char logfilename[MAX_PATH];

    if (!suffix) {
        suffix = " Log";
    }

    Blizzard::Process::GetProcessDirectory(logpath, logpathchars);
    size_t directory = SStrLen(logpath);
    SStrPack(logpath, "Errors/", logpathchars);

    if (mkdir(logpath, 0755) != 0 && errno != EEXIST) {
        logpath[directory] = 0;
    }

    snprintf(logfilename, sizeof(logfilename), "%04d-%02d-%02d %02d.%02d.%02d %s.%s", time->wYear, time->wMonth, time->wDay, time->wHour, time->wMinute, time->wSecond, suffix, ext);
    SStrPack(logpath, logfilename, logpathchars);

    return fopen(logpath, "w");
}

FILE* CreateErrorLog(const char* suffix, const char* message, const SYSTEMTIME* time) {
    char logpath[MAX_PATH];
    SYSTEMTIME localTime;

    if (!time) {
        GetLocalTime(&localTime);
        time = &localTime;
    }

    FILE* logfile = CreateErrorLogFile(suffix, "txt", logpath, sizeof(logpath), time);

    if (!logfile) {
        return nullptr;
    }

    LogComputerInfoHeader(LOG_OPTION_DEFAULT, WriteLine, logfile, s_logtitle, time);
    WriteMessageToLog(logfile, message);

    if (s_logCallback) {
        char* buffer = static_cast<char*>(alloca(5200));
        buffer[0] = 0;
        s_logCallback(buffer, 5200);

        if (strlen(buffer)) {
            WriteMessageToLog(logfile, buffer);
        }
    }

    SStrCopy(s_logLastFile, logpath, sizeof(s_logLastFile));

    return logfile;
}

const char* SErrGetErrorText(uint32_t errorcode) {
    switch (errorcode) {
        case STORM_ERROR_ASSERTION:
            return "Assertion Failure";
        case STORM_ERROR_NOT_ARCHIVE:
            return "Not Archive";
        case STORM_ERROR_VERSION_MISMATCH:
            return "Version Mismatch";
        case STORM_ERROR_MEM_NOT_ALLOCATED:
            return "Memory Already Freed";
        case STORM_ERROR_MEM_CORRUPTED:
            return "Memory Corrupt";
        case STORM_ERROR_MEM_INVALID:
            return "Memory Invalid Block";
        case STORM_ERROR_MEM_NOT_FREED:
            return "Memory Never Released";
        case STORM_ERROR_RESOURCES_NOT_RELEASED:
            return "Handle Never Released";
        case STORM_ERROR_OUT_OF_BOUNDS:
            return "Access Out Of Bounds";
        case STORM_ERROR_FILE_CORRUPTED:
            return "File Corrupt";
        case STORM_ERROR_FATAL:
            return "Fatal Exception";
        case STORM_ERROR_FATAL_CONDITION:
            return "Fatal Condition";
        default:
            return "";
    }
}

int32_t UndecorateObjectName(const char* source, char* dest, uint32_t destchars) {
    if (SStrLen(source) < 6 || source[0] != '.' || source[3] != 'U' || !strpbrk(source + 4, "?@")) {
        return 0;
    }

    SStrCopy(dest, source + 4, destchars);
    char* end = strpbrk(dest, "?@");

    if (!end) {
        return 0;
    }

    *end = 0;

    for (char* c = end - 1; c >= dest && *c == '_'; --c) {
        *c = 0;
    }

    SStrPack(dest, " (", destchars);
    SStrPack(dest, source, destchars);
    SStrPack(dest, ")", destchars);

    return 1;
}

CFStringRef MakeString(const char* text) {
    CFStringRef string = CFStringCreateWithCString(kCFAllocatorDefault, text, kCFStringEncodingUTF8);
    return string ? string : CFStringCreateWithCString(kCFAllocatorDefault, text, kCFStringEncodingMacRoman);
}

// MessageBox on CFUserNotification. Buttons are given right to left: the default (rightmost) one first.
int32_t ShowAlert(const char* title, const char* text, bool warning, const char* const* buttons, const int32_t* results, int32_t buttonCount) {
    CFStringRef header = MakeString(title);
    CFStringRef message = MakeString(text);
    CFStringRef labels[3] = {};

    for (int32_t i = 0; i < buttonCount; i++) {
        labels[i] = MakeString(buttons[i]);
    }

    CFOptionFlags response = kCFUserNotificationDefaultResponse;
    SInt32 shown = CFUserNotificationDisplayAlert(0, warning ? kCFUserNotificationCautionAlertLevel : kCFUserNotificationStopAlertLevel, nullptr, nullptr, nullptr, header, message, labels[0], labels[1], labels[2], &response);

    for (int32_t i = 0; i < 3; i++) {
        if (labels[i]) {
            CFRelease(labels[i]);
        }
    }

    CFRelease(message);
    CFRelease(header);

    if (shown != 0) {
        return results[0];
    }

    switch (response & 0x3) {
        case kCFUserNotificationAlternateResponse:
            return buttonCount > 1 ? results[1] : results[0];
        case kCFUserNotificationOtherResponse:
            return buttonCount > 2 ? results[2] : results[0];
        default:
            return results[0];
    }
}

const char* SignalName(int signal) {
    switch (signal) {
        case SIGSEGV:
            return "ACCESS_VIOLATION";
        case SIGBUS:
            return "BUS_ERROR";
        case SIGILL:
            return "ILLEGAL_INSTRUCTION";
        case SIGFPE:
            return "FLOATING_POINT";
        case SIGTRAP:
            return "BREAKPOINT";
        default:
            return "unknown exception";
    }
}

void CrashHandler(int signal, siginfo_t* info, void* param) {
    ucontext_t* context = static_cast<ucontext_t*>(param);

    InternalEnterCriticalSection(&s_exceptioncritsect);

    if (!s_crashContext) {
        s_crashContext = context;
    }

    InternalLeaveCriticalSection(&s_exceptioncritsect);

    void* pc = reinterpret_cast<void*>(arm_thread_state64_get_pc(context->uc_mcontext->__ss));
    char buffer[120];
    char message[256];
    message[0] = 0;
    SStrPrintf(buffer, sizeof(buffer), "0x%08X (%s) at %p", static_cast<uint32_t>(signal), SignalName(signal), pc);

    if (signal == SIGSEGV || signal == SIGBUS) {
        // ESR bit 6 (WnR) tells a faulting write from a read.
        bool written = (context->uc_mcontext->__es.__esr >> 6) & 1;
        SStrPrintf(message, sizeof(message), GetErrorString(SERR_STR_ACCESS_VIOLATION), pc, info->si_addr, GetErrorString(written ? SERR_STR_WRITTEN : SERR_STR_READ));
    }

    SErrDisplayError(STORM_ERROR_FATAL, buffer, SLOG_EXCEPTION, message, 0, 1, 0x11111111);

    // SA_RESETHAND restored the default action; returning re-runs the fault and ends the process with the signal.
}

}

void SErrInitialize() {
    g_opt.serrleaksilentwarning = 1;
    SRegLoadValue("Internal", "No Minidumps", 0, &s_noMiniDumps);
}

uint32_t STORMAPI SErrGetLastError() {
    return s_lasterror;
}

int32_t STORMAPI SErrIsDisplayingError() {
    return s_displaying;
}

void STORMAPI SErrPrepareAppFatal(const char* filename, int32_t linenumber) {
    InternalEnterCriticalSection(&s_critsect);
    s_appFatInfo.filename = filename;
    s_appFatInfo.linenumber = linenumber;
    s_appFatInfo.threadId = SGetCurrentThreadId();
    InternalLeaveCriticalSection(&s_critsect);
}

void STORMAPI SErrSetLastError(uint32_t errorcode) {
    s_lasterror = errorcode;
}

void STORMAPI SErrSetLogTitleString(const char* title) {
    InternalEnterCriticalSection(&s_critsect);
    SStrCopy(s_logtitle, title, sizeof(s_logtitle));
    InternalLeaveCriticalSection(&s_critsect);
}

void STORMAPI SErrSetLogCallback(int32_t (STORMAPI* callback)(char*, uint32_t)) {
    InternalEnterCriticalSection(&s_critsect);
    s_logCallback = callback;
    InternalLeaveCriticalSection(&s_critsect);
}

int32_t STORMAPI SErrGetLogLastPath(char* buf, uint32_t size) {
    InternalEnterCriticalSection(&s_critsect);
    int32_t result = 0;

    if (s_logLastFile[0]) {
        SStrCopy(buf, s_logLastFile, size);
        result = 1;
    }

    InternalLeaveCriticalSection(&s_critsect);

    return result;
}

int32_t STORMAPI SErrGetErrorStr(uint32_t errorcode, char* buffer, uint32_t bufferchars) {
    if (!buffer || !bufferchars) {
        SErrSetLastError(87);
        return 0;
    }

    *buffer = 0;

    if (errorcode == 108) {
        SStrPrintf(buffer, bufferchars, "%s", "Invalid or corrupt archive.\n");
        return 1;
    }

    // Windows formats these from message tables; without them the caller falls back to "ERROR #n (0x...) <text>".
    return 0;
}

void STORMAPI SErrRegisterHandler(int32_t (STORMAPI* handler)(uint32_t, const char*, const char*, int32_t, const char*)) {
    InternalEnterCriticalSection(&s_critsect);

    if (s_handlerCount < kMaxHandlers) {
        // The Windows list links new handlers at the head, so the newest runs first.
        memmove(&s_handlers[1], &s_handlers[0], s_handlerCount * sizeof(s_handlers[0]));
        s_handlers[0] = handler;
        s_handlerCount++;
    }

    InternalLeaveCriticalSection(&s_critsect);
}

int32_t STORMAPI SErrDestroy() {
    s_handlerCount = 0;

    if (s_critsectinit != -1) {
        DeleteCriticalSection(&s_critsect);
        DeleteCriticalSection(&s_exceptioncritsect);
        s_critsectinit = -1;
    }

    return 1;
}

int32_t STORMAPI SErrDisplayError(uint32_t errorcode, const char* filename, int32_t linenumber, const char* description, int32_t recoverable, uint32_t exitcode, uint32_t unknown) {
    static int32_t lastlinenumber;
    static const char* lastfilename;

    if (g_opt.unk_0028) {
        if (filename == lastfilename && linenumber == lastlinenumber) {
            return 1;
        }

        lastfilename = filename;
        lastlinenumber = linenumber;
    }

    if (s_displaying) {
        return 0;
    }

    s_displaying = 1;

    static char appfilename[MAX_PATH];
    static char appname[MAX_PATH];
    uint32_t pathsize = sizeof(appfilename);
    appfilename[0] = 0;

    if (_NSGetExecutablePath(appfilename, &pathsize) != 0) {
        appfilename[0] = 0;
    }

    const char* base = strrchr(appfilename, '/');
    SStrCopy(appname, base ? base + 1 : appfilename, sizeof(appname));

    if (SStrChrR(appname, '.')) {
        *SStrChrR(appname, '.') = 0;
    }

    static char errorstr[256];
    errorstr[0] = 0;
    SErrGetErrorStr(errorcode, errorstr, sizeof(errorstr));

    if (!errorstr[0]) {
        SStrPrintf(errorstr, sizeof(errorstr), GetErrorString(SERR_STR_ERROR), errorcode, errorcode, SErrGetErrorText(errorcode));
    }

    const char* localnameptr = filename;
    static char localnamebuffer[256];

    if (localnameptr && *localnameptr && (linenumber == SLOG_OBJECT || linenumber == SLOG_HANDLE) && UndecorateObjectName(localnameptr, localnamebuffer, sizeof(localnamebuffer))) {
        localnameptr = localnamebuffer;
    }

    static char outstr[2048];
    char* outptr = outstr;
    char* outend = outstr + sizeof(outstr);

    if (localnameptr) {
        outptr += SStrCopy(outptr, localnameptr, outend - outptr);
    }

    if (linenumber > 0) {
        SStrPrintf(outptr, outend - outptr, "(%u)", linenumber);
        outptr += SStrLen(outptr);
    }

    SStrPrintf(outptr, outend - outptr, " : error %u: ", errorcode);
    outptr += SStrLen(outptr);
    SStrCopy(outptr, errorstr, outend - outptr);
    SDebugPrintf("%s\n", outstr);

    outptr = outstr;
    SStrPrintf(outptr, outend - outptr, GetErrorString(SERR_STR_CRITICAL), errorstr);
    outptr += SStrLen(outptr);
    SStrPrintf(outptr, outend - outptr, GetErrorString(SERR_STR_PROGRAM), appfilename);
    outptr += SStrLen(outptr);

    if (localnameptr && *localnameptr) {
        switch (linenumber) {
            case SLOG_EXCEPTION:
                SStrPrintf(outptr, outend - outptr, GetErrorString(SERR_STR_EXCEPTION), localnameptr);
                break;
            case SLOG_FILE:
                SStrPrintf(outptr, outend - outptr, GetErrorString(SERR_STR_FILE), localnameptr);
                break;
            case SLOG_HANDLE:
                SStrPrintf(outptr, outend - outptr, GetErrorString(SERR_STR_HANDLE), localnameptr);
                break;
            case SLOG_OBJECT:
                SStrPrintf(outptr, outend - outptr, GetErrorString(SERR_STR_OBJECT), localnameptr);
                break;
            case SLOG_FUNCTION:
                SStrPrintf(outptr, outend - outptr, GetErrorString(SERR_STR_FUNCTION), localnameptr);
                break;
            default:
                SStrPrintf(outptr, outend - outptr, GetErrorString(SERR_STR_FILE_LINE), localnameptr, linenumber);
                break;
        }

        outptr += SStrLen(outptr);
    }

    SStrPrintf(outptr, outend - outptr, GetErrorString(errorcode == STORM_ERROR_ASSERTION ? SERR_STR_EXPRESSION : SERR_STR_DESCRIPTION), description ? description : "");
    outptr += SStrLen(outptr);

    if (!g_opt.serrsuppresslogs) {
        LoadMachineStateSymbols();
        InternalEnterCriticalSection(&s_exceptioncritsect);
        ucontext_t* context = s_crashContext;
        s_crashContext = nullptr;
        InternalLeaveCriticalSection(&s_exceptioncritsect);

        SYSTEMTIME time;
        GetLocalTime(&time);

        FILE* logfile = CreateErrorLog(context ? "Crash" : "Error", outstr, &time);

        if (logfile) {
            LogMachineState(LOG_OPTION_DEFAULT | LOG_OPTION_FRAMEPARAMS, WriteLine, logfile, context ? 0 : 2, reinterpret_cast<CONTEXT*>(context));
            fclose(logfile);
        }

        char* details = SStrStr(outstr, "Debug Details:");

        if (details) {
            outptr = details;
        }

        UnloadMachineStateSymbols();
    }

    if (g_opt.unk_0028) {
        s_displaying = 0;
        return 1;
    }

    const char* buttons[3];
    int32_t results[3];
    int32_t buttonCount;
    int32_t keeprunning;
    int32_t breakresult;

    if (!CanBreakToDebugger()) {
        if (recoverable) {
            outptr += SStrCopy(outptr, GetErrorString(SERR_STR_KEEP_RUNNING), outend - outptr);
            buttons[0] = "Cancel";
            results[0] = kIdCancel;
            buttons[1] = "OK";
            results[1] = kIdOk;
            buttonCount = 2;
            keeprunning = kIdOk;
            breakresult = 0;
        } else {
            outptr += SStrCopy(outptr, GetErrorString(SERR_STR_TERMINATE), outend - outptr);
            buttons[0] = "OK";
            results[0] = kIdOk;
            buttonCount = 1;
            keeprunning = 0;
            breakresult = 0;
        }
    } else {
        if (!recoverable) {
            outptr += SStrCopy(outptr, GetErrorString(SERR_STR_BREAK), outend - outptr);
            buttons[0] = "No";
            results[0] = kIdNo;
            buttons[1] = "Yes";
            results[1] = kIdYes;
            buttonCount = 2;
            keeprunning = 0;
            breakresult = kIdYes;
        } else {
            outptr += SStrCopy(outptr, GetErrorString(SERR_STR_BREAK_OR_RUN), outend - outptr);
            buttons[0] = "No";
            results[0] = kIdNo;
            buttons[1] = "Yes";
            results[1] = kIdYes;
            buttons[2] = "Cancel";
            results[2] = kIdCancel;
            buttonCount = 3;
            keeprunning = kIdCancel;
            breakresult = kIdYes;
        }
    }

    InternalEnterCriticalSection(&s_critsect);

    int32_t result = keeprunning ? keeprunning : results[0];
    int32_t handled = 0;

    for (int32_t i = 0; i < s_handlerCount; i++) {
        if (!s_handlers[i](errorcode, errorstr, localnameptr, linenumber, description ? description : "")) {
            handled = 1;
            break;
        }
    }

    if (!handled) {
        result = ShowAlert(appname, outstr, recoverable != 0, buttons, results, buttonCount);
    }

    InternalLeaveCriticalSection(&s_critsect);

    if (result == breakresult) {
        Breakpoint();
        s_displaying = 0;
        return 1;
    }

    if (result == keeprunning) {
        s_displaying = 0;
        return 1;
    }

    _exit(static_cast<int>(exitcode));
}

void STORMAPI SErrReportNamedResourceLeak(const char* handlename, const char* resourcename) {
    static int32_t checked;
    static uint32_t debugmode;

    if (!checked) {
        checked = 1;
        SRegLoadValue("Internal", "Debug Memory", 0, &debugmode);
    }

    if (!debugmode) {
        return;
    }

    char errormessage[256];

    if (resourcename && *resourcename) {
        SStrPrintf(errormessage, sizeof(errormessage), "%s (%s)", handlename, resourcename);
    } else {
        SStrPrintf(errormessage, sizeof(errormessage), "%s", handlename);
    }

    if (g_opt.serrleaksilentwarning) {
        fprintf(stderr, "Storm Error : handle never released -- %s\n", errormessage);
    } else {
        SErrDisplayError(STORM_ERROR_RESOURCES_NOT_RELEASED, errormessage, SLOG_HANDLE, nullptr, 1, 1, 0x11111111);
    }
}

void SErrDisplayAppFatalCustomV(uint32_t errorcode, const char* format, va_list args) {
    const char* filename = nullptr;
    int32_t linenumber = 0;

    InternalEnterCriticalSection(&s_critsect);

    if (s_appFatInfo.threadId == SGetCurrentThreadId()) {
        filename = s_appFatInfo.filename;
        linenumber = s_appFatInfo.linenumber;
        s_appFatInfo.filename = nullptr;
        s_appFatInfo.linenumber = 0;
        s_appFatInfo.threadId = 0;
    }

    InternalLeaveCriticalSection(&s_critsect);

    char buffer[2048];
    vsnprintf(buffer, sizeof(buffer) - 1, format, args);
    buffer[sizeof(buffer) - 1] = 0;
    SErrDisplayError(errorcode, filename, linenumber, buffer, 0, 1, 0x11111111);
    StormExitProcess(1);
    _exit(1);
}

void SErrDisplayAppFatalCustom(uint32_t errorcode, const char* format, ...) {
    va_list args;
    va_start(args, format);
    SErrDisplayAppFatalCustomV(errorcode, format, args);
}

void SErrDisplayAppFatal(const char* format, ...) {
    va_list args;
    va_start(args, format);
    SErrDisplayAppFatalCustomV(STORM_ERROR_FATAL_CONDITION, format, args);
}

int32_t SErrDisplayErrorFmt(uint32_t errorcode, const char* filename, int32_t linenumber, int32_t recoverable, uint32_t exitcode, const char* format, ...) {
    char buffer[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer) - 1, format, args);
    va_end(args);
    buffer[sizeof(buffer) - 1] = 0;

    return SErrDisplayError(errorcode, filename, linenumber, buffer, recoverable, exitcode, 0x11111111);
}

void STORMAPI SErrCatchUnhandledExceptions() {
    // The handler runs on its own stack so a stack overflow can still be reported.
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
}

void SErrDisplayAppFatalMessage(const char* message, const char* filename, int32_t linenumber) {
    SErrPrepareAppFatal(filename, linenumber);
    SErrDisplayAppFatal("%s", message);
    _exit(1);
}
