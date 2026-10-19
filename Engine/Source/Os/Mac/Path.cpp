// macOS implementation of Os/Win64/Path.cpp. Paths and the command line are UTF-8 already.

#include <climits>
#include <crt_externs.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mach-o/dyld.h>
#include <string>
#include "Engine/Source/Os/Path.h"
#include "Engine/Source/Storm/H/storm.h"

static CommandLineUTF8 s_commandLineUTF8;

uint32_t GetModuleFileNameUTF8(void* module, char* path, uint32_t size) {
    // Only the main executable (module nullptr) exists on macOS.
    char executable[PATH_MAX];
    uint32_t length = sizeof(executable);

    if (!size) {
        return 0;
    }

    if (_NSGetExecutablePath(executable, &length) != 0) {
        path[0] = '\0';
        return 0;
    }

    char resolved[PATH_MAX];
    char* source = realpath(executable, resolved) ? resolved : executable;

    // In a bundle the application file is Wow.app, not Wow.app/Contents/MacOS/Wow: callers strip the filename to find
    // the game folder (OsGetExePath), which holds Data/ next to the bundle, as Blizzard::Process::GetProcessDirectory
    // does.
    if (char* bundle = strstr(source, ".app/Contents/MacOS/")) {
        bundle[4] = '\0';
    }

    uint32_t written = static_cast<uint32_t>(strlen(source));

    if (written >= size) {
        written = size - 1;
    }

    memcpy(path, source, written);
    path[written] = '\0';

    return written;
}

CommandLineUTF8::CommandLineUTF8() {
    // GetCommandLine: the arguments joined with spaces, quoted where they contain spaces or quotes.
    std::string commandLine;
    int argc = *_NSGetArgc();
    char** argv = *_NSGetArgv();

    for (int i = 0; i < argc; i++) {
        const char* arg = argv[i];

        if (i) {
            commandLine += ' ';
        }

        if (*arg && !strpbrk(arg, " \t\"")) {
            commandLine += arg;
            continue;
        }

        commandLine += '"';

        for (const char* c = arg; *c; c++) {
            if (*c == '"') {
                commandLine += '\\';
            }

            commandLine += *c;
        }

        commandLine += '"';
    }

    m_commandLine = static_cast<char*>(SMemAlloc(static_cast<uint32_t>(commandLine.size() + 1), __FILE__, __LINE__, 0));
    memcpy(m_commandLine, commandLine.c_str(), commandLine.size() + 1);
}

CommandLineUTF8::~CommandLineUTF8() {
    SMemFree(m_commandLine, __FILE__, __LINE__, 0);
}
