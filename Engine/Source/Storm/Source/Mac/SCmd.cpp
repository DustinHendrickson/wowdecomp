// macOS implementation of the platform parts of Storm command-line parsing (Win64/SCmd.cpp).

#include <crt_externs.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "Engine/Source/Storm/Source/SCmd.h"
#include "Engine/Source/Storm/H/storm.h"

namespace {

// ERROR_OPEN_FAILED, the Win32 code Storm reports for an unreadable response file.
const uint32_t kErrorOpenFailed = 110;

const char* const s_errorstr[3] = {
    "Invalid argument: %s",
    "The syntax of the command is incorrect.",
    "Unable to open response file: %s",
};

// GetCommandLine, rebuilt from argv: arguments containing spaces or quotes are quoted the Windows way.
const char* CommandLine() {
    static std::string s_commandLine;

    if (s_commandLine.empty()) {
        int argc = *_NSGetArgc();
        char** argv = *_NSGetArgv();

        for (int i = 0; i < argc; i++) {
            const char* arg = argv[i];

            if (i) {
                s_commandLine += ' ';
            }

            if (*arg && !strpbrk(arg, " \t\"")) {
                s_commandLine += arg;
                continue;
            }

            s_commandLine += '"';

            for (const char* c = arg; *c; c++) {
                if (*c == '"') {
                    s_commandLine += '\\';
                }

                s_commandLine += *c;
            }

            s_commandLine += '"';
        }
    }

    return s_commandLine.c_str();
}

}

void GenerateError(void (STORMAPI* errorcallback)(_CMDERROR*), uint32_t errorcode, const char* itemstring) {
    uint32_t index;

    switch (errorcode) {
        case STORM_ERROR_BAD_ARGUMENT:
            index = 0;
            break;
        case STORM_ERROR_NOT_ENOUGH_ARGUMENTS:
            index = 1;
            break;
        case kErrorOpenFailed:
            index = 2;
            break;
        default:
            return;
    }

    char errorstr[256];

    if (strstr(s_errorstr[index], "%s")) {
        snprintf(errorstr, sizeof(errorstr), s_errorstr[index], itemstring);
    } else {
        SStrCopy(errorstr, s_errorstr[index], sizeof(errorstr));
    }

    if (errorstr[0]) {
        SStrPack(errorstr, "\n", sizeof(errorstr));
    }

    SErrSetLastError(errorcode);

    _CMDERROR data;
    data.errorcode = errorcode;
    data.itemstr = itemstring;
    data.errorstr = errorstr;
    errorcallback(&data);
}

int32_t ProcessFile(const char* filename, _PROCESSING* processing, CMDDEF** nextarg, int32_t (STORMAPI* extracallback)(const char*), void (STORMAPI* errorcallback)(_CMDERROR*)) {
    FILE* file = fopen(filename, "rb");

    if (!file) {
        if (errorcallback) {
            GenerateError(errorcallback, kErrorOpenFailed, filename);
        }

        return 0;
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* data = static_cast<char*>(SMemAlloc(size > 0 ? size + 1 : 1, __FILE__, __LINE__, 0));
    size_t bytesread = size > 0 ? fread(data, 1, static_cast<size_t>(size), file) : 0;
    fclose(file);
    data[bytesread] = 0;

    const char* curr = data;
    int32_t result = ProcessString(&curr, processing, nextarg, extracallback, errorcallback);
    SMemFree(data, __FILE__, __LINE__, 0);

    return result;
}

int32_t STORMAPI SCmdProcessCommandLine(int32_t (STORMAPI* extracallback)(const char*), void (STORMAPI* errorcallback)(_CMDERROR*)) {
    return SCmdProcess(CommandLine(), 1, extracallback, errorcallback);
}
