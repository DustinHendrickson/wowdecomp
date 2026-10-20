// Nintendo Switch implementation of Os/Win64/OsMemory.cpp: memory within +-1.5 GB of an address, for M2 data that is
// addressed with 32-bit offsets from its model. There's no mmap; the process heap is one contiguous block that libnx
// sets up at start, so an ordinary allocation is near any other unless the heap is larger than the range. One that
// lands too far away is refused, and the caller fails the load as it does when memory runs out.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include "Engine/Source/Os/OsMemory.h"

namespace {

const uintptr_t kNearRange = 0x60000000;

}

void* OsAllocNear(const void* anchor, size_t size) {
    void* ptr = aligned_alloc(16, (size + 15) & ~static_cast<size_t>(15));

    if (!ptr) {
        return nullptr;
    }

    uintptr_t address = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t center = reinterpret_cast<uintptr_t>(anchor);
    uintptr_t distance = address > center ? address - center : center - address;

    if (distance + size > kNearRange) {
        free(ptr);
        return nullptr;
    }

    return ptr;
}

void OsFreeNear(void* ptr) {
    free(ptr);
}
