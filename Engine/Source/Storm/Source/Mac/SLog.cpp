// macOS implementation of Storm logs (Win64/SLog.cpp). Same buffering and locking; files are POSIX descriptors, paths
// use '/', and relative log names land next to the game (the Process directory, outside the .app bundle).

#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include "Engine/Source/BlizzardCore/Source/System/Process/Process.h"
#include "Engine/Source/Storm/Source/SLog.h"
#include "Engine/Source/Storm/Source/SOption.h"
#include "Engine/Source/Storm/H/storm.h"

namespace {

void* const kInvalidFile = reinterpret_cast<void*>(static_cast<intptr_t>(-1));

int32_t s_logsysteminit;
uintptr_t s_sequence;
char s_defaultdir[MAX_PATH];
CRITICAL_SECTION s_defaultdir_critsect;
_LOG* s_loghead[4];
CRITICAL_SECTION s_critsect[4];

int Descriptor(const _LOG* logptr) {
    return static_cast<int>(reinterpret_cast<intptr_t>(logptr->file));
}

void FlushLog(_LOG* logptr) {
    if (logptr->bufferused) {
        const char* data = logptr->buffer;
        size_t remaining = logptr->bufferused;

        while (remaining) {
            ssize_t written = write(Descriptor(logptr), data, remaining);

            if (written < 0) {
                if (errno == EINTR) {
                    continue;
                }

                break;
            }

            data += written;
            remaining -= static_cast<size_t>(written);
        }

        fsync(Descriptor(logptr));
        logptr->bufferused = 0;
        logptr->pendpoint = 0;
    }
}

_LOG* LockLog(HSLOG log, HLOCKEDLOG* lockedhandle, int32_t createifnecessary) {
    if (!log) {
        *lockedhandle = reinterpret_cast<HLOCKEDLOG>(-1);
        return nullptr;
    }

    uintptr_t index = reinterpret_cast<uintptr_t>(log) & 3;
    EnterCriticalSection(&s_critsect[index]);
    *lockedhandle = reinterpret_cast<HLOCKEDLOG>(index);

    _LOG** prevptr = &s_loghead[index];

    for (_LOG* logptr = s_loghead[index]; logptr; logptr = logptr->next) {
        if (logptr->log == log) {
            return logptr;
        }

        prevptr = &logptr->next;
    }

    if (!createifnecessary) {
        LeaveCriticalSection(&s_critsect[index]);
        *lockedhandle = reinterpret_cast<HLOCKEDLOG>(-1);
        return nullptr;
    }

    _LOG* logptr = static_cast<_LOG*>(calloc(1, sizeof(_LOG)));
    *prevptr = logptr;

    if (!logptr) {
        LeaveCriticalSection(&s_critsect[index]);
        *lockedhandle = reinterpret_cast<HLOCKEDLOG>(-1);
        return nullptr;
    }

    logptr->log = log;
    logptr->next = nullptr;
    logptr->filename[0] = 0;
    logptr->file = kInvalidFile;
    logptr->bufferused = 0;
    logptr->pendpoint = 0;

    return logptr;
}

void OutputIndent(_LOG* logptr) {
    int32_t indent = logptr->indent;

    if (indent > 0) {
        if (indent >= 128) {
            indent = 128;
        }

        memset(&logptr->buffer[logptr->bufferused], ' ', indent);
        logptr->buffer[logptr->bufferused + indent] = 0;
        logptr->bufferused += indent;
    }
}

void OutputReturn(_LOG* logptr) {
    strcpy(&logptr->buffer[logptr->bufferused], "\n");
    logptr->bufferused += 1;
}

void OutputTime(_LOG* logptr, int32_t show) {
    static uint32_t lasttime;
    static char timestr[64];
    static uint32_t timestrlen;

    if (!logptr->timeStamp) {
        return;
    }

    timeval now;
    gettimeofday(&now, nullptr);
    uint32_t time = static_cast<uint32_t>(now.tv_sec * 1000 + now.tv_usec / 1000);

    if (time != lasttime) {
        lasttime = time;

        tm parts;
        localtime_r(&now.tv_sec, &parts);
        snprintf(timestr, sizeof(timestr), "%u/%u %02u:%02u:%02u.%03u  ", parts.tm_mon + 1, parts.tm_mday, parts.tm_hour, parts.tm_min, parts.tm_sec, static_cast<uint32_t>(now.tv_usec / 1000));
        timestrlen = SStrLen(timestr);
    }

    if (show) {
        memcpy(&logptr->buffer[logptr->bufferused], timestr, timestrlen + 1);
    } else {
        memset(&logptr->buffer[logptr->bufferused], ' ', timestrlen);
        logptr->buffer[logptr->bufferused + timestrlen] = 0;
    }

    logptr->bufferused += timestrlen;
}

void UnlockDeleteLog(_LOG* logptr, HLOCKEDLOG lockedhandle) {
    uintptr_t index = reinterpret_cast<uintptr_t>(lockedhandle);
    _LOG** prevptr = &s_loghead[index];

    for (_LOG* curr = s_loghead[index]; curr; curr = curr->next) {
        if (curr == logptr) {
            *prevptr = curr->next;
            free(curr);
            break;
        }

        prevptr = &curr->next;
    }

    LeaveCriticalSection(&s_critsect[index]);
}

void UnlockLog(HLOCKEDLOG lockedhandle) {
    LeaveCriticalSection(&s_critsect[reinterpret_cast<uintptr_t>(lockedhandle)]);
}

void ConvertSlashes(char* path) {
    for (char* curr = SStrChr(path, '\\'); curr; curr = SStrChr(curr + 1, '\\')) {
        *curr = '/';
    }
}

const char* PrependDefaultDir(char* newfilename, uint32_t newfilenamesize, const char* filename) {
    // Absolute names and names with a directory are used as given (with '/' separators).
    if (!filename || !*filename) {
        return filename;
    }

    if (filename[0] == '/' || SStrChr(filename, '\\') || SStrChr(filename, '/')) {
        SStrCopy(newfilename, filename, newfilenamesize);
        ConvertSlashes(newfilename);
        return newfilename;
    }

    EnterCriticalSection(&s_defaultdir_critsect);

    if (s_defaultdir[0]) {
        size_t chars = SStrCopy(newfilename, s_defaultdir, newfilenamesize);
        SStrCopy(&newfilename[chars], filename, newfilenamesize - chars);
    } else {
        Blizzard::Process::GetProcessDirectory(newfilename, newfilenamesize);
        SStrPack(newfilename, filename, newfilenamesize);
    }

    LeaveCriticalSection(&s_defaultdir_critsect);

    ConvertSlashes(newfilename);

    return newfilename;
}

int32_t CreateFileDirectory(const char* path) {
    if (!path) {
        SErrSetLastError(87);
        return 0;
    }

    char buffer[MAX_PATH];
    SStrCopy(buffer, path, sizeof(buffer));
    char* slash = SStrChrR(buffer, '/');

    if (!slash) {
        return 1;
    }

    *slash = 0;

    for (char* curr = SStrChr(buffer + 1, '/'); curr; curr = SStrChr(curr + 1, '/')) {
        *curr = 0;
        mkdir(buffer, 0755);
        *curr = '/';
    }

    return mkdir(buffer, 0755) == 0 || errno == EEXIST;
}

int32_t OpenLogFile(const char* filename, void** file, uint32_t flags) {
    if (!filename || !*filename) {
        *file = kInvalidFile;
        return 0;
    }

    char newfilename[MAX_PATH];
    const char* path = PrependDefaultDir(newfilename, sizeof(newfilename), filename);
    int32_t append = (flags & SLOG_FLAG_APPEND) != 0;
    CreateFileDirectory(path);

    int fd = open(path, O_WRONLY | O_CREAT | O_CLOEXEC | (append ? O_APPEND : O_TRUNC), 0644);

    // Keep descriptors off 0-2, matching the file layer.
    if (fd >= 0 && fd < 3) {
        int moved = fcntl(fd, F_DUPFD_CLOEXEC, 3);
        close(fd);
        fd = moved;
    }

    *file = fd >= 0 ? reinterpret_cast<void*>(static_cast<intptr_t>(fd)) : kInvalidFile;

    return fd >= 0;
}

int32_t PrepareLog(_LOG* logptr) {
    if (logptr->file != kInvalidFile) {
        return 1;
    }

    if (OpenLogFile(logptr->filename, &logptr->file, logptr->flags)) {
        return 1;
    }

    logptr->filename[0] = 0;
    return 0;
}

}

