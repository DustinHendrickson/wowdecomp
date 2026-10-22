#ifndef WOW_SOURCE_SWITCH_NXMAIN_H
#define WOW_SOURCE_SWITCH_NXMAIN_H

// libnx setup for the Switch entry point (main.cpp), apart from the game's headers.

#include <cstddef>
#include <cstdint>

// Sockets, and the debug console over nxlink when started from nxlink. Returns false with a
// message for the player when the game can't run: as an applet (started from the album rather than over a game), it
// gets well under the memory it needs.
bool NxMainInitialize();

void NxMainShutdown();

// Ends the process once the game has shut down and saved (after NxMainShutdown), restoring the clocks and closing the
// console log. It skips the C++ static destructors and libnx's service teardown, which run with threads still about
// (audio, graphics, network) and are the likely place of a crash on quitting that left no report ("software closed"):
// the system then returns to the HOME menu rather than the Homebrew Menu.
[[noreturn]] void NxMainExit(int result);

// Without nxlink, stdout and stderr go to Logs/Console.txt in the game folder (the working directory by then), so a run
// on the console or in an emulator leaves its output behind.
void NxMainRedirectConsole();

// The folder holding the NRO, from the path the loader gives, '/'-rooted (no "sdmc:") with a trailing slash.
void NxMainGameFolder(char* buffer, size_t size);

// Whether the folder has Data/ with MPQs in it.
bool NxMainHasGameData(const char* folder);

void NxMainShowError(const char* message);

// Copies the add-ons packed in the NRO (Tools/Switch/romfs/AddOns) into Interface/AddOns in the game folder (the
// working directory), replacing older copies, so they always match the build.
void NxMainInstallAddOns();

#endif
