// macOS version of GetComponentVersionData (Win64/Streaming.cpp), which reads the BLIZZARDCOMPONENT resource from the
// executable. A Mac build carries no such resource; callers treat no data as the live (non-PTR) client.

#include <cstdint>
#include "Engine/Source/BlizzardCore/Source/Packages/Streaming/Streaming.h"

char* GetComponentVersionData(uint32_t* size) {
    *size = 0;

    return nullptr;
}
