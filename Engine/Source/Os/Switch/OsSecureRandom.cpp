// Nintendo Switch implementation of Os/Win64/OsSecureRandom.cpp, on libnx's CSPRNG (seeded from the kernel's entropy).

#include <switch.h>
#include <cstdint>
#include "Engine/Source/Os/OsSecureRandom.h"

void IOsSecureRandom(uint8_t* buffer, uint32_t size) {
    randomGet(buffer, size);
}

void OsSecureRandom(uint8_t* buffer, uint32_t size) {
    randomGet(buffer, size);
}
