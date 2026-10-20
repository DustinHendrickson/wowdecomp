// libnx side of the Switch entry point (NxMain.h).

#include <switch.h>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <strings.h>
#include <sys/iosupport.h>
#include <sys/stat.h>
#include <unistd.h>
#include "WoW/Source/Switch/NxMain.h"

extern "C" int __system_argc;
extern "C" char** __system_argv;

namespace {

int s_nxlink = -1;

}

bool NxMainInitialize() {
    AppletType type = appletGetAppletType();

    if (type != AppletType_Application && type != AppletType_SystemApplication) {
        NxMainShowError("World of Warcraft needs the memory the Switch gives a game, which homebrew started from the "
                        "album doesn't get.\n\nStart it over a game instead: hold R while starting any game from the "
                        "HOME menu, then choose World of Warcraft in the Homebrew Menu.");
        return false;
    }

    // The game's connections carry the whole world: the default socket buffers are far too small for it.
    SocketInitConfig config = *socketGetDefaultInitConfig();
    config.tcp_tx_buf_size = 0x20000;
    config.tcp_rx_buf_size = 0x40000;
    config.tcp_tx_buf_max_size = 0x80000;
    config.tcp_rx_buf_max_size = 0x100000;
    config.sb_efficiency = 8;

    if (R_FAILED(socketInitialize(&config))) {
        NxMainShowError("The network couldn't be started.");
        return false;
    }

    // Started with nxlink -s: stdout and stderr go to the computer that sent the NRO.
    s_nxlink = nxlinkStdio();

    return true;
}

namespace {

FILE* s_consoleFile;
Mutex s_consoleLock;

ssize_t ConsoleWrite(struct _reent* r, void* fd, const char* ptr, size_t len) {
    mutexLock(&s_consoleLock);
    fwrite(ptr, 1, len, s_consoleFile);
    fflush(s_consoleFile);
    mutexUnlock(&s_consoleLock);
    return static_cast<ssize_t>(len);
}

devoptab_t s_consoleDevice;

}

void NxMainRedirectConsole() {
    if (s_nxlink >= 0) {
        return;
    }

    mkdir("Logs", 0777);
    s_consoleFile = fopen("Logs/Console.txt", "w");

    if (!s_consoleFile) {
        return;
    }

    // newlib keeps stdout and stderr per thread, so freopen would only move the main thread's. Replacing the devices
    // behind file descriptors 1 and 2, as nxlinkStdio does, moves every thread's.
    mutexInit(&s_consoleLock);
    s_consoleDevice.name = "con";
    s_consoleDevice.write_r = ConsoleWrite;
    devoptab_list[STD_OUT] = &s_consoleDevice;
    devoptab_list[STD_ERR] = &s_consoleDevice;
    setvbuf(stdout, nullptr, _IOLBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
}

void NxMainShutdown() {
    if (s_nxlink >= 0) {
        close(s_nxlink);
        s_nxlink = -1;
    }

    socketExit();
}

void NxMainGameFolder(char* buffer, size_t size) {
    const char* path = __system_argc > 0 && __system_argv[0] ? __system_argv[0] : "";
    const char* slash = strrchr(path, '/');

    if (!slash) {
        snprintf(buffer, size, "/switch/wow/");
        return;
    }

    // '/'-rooted, without the "sdmc:" device: libnx resolves those on the SD card, and the game's path code only takes
    // paths starting with '/' as absolute.
    if (!strncmp(path, "sdmc:/", 6)) {
        path += 5;
    }

    size_t length = static_cast<size_t>(slash - path) + 1;

    if (length >= size) {
        length = size - 1;
    }

    memcpy(buffer, path, length);
    buffer[length] = '\0';
}

bool NxMainHasGameData(const char* folder) {
    char data[1024];
    snprintf(data, sizeof(data), "%sData", folder);

    DIR* directory = opendir(data);

    if (!directory) {
        return false;
    }

    bool found = false;

    while (dirent* entry = readdir(directory)) {
        size_t length = strlen(entry->d_name);

        if (length > 4 && !strcasecmp(entry->d_name + length - 4, ".mpq")) {
            found = true;
            break;
        }
    }

    closedir(directory);
    return found;
}

void NxMainShowError(const char* message) {
    ErrorApplicationConfig config;

    if (R_SUCCEEDED(errorApplicationCreate(&config, message, nullptr))) {
        errorApplicationShow(&config);
    }
}
