#ifndef ENGINE_SOURCE_BLIZZARDCORE_SOURCE_SYSTEM_PROCESS_MAC_GAMEDIRECTORY_H
#define ENGINE_SOURCE_BLIZZARDCORE_SOURCE_SYSTEM_PROCESS_MAC_GAMEDIRECTORY_H

// The game folder (Data/, WTF/, Logs/, Errors/, ...) is the one holding the executable, or the .app when run from one,
// as Wow.exe's folder is on Windows. A Mac build can instead run from a folder chosen at launch (WoW/Source/Mac/main.mm),
// so the app can live anywhere: setting it here makes Blizzard::Process::GetProcessDirectory and the Os layer's
// GetModuleFileNameUTF8 (OsGetExePath, which SetPaths changes to and reads the MPQs from) report that folder.

// directory ends with a slash; nullptr clears it.
void MacSetGameDirectory(const char* directory);

// The folder set above, or nullptr.
const char* MacGetGameDirectory();

#endif
