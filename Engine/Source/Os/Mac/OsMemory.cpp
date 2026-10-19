// macOS implementation of Os/Win64/OsMemory.cpp: memory within +-1.5 GB of an address (for code that must be
// reachable with 32-bit displacements). mmap takes the address as a hint, so candidates are tried outward from the
// anchor in 64 KB steps, keeping the first mapping that lands in range. munmap needs the size, which OsFreeNear isn't
// given, so allocations are remembered.

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <sys/mman.h>
#include "Engine/Source/Os/OsMemory.h"

namespace {

const uintptr_t kNearRange = 0x60000000;
const uintptr_t kGranularity = 0x10000;

std::mutex s_lock;
std::map<void*, size_t>* s_allocations;

}

void* OsAllocNear(const void* anchor, size_t size) {
    uintptr_t center = reinterpret_cast<uintptr_t>(anchor) & ~(kGranularity - 1);
    uintptr_t bytes = (size + kGranularity - 1) & ~(kGranularity - 1);

    for (uintptr_t offset = kGranularity; offset < kNearRange; offset += kGranularity) {
        uintptr_t candidates[2] = { center + offset, center > offset ? center - offset : 0 };

        for (uintptr_t candidate : candidates) {
            if (!candidate) {
                continue;
            }

            void* ptr = mmap(reinterpret_cast<void*>(candidate), bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);

            if (ptr == MAP_FAILED) {
                continue;
            }

            uintptr_t address = reinterpret_cast<uintptr_t>(ptr);
            uintptr_t distance = address > center ? address - center : center - address;

            if (distance + bytes <= kNearRange) {
                std::lock_guard<std::mutex> lock(s_lock);

                if (!s_allocations) {
                    s_allocations = new std::map<void*, size_t>();
                }

                (*s_allocations)[ptr] = bytes;
                return ptr;
            }

            munmap(ptr, bytes);
        }
    }

    return nullptr;
}

void OsFreeNear(void* ptr) {
    if (!ptr) {
        return;
    }

    size_t bytes = 0;

    {
        std::lock_guard<std::mutex> lock(s_lock);

        if (s_allocations) {
            auto it = s_allocations->find(ptr);

            if (it != s_allocations->end()) {
                bytes = it->second;
                s_allocations->erase(it);
            }
        }
    }

    if (bytes) {
        munmap(ptr, bytes);
    }
}
