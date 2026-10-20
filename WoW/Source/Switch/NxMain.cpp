// libnx side of the Switch entry point (NxMain.h).

#include <switch.h>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <strings.h>
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

void NxMainRedirectConsole() {
    if (s_nxlink >= 0) {
        return;
    }

    mkdir("Logs", 0777);

    // Two streams on one file, both appending, so neither overwrites the other's lines.
    FILE* file = fopen("Logs/Console.txt", "w");

    if (file) {
        fclose(file);
    }

    if (freopen("Logs/Console.txt", "a", stdout)) {
        setvbuf(stdout, nullptr, _IOLBF, 0);
    }

    if (freopen("Logs/Console.txt", "a", stderr)) {
        setvbuf(stderr, nullptr, _IONBF, 0);
    }
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
        snprintf(buffer, size, "sdmc:/switch/wow/");
        return;
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
