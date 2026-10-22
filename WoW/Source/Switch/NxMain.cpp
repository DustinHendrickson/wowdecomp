// libnx side of the Switch entry point (NxMain.h).

#include <switch.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <strings.h>
#include <sys/iosupport.h>
#include <sys/stat.h>
#include <unistd.h>
#include "Engine/Source/Os/Switch/NxInput.h"
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

// Console output is queued and written to the SD card by a thread of its own: a write to the card can block for tens of
// milliseconds, a visible hitch on the game thread (the performance report every 5 seconds showed as a stutter). After
// a crash (wowdecomp_switch_console_sync) writes go straight to the file, so nothing is lost when the process ends.
const size_t kConsoleQueueLimit = 1 << 20;

FILE* s_consoleFile;
Mutex s_consoleLock;            // the queue
RMutex s_consoleFileLock;       // the file, held while writing it
CondVar s_consoleReady;
char* s_consoleQueue;
size_t s_consoleQueued;
bool s_consoleSync;
Thread s_consoleThread;

void ConsoleWriteFile(const char* data, size_t length) {
    rmutexLock(&s_consoleFileLock);

    // Closed by NxMainExit just before the process ends.
    if (s_consoleFile) {
        fwrite(data, 1, length, s_consoleFile);
        fflush(s_consoleFile);
    }

    rmutexUnlock(&s_consoleFileLock);
}

// Writes what is queued. The file lock is taken before the queue is emptied, so output reaches the file in order.
void ConsoleDrain(char* buffer) {
    rmutexLock(&s_consoleFileLock);
    mutexLock(&s_consoleLock);
    size_t length = s_consoleQueued;
    memcpy(buffer, s_consoleQueue, length);
    s_consoleQueued = 0;
    mutexUnlock(&s_consoleLock);

    if (length) {
        ConsoleWriteFile(buffer, length);
    }

    rmutexUnlock(&s_consoleFileLock);
}

void ConsoleThread(void* argument) {
    char* buffer = static_cast<char*>(malloc(kConsoleQueueLimit));

    while (true) {
        mutexLock(&s_consoleLock);

        while (!s_consoleQueued) {
            condvarWait(&s_consoleReady, &s_consoleLock);
        }

        mutexUnlock(&s_consoleLock);
        ConsoleDrain(buffer);
    }
}

ssize_t ConsoleWrite(struct _reent* r, void* fd, const char* ptr, size_t len) {
    mutexLock(&s_consoleLock);

    if (s_consoleSync || !s_consoleQueue) {
        mutexUnlock(&s_consoleLock);
        ConsoleWriteFile(ptr, len);
        return static_cast<ssize_t>(len);
    }

    // Past the limit (the card stalled for a long time) output is dropped rather than held up.
    size_t room = kConsoleQueueLimit - s_consoleQueued;
    size_t length = len < room ? len : room;
    memcpy(s_consoleQueue + s_consoleQueued, ptr, length);
    s_consoleQueued += length;
    condvarWakeOne(&s_consoleReady);
    mutexUnlock(&s_consoleLock);

    return static_cast<ssize_t>(len);
}

devoptab_t s_consoleDevice;

}

extern "C" void wowdecomp_switch_console_sync(void) {
    if (!s_consoleFile) {
        return;
    }

    rmutexLock(&s_consoleFileLock);
    mutexLock(&s_consoleLock);
    s_consoleSync = true;
    size_t length = s_consoleQueued;
    s_consoleQueued = 0;

    if (length) {
        ConsoleWriteFile(s_consoleQueue, length);
    }

    mutexUnlock(&s_consoleLock);
    rmutexUnlock(&s_consoleFileLock);
}