void STORMAPI SLogClose(HSLOG log) {
    if (!s_logsysteminit) {
        return;
    }

    HLOCKEDLOG lockedhandle;
    _LOG* logptr = LockLog(log, &lockedhandle, 0);

    if (!logptr) {
        return;
    }

    if (logptr->file != kInvalidFile) {
        FlushLog(logptr);
        close(Descriptor(logptr));
    }

    UnlockDeleteLog(logptr, lockedhandle);
}

void STORMAPI SLogFlush(HSLOG log) {
    HLOCKEDLOG lockedhandle;
    _LOG* logptr = LockLog(log, &lockedhandle, 0);

    if (logptr) {
        if (logptr->file != kInvalidFile) {
            FlushLog(logptr);
        }

        UnlockLog(lockedhandle);
    }
}

void STORMAPI SLogFlushAll() {
    for (int32_t i = 0; i < 4; i++) {
        EnterCriticalSection(&s_critsect[i]);

        for (_LOG* logptr = s_loghead[i]; logptr; logptr = logptr->next) {
            if (logptr->file != kInvalidFile) {
                FlushLog(logptr);
            }
        }

        LeaveCriticalSection(&s_critsect[i]);
    }
}

void STORMAPI SLogInitialize() {
    if (s_logsysteminit) {
        return;
    }

    for (int32_t i = 0; i < 4; i++) {
        InitializeCriticalSection(&s_critsect[i]);
    }

    InitializeCriticalSection(&s_defaultdir_critsect);
    s_logsysteminit = 1;
}

