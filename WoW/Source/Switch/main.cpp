// Nintendo Switch entry point (Win64/WinMain.cpp, Mac/main.mm).
//
// The game folder is the one holding WoW.nro (sdmc:/switch/wow/ by default), with the 3.3.5a Data folder next to it;
// WTF/, Logs/, Interface/ and the rest are written there as next to Wow.exe. Windows runs the client on an 8 MB fiber,
// so it runs here on a thread with an 8 MB stack: the loader's main thread has much less.

#include <pthread.h>
#include <cstdio>
#include <string>
#include <unistd.h>
#include "Engine/Source/BlizzardCore/Source/System/Process/Mac/GameDirectory.h"
#include "Engine/Source/Storm/H/storm.h"
#include "WoW/Source/Client.h"
#include "WoW/Source/Switch/NxMain.h"

namespace {

int s_result;

void* ClientThread(void* param) {
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
