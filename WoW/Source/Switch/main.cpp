// Nintendo Switch entry point (Win64/WinMain.cpp, Mac/main.mm).
//
// The game folder is the one holding WoW.nro (sdmc:/switch/wow/ by default), with the 3.3.5a Data folder next to it;
// WTF/, Logs/, Interface/ and the rest are written there as next to Wow.exe. Windows runs the client on an 8 MB fiber,
// so it runs here on a thread with an 8 MB stack: the loader's main thread has much less.

#include <pthread.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>
#include "Engine/Source/BlizzardCore/Source/System/Process/Mac/GameDirectory.h"
#include "Engine/Source/Storm/H/storm.h"
#include "WoW/Source/Client.h"
#include "WoW/Source/Switch/NxMain.h"

namespace {

int s_result;

// The client's default resolutions are 4:3 (from its hardware table), and with widescreen on gxResolution only takes the
// 16:9 sizes the Switch offers (Gx/CGxDevice/Switch), so a default size is refused and gxResolution is left empty, which
// the glue screens can't parse. Before the client reads WTF/Config.wtf, a gxResolution that isn't a Switch size becomes
// 1280x720, and a new config skips the hardware detection that would pick a 4:3 size again.
void PrepareConfig() {
    std::vector<std::string> lines;
    bool haveResolution = false;
    bool haveDetect = false;

    if (FILE* file = fopen("WTF/Config.wtf", "r")) {
        char line[1024];

        while (fgets(line, sizeof(line), file)) {
            std::string text = line;

            while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
                text.pop_back();
            }

            if (!strncasecmp(text.c_str(), "SET gxResolution ", 17)) {
                if (text.find("\"1280x720\"") == std::string::npos && text.find("\"1920x1080\"") == std::string::npos) {
                    continue;
                }

                haveResolution = true;
            } else if (!strncasecmp(text.c_str(), "SET hwDetect ", 13)) {
                haveDetect = true;
            }

            lines.push_back(text);
        }

        fclose(file);
    }

    if (haveResolution && haveDetect) {
        return;
    }

    if (!haveResolution) {
        lines.push_back("SET gxResolution \"1280x720\"");
    }

    if (!haveDetect) {
        lines.push_back("SET hwDetect \"0\"");
    }

    mkdir("WTF", 0777);

    if (FILE* file = fopen("WTF/Config.wtf", "w")) {
        for (const std::string& line : lines) {
            fprintf(file, "%s\n", line.c_str());
        }

        fclose(file);
    }
}

void* ClientThread(void* param) {
    wowdecomp_switch_thread_start(WOWDECOMP_THREAD_GAME);
    StormRtlInitialize();
    s_result = CommonMain();
    return nullptr;
}

}

int main() {
    if (!NxMainInitialize()) {
        return 1;
    }

    char folder[1024];
    NxMainGameFolder(folder, sizeof(folder));

    if (!NxMainHasGameData(folder)) {
        std::string message = std::string("No game data found.\n\nCopy the Data folder of a World of Warcraft 3.3.5a "
                                          "(12340) client to ") + folder + "Data, next to WoW.nro.";
        NxMainShowError(message.c_str());
        NxMainShutdown();
        return 1;
    }

    MacSetGameDirectory(folder);
    chdir(folder);
    NxMainRedirectConsole();
    PrepareConfig();
    NxMainInstallAddOns();

    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setstacksize(&attributes, 8 * 1024 * 1024);

    pthread_t thread;

    if (pthread_create(&thread, &attributes, ClientThread, nullptr) == 0) {
        pthread_join(thread, nullptr);
    } else {
        NxMainShowError("The game thread couldn't be started.");
        s_result = 1;
    }

    pthread_attr_destroy(&attributes);
    NxMainShutdown();

    return s_result;
}