int32_t STORMAPI SLogCreate(const char* filename, uint32_t flags, HSLOG* log) {
    if (!filename || !*filename || !log) {
        SErrSetLastError(87);
        return 0;
    }

    *log = nullptr;

    if (flags & SLOG_FLAG_NOFILE) {
        filename = "";
        flags &= ~SLOG_FLAG_OPENFILE;
    }

    void* file = kInvalidFile;

    if ((flags & SLOG_FLAG_OPENFILE) && !OpenLogFile(filename, &file, flags)) {
        return 0;
    }

    *log = reinterpret_cast<HSLOG>(++s_sequence);

    HLOCKEDLOG lockedhandle;
    _LOG* logptr = LockLog(*log, &lockedhandle, 1);

    if (!logptr) {
        *log = nullptr;
        return 0;
    }

    logptr->file = file;
    SStrCopy(logptr->filename, filename, sizeof(logptr->filename));
    logptr->flags = flags;
    logptr->timeStamp = 1;
    logptr->indent = 0;
    UnlockLog(lockedhandle);

    return 1;
}

void STORMAPI SLogDestroy() {
    SLogFlushAll();

    for (int32_t i = 0; i < 4; i++) {
        EnterCriticalSection(&s_critsect[i]);

        while (s_loghead[i]) {
            char filename[MAX_PATH];
            SStrCopy(filename, s_loghead[i]->filename, sizeof(filename));
            SLogClose(s_loghead[i]->log);
            SErrReportNamedResourceLeak("HSLOG", filename);
        }

        LeaveCriticalSection(&s_critsect[i]);
    }

    s_logsysteminit = 0;

    for (int32_t i = 0; i < 4; i++) {
        DeleteCriticalSection(&s_critsect[i]);
    }

    DeleteCriticalSection(&s_defaultdir_critsect);
}

void STORMAPI SLogVWrite(HSLOG log, const char* format, va_list arglist) {
    HLOCKEDLOG lockedhandle;
    _LOG* logptr = LockLog(log, &lockedhandle, 0);

    if (!logptr) {
        return;
    }

    if (PrepareLog(logptr)) {
        OutputTime(logptr, 1);
        OutputIndent(logptr);
        SStrVPrintf(&logptr->buffer[logptr->bufferused], sizeof(logptr->buffer) - logptr->bufferused, format, arglist);
        logptr->bufferused += SStrLen(&logptr->buffer[logptr->bufferused]);
        OutputReturn(logptr);

        if (g_opt.echotooutputdebugstring) {
            fputs(&logptr->buffer[logptr->pendpoint], stderr);
        }

        logptr->pendpoint = logptr->bufferused;

        if (logptr->bufferused >= 0xC000) {
            FlushLog(logptr);
        }
    }

    UnlockLog(lockedhandle);
}

void SLogWrite(HSLOG log, const char* format, ...) {
    va_list arglist;
    va_start(arglist, format);
    SLogVWrite(log, format, arglist);
    va_end(arglist);
}
