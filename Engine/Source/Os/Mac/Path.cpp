// macOS implementation of Os/Win64/Path.cpp. Paths and the command line are UTF-8 already. The Switch build shares
// it: there the executable is the NRO the Homebrew Menu passes as argv[0] (sdmc:/switch/wow/WoW.nro).

#include <climits>
#include <crt_externs.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#include <string>
#include "Engine/Source/BlizzardCore/Source/System/Process/Mac/GameDirectory.h"
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

#if defined(__SWITCH__)
    (void)length;
    const char* argv0 = *_NSGetArgc() > 0 && (*_NSGetArgv())[0] && (*_NSGetArgv())[0][0] ? (*_NSGetArgv())[0] : "sdmc:/switch/wow/WoW.nro";
    strncpy(executable, argv0, sizeof(executable) - 1);
    executable[sizeof(executable) - 1] = '\0';
    char* source = executable;
#else
    if (_NSGetExecutablePath(executable, &length) != 0) {
        path[0] = '\0';
        return 0;
    }

    char resolved[PATH_MAX];
    char* source = realpath(executable, resolved) ? resolved : executable;
#endif

    // In a bundle the application file is Wow.app, not Wow.app/Contents/MacOS/Wow: callers strip the filename to find
    // the game folder (OsGetExePath), which holds Data/ next to the bundle, as Blizzard::Process::GetProcessDirectory
    // does.
    if (char* bundle = strstr(source, ".app/Contents/MacOS/")) {
        bundle[4] = '\0';
    }

    // A game folder chosen at launch: the executable is reported inside it, so the folder callers derive is that one.
    std::string chosen;

    if (const char* game = MacGetGameDirectory()) {
        const char* name = strrchr(source, '/');
        chosen = std::string(game) + (name ? name + 1 : source);
        source = &chosen[0];
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
