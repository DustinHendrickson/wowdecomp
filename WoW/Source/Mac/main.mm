// macOS entry point (Win64/WinMain.cpp).
//
// Windows starts in WinMain, which runs the client on an 8 MB fiber; the macOS main thread already has an 8 MB stack.
// Relative paths (WTF/, Logs/, Interface/) are relative to the working directory, which Windows shortcuts set to the
// game folder; an .app starts in /, so the working directory moves to the game folder first: the one holding the
// executable, or the .app bundle when run from one.

#import <Foundation/Foundation.h>
#include <csignal>
#include <unistd.h>
#include "Engine/Source/BlizzardCore/Source/System/Process/Process.h"
#include "Engine/Source/Storm/H/storm.h"
#include "WoW/Source/Client.h"

int main() {
    // Writes to a closed connection must fail with EPIPE rather than end the process.
    signal(SIGPIPE, SIG_IGN);

    char directory[1024];

    if (Blizzard::Process::GetProcessDirectory(directory, sizeof(directory)) && directory[0]) {
        chdir(directory);
    }

    StormRtlInitialize();

    return CommonMain();
}
