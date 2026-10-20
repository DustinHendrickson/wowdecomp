// macOS entry point (Win64/WinMain.cpp).
//
// Windows starts in WinMain, which runs the client on an 8 MB fiber; the macOS main thread already has an 8 MB stack.
// Relative paths (WTF/, Logs/, Interface/) are relative to the working directory, which Windows shortcuts set to the
// game folder; an .app starts in /, so the working directory moves to the game folder first.
//
// The game folder is the one holding the executable (or the .app) when it has a Data folder, as Wow.exe's is on
// Windows. Otherwise it is a 3.3.5a folder chosen once and remembered (the GameFolder user default), so the app can live
// anywhere; holding Option at launch asks again.

#import <AppKit/AppKit.h>
#include <csignal>
#include <string>
#include <unistd.h>
#include "Engine/Source/BlizzardCore/Source/System/Process/Mac/GameDirectory.h"
#include "Engine/Source/BlizzardCore/Source/System/Process/Process.h"
#include "Engine/Source/Os/Mac/OsMacWindow.h"
#include "Engine/Source/Storm/H/storm.h"
#include "WoW/Source/Client.h"

namespace {

NSString* const kGameFolderKey = @"GameFolder";

// A folder the client can run from: Data/ with at least one MPQ in it.
bool HasGameData(NSString* folder) {
    NSArray<NSString*>* files = [NSFileManager.defaultManager contentsOfDirectoryAtPath:[folder stringByAppendingPathComponent:@"Data"] error:nil];

    for (NSString* file in files) {
        if ([file.pathExtension caseInsensitiveCompare:@"mpq"] == NSOrderedSame) {
            return true;
        }
    }

    return false;
}

// Asks for a 3.3.5a folder until the player picks one with game data or cancels.
NSString* AskForGameFolder(NSString* start) {
    OsMacInitializeApplication();

    while (true) {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.canChooseDirectories = YES;
        panel.canChooseFiles = NO;
        panel.allowsMultipleSelection = NO;
        panel.prompt = @"Play";
        panel.message = @"Choose your World of Warcraft 3.3.5a folder (the one with the Data folder). Settings, logs and "
                        @"AddOns are read from and written to it, as next to Wow.exe on Windows.";

        if (start) {
            panel.directoryURL = [NSURL fileURLWithPath:start isDirectory:YES];
        }

        [NSApp activateIgnoringOtherApps:YES];

        if ([panel runModal] != NSModalResponseOK || !panel.URL) {
            return nil;
        }

        NSString* folder = panel.URL.path;

        if (HasGameData(folder)) {
            return folder;
        }

        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = @"No game data in that folder";
        alert.informativeText = [NSString stringWithFormat:@"%@ has no Data folder with MPQ files. Choose the folder "
                                                           @"that holds Data (and Wow.exe).", folder.lastPathComponent];
        [alert runModal];
        start = folder;
    }
}

// The game folder, with a trailing slash.
std::string ChooseGameFolder(const char* processDirectory) {
    NSString* local = [NSString stringWithUTF8String:processDirectory];
    bool ask = (NSEvent.modifierFlags & NSEventModifierFlagOption) != 0;

    if (!ask && HasGameData(local)) {
        return processDirectory;
    }

    NSUserDefaults* defaults = NSUserDefaults.standardUserDefaults;
    NSString* saved = [defaults stringForKey:kGameFolderKey];

    if (!ask && saved && HasGameData(saved)) {
        return std::string(saved.UTF8String) + "/";
    }

    NSString* chosen = AskForGameFolder(saved ? saved : local);

    if (!chosen) {
        // The client reports the missing data itself.
        return processDirectory;
    }

    [defaults setObject:chosen forKey:kGameFolderKey];
    return std::string(chosen.UTF8String) + "/";
}

}

int main() {
    // Writes to a closed connection must fail with EPIPE rather than end the process.
    signal(SIGPIPE, SIG_IGN);

    @autoreleasepool {
        // Keep App Nap from throttling the game while its window is covered or hidden: it would stall the network and
        // the world. Idle system sleep is still allowed.
        static id<NSObject> s_activity = [NSProcessInfo.processInfo beginActivityWithOptions:NSActivityUserInitiatedAllowingIdleSystemSleep reason:@"Playing World of Warcraft"];
        (void)s_activity;

        char directory[1024];

        if (Blizzard::Process::GetProcessDirectory(directory, sizeof(directory)) && directory[0]) {
            std::string game = ChooseGameFolder(directory);

            if (game != directory) {
                MacSetGameDirectory(game.c_str());
            }

            chdir(game.c_str());
        }
    }

    StormRtlInitialize();

    return CommonMain();
}
