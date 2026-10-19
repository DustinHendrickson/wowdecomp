#ifndef ENGINE_SOURCE_BLIZZARDCORE_SOURCE_SYSTEM_LOCK_MAC_WIN32SYNC_H
#define ENGINE_SOURCE_BLIZZARDCORE_SOURCE_SYSTEM_LOCK_MAC_WIN32SYNC_H

// The Win32 event and wait calls, on MacWaitable.
//
// Some Win64/ sources are decompiled logic whose only Windows dependency is a handful of sync calls (Streaming's
// Storage, Streaming, Manifests and Transport). On macOS they include this header instead of <windows.h> and build
// unchanged, so the decompiled code has one copy. Anything beyond event and wait primitives gets a real Mac/
// implementation instead of being added here. Critical sections and Sleep come from Platform.h.

#include <cstdint>
#include <ctime>
#include "Engine/Source/BlizzardCore/Source/System/Lock/Mac/Waitable.h"

#ifndef INFINITE
#define INFINITE MacWaitable::kInfinite
#endif

#ifndef WAIT_OBJECT_0
#define WAIT_OBJECT_0 MacWaitable::kWaitObject0
#endif

#ifndef WAIT_TIMEOUT
#define WAIT_TIMEOUT MacWaitable::kWaitTimeout
#endif

#ifndef WAIT_FAILED
#define WAIT_FAILED MacWaitable::kWaitFailed
#endif

inline HANDLE CreateEventA(void* attributes, BOOL manualReset, BOOL initialState, const char* name) {
    return MacWaitable::CreateEvent(manualReset != 0, initialState != 0);
}

inline BOOL SetEvent(HANDLE event) {
    return MacWaitable::SetEvent(event);
}

inline BOOL ResetEvent(HANDLE event) {
    return MacWaitable::ResetEvent(event);
}

inline BOOL CloseHandle(HANDLE handle) {
    MacWaitable::Close(handle);
    return TRUE;
}

inline DWORD WaitForSingleObject(HANDLE handle, DWORD milliseconds) {
    return MacWaitable::Wait(handle, milliseconds);
}

inline DWORD WaitForMultipleObjects(DWORD count, const HANDLE* handles, BOOL waitAll, DWORD milliseconds) {
    return MacWaitable::WaitMultiple(count, handles, waitAll != 0, milliseconds);
}

inline DWORD GetCurrentThreadId() {
    return MacWaitable::CurrentThreadId();
}

inline DWORD GetTickCount() {
    return static_cast<DWORD>(clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW) / 1000000);
}

#endif
