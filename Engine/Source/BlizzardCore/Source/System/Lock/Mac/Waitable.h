#ifndef ENGINE_SOURCE_BLIZZARDCORE_SOURCE_SYSTEM_LOCK_MAC_WAITABLE_H
#define ENGINE_SOURCE_BLIZZARDCORE_SOURCE_SYSTEM_LOCK_MAC_WAITABLE_H

#include <cstdint>
#include <pthread.h>

// Win32-style waitable objects for the macOS platform layers.
//
// BlizzardCore and Storm keep kernel object HANDLEs in their structures (events, mutexes, semaphores, threads) and
// wait on them singly or in groups, with the Win32 return codes. This is the one place those semantics live on macOS:
// a handle is a pointer to an Object, and every wait runs against a single process-wide condition variable, which keeps
// WaitMultiple (any / all) simple and correct. The objects are used for thread start-up and shutdown and for worker
// wake-ups, never in hot loops, so the shared condition variable costs nothing that matters.
namespace MacWaitable {

constexpr uint32_t kInfinite = 0xFFFFFFFF;
constexpr uint32_t kWaitObject0 = 0x00000000;
constexpr uint32_t kWaitTimeout = 0x00000102;
constexpr uint32_t kWaitFailed = 0xFFFFFFFF;
constexpr uint32_t kMaxWaitObjects = 64;

// Creation. Each returns a handle with one reference, or nullptr.
void* CreateEvent(bool manualReset, bool initialState);
void* CreateMutex(bool initialOwner);
void* CreateSemaphore(int32_t initialCount, int32_t maximumCount);
// A thread object becomes signaled, and stays signaled, once SignalThreadExit is called on it.
void* CreateThreadObject();

// Reference counting. Close drops one reference (CloseHandle); the object goes away with the last one.
void Retain(void* handle);
void Close(void* handle);

bool SetEvent(void* handle);
bool ResetEvent(void* handle);
bool ReleaseMutex(void* handle);
bool ReleaseSemaphore(void* handle, int32_t count, int32_t* previousCount);

void SetThread(void* handle, pthread_t thread, uint32_t threadId);
bool GetThread(void* handle, pthread_t* thread);
void SignalThreadExit(void* handle);

// WaitForSingleObject / WaitForMultipleObjects. Returns kWaitObject0 + index, kWaitTimeout or kWaitFailed.
uint32_t Wait(void* handle, uint32_t timeout);
uint32_t WaitMultiple(uint32_t count, void* const* handles, bool waitAll, uint32_t timeout);

// 32-bit id for the calling thread (its Mach port), like GetCurrentThreadId.
uint32_t CurrentThreadId();

}

#endif
