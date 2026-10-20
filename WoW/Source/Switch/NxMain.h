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

// The folder holding the NRO, from the path the loader gives, with a trailing slash.
void NxMainGameFolder(char* buffer, size_t size);

// Whether the folder has Data/ with MPQs in it.
bool NxMainHasGameData(const char* folder);

void NxMainShowError(const char* message);

#endif
