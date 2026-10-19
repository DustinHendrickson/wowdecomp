// macOS version of CGlueMgr::PatchDownloadExecuteCommand (Win64/PatchDownloadGlue.cpp). Windows elevates the downloaded
// patcher with ShellExecute("runas"); there is no elevation step here, the program just runs.

#include "WoW/Source/Glue/CGlueMgr.h"
#include "Engine/Source/BlizzardCore/Source/System/Process/Process.h"

bool CGlueMgr::PatchDownloadExecuteCommand(const char* path, const char* arguments) {
    return Blizzard::Process::LaunchChildProcessWithArguments(path, arguments, nullptr, nullptr);
}
