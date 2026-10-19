// macOS implementation of Os/Win64/OsSecureRandom.cpp. arc4random_buf draws from the kernel CSPRNG and can't fail, so
// the hashed-entropy fallback the Windows code keeps for missing crypto providers isn't needed.

#include <cstdint>
#include <cstdlib>
#include "Engine/Source/Os/OsSecureRandom.h"

void IOsSecureRandom(uint8_t* buffer, uint32_t size) {
    arc4random_buf(buffer, size);
}

void OsSecureRandom(uint8_t* buffer, uint32_t size) {
    arc4random_buf(buffer, size);
}
