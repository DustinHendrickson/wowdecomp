// macOS implementation of Os/Win64/Tls.cpp on pthread keys. Index 0 is a valid key here as it is a valid TLS index on
// Windows; TLS_OUT_OF_INDEXES (0xFFFFFFFF) reports failure.

#include <cstdint>
#include <pthread.h>
#include "Engine/Source/Os/Tls.h"

uint32_t OsTlsAlloc() {
    pthread_key_t key;
    return pthread_key_create(&key, nullptr) == 0 ? static_cast<uint32_t>(key) : 0xFFFFFFFF;
}

void OsTlsFree(uint32_t index) {
    pthread_key_delete(static_cast<pthread_key_t>(index));
}

void* OsTlsGetValue(uint32_t index) {
    return pthread_getspecific(static_cast<pthread_key_t>(index));
}

int32_t OsTlsSetValue(uint32_t index, void* value) {
    return pthread_setspecific(static_cast<pthread_key_t>(index), value) == 0;
}