void NxMainRedirectConsole() {
    if (s_nxlink >= 0) {
        return;
    }

    mkdir("Logs", 0777);

    // The previous run's output is kept, so a run that ends badly doesn't lose the one before it.
    remove("Logs/Console-previous.txt");
    rename("Logs/Console.txt", "Logs/Console-previous.txt");
    s_consoleFile = fopen("Logs/Console.txt", "w");

    if (!s_consoleFile) {
        return;
    }

    // newlib keeps stdout and stderr per thread, so freopen would only move the main thread's. Replacing the devices
    // behind file descriptors 1 and 2, as nxlinkStdio does, moves every thread's.
    mutexInit(&s_consoleLock);
    rmutexInit(&s_consoleFileLock);
    condvarInit(&s_consoleReady);

    // Low priority on the third core, away from the game thread; it spends its time waiting on the card.
    s_consoleQueue = static_cast<char*>(malloc(kConsoleQueueLimit));

    if (!s_consoleQueue || R_FAILED(threadCreate(&s_consoleThread, ConsoleThread, nullptr, nullptr, 0x8000, 0x3B, 2)) ||
        R_FAILED(threadStart(&s_consoleThread))) {
        free(s_consoleQueue);
        s_consoleQueue = nullptr;
    }

    s_consoleDevice.name = "con";
    s_consoleDevice.write_r = ConsoleWrite;
    devoptab_list[STD_OUT] = &s_consoleDevice;
    devoptab_list[STD_ERR] = &s_consoleDevice;
    setvbuf(stdout, nullptr, _IOLBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

    fprintf(stderr, "World of Warcraft for Switch, build %s (%s %s)\n", WOWDECOMP_BUILD_ID, __DATE__, __TIME__);

    // How the game was started and the memory it got: about 3.2 GB over a game, under 0.5 GB as an applet.
    u64 total = 0;
    u64 used = 0;
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    fprintf(stderr, "Applet type %d, memory %llu MB, used %llu MB at start\n", static_cast<int>(appletGetAppletType()), static_cast<unsigned long long>(total >> 20), static_cast<unsigned long long>(used >> 20));
}

void NxMainShutdown() {
    wowdecomp_switch_console_sync();

    // The exit lock the input layer takes, so closing from the HOME menu quits the game properly; it's done now.
    appletUnlockExit();

    if (s_nxlink >= 0) {
        close(s_nxlink);
        s_nxlink = -1;
    }

    socketExit();
}

void NxMainExit(int result) {
    NxClocksRestore();
    fprintf(stderr, "Quit: exiting (%d)\n", result);
    wowdecomp_switch_console_sync();

    if (s_consoleFile) {
        rmutexLock(&s_consoleFileLock);
        fclose(s_consoleFile);
        s_consoleFile = nullptr;
        rmutexUnlock(&s_consoleFileLock);
    }

    svcExitProcess();
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

namespace {

bool CopyFile(const char* from, const char* to) {
    FILE* source = fopen(from, "rb");

    if (!source) {
        return false;
    }

    FILE* destination = fopen(to, "wb");

    if (!destination) {
        fclose(source);
        return false;
    }

    char buffer[0x4000];
    size_t count;

    while ((count = fread(buffer, 1, sizeof(buffer), source)) > 0) {
        fwrite(buffer, 1, count, destination);
    }

    fclose(source);
    fclose(destination);
    return true;
}

void CopyTree(const char* from, const char* to) {
    DIR* directory = opendir(from);

    if (!directory) {
        return;
    }

    mkdir(to, 0777);

    while (dirent* entry = readdir(directory)) {
        if (entry->d_name[0] == '.') {
            continue;
        }

        char source[1024];
        char destination[1024];
        snprintf(source, sizeof(source), "%s/%s", from, entry->d_name);
        snprintf(destination, sizeof(destination), "%s/%s", to, entry->d_name);

        if (entry->d_type == DT_DIR) {
            CopyTree(source, destination);
        } else if (!CopyFile(source, destination)) {
            fprintf(stderr, "Couldn't install %s\n", destination);
        }
    }

    closedir(directory);
}

}

void NxMainInstallAddOns() {
    if (R_FAILED(romfsInit())) {
        fprintf(stderr, "No RomFS: built-in add-ons not installed\n");
        return;
    }

    mkdir("Interface", 0777);
    CopyTree("romfs:/AddOns", "Interface/AddOns");
    romfsExit();
}

void NxMainShowError(const char* message) {
    ErrorApplicationConfig config;

    if (R_SUCCEEDED(errorApplicationCreate(&config, message, nullptr))) {
        errorApplicationShow(&config);
    }
}
