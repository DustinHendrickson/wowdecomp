// macOS implementation of SServer (Win64/SServer.cpp). Windows looks up TryEnterCriticalSection in kernel32 here;
// pthread mutexes always have trylock, so there is nothing to load.

#include "Engine/Source/Storm/H/storm.h"

void SServerInitialize() {
}

void SServerDestroy() {
}
