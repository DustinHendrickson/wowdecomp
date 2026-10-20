#ifndef ENGINE_SOURCE_OS_SWITCH_NXSYSTEM_H
#define ENGINE_SOURCE_OS_SWITCH_NXSYSTEM_H

// libnx calls the shared Mac/ layers make on the Switch. They sit behind plain functions because <switch.h> declares
// names (Handle, Event, Result, ...) that collide with the game's headers.

#include <cstdint>

// Memory the process may use: the heap libnx was given (about 3.2 GB when started over a game, under 0.5 GB as an
// applet).
uint64_t NxGetTotalMemory();

// Minutes east of UTC for the console's time zone.
int32_t NxGetTimeZoneMinutes();

// Opens a URL in the web applet. Returns nonzero on success.
int32_t NxOpenURL(const char* url);

#endif
