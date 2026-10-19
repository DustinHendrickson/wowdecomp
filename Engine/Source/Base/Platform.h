#ifndef ENGINE_SOURCE_BASE_PLATFORM_H
#define ENGINE_SOURCE_BASE_PLATFORM_H

// Portability shim for non-Windows builds (macOS arm64).
//
// The build force-includes this header into every project source on macOS (cmake/Platform.cmake), so shared code
// can keep the Windows types, calling conventions, intrinsics and CRT names it was decompiled with. It is a no-op on
// Windows, where the real definitions come from <windows.h> and the MSVC CRT.
//
// Scope: only what shared (non-Win32/Win64) code uses. Platform layers get real Mac implementations in Mac/
// folders instead of more emulation here. Must stay valid C as well as C++: FMOD Ex's C libraries include it too.

#if !defined(_WIN32)

#include <alloca.h>
#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <malloc/malloc.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

// Calling conventions: meaningful on x86 only. Windows x64 ignores them too.
#define __stdcall
#define __cdecl
#define __fastcall
#define __thiscall
#define WINAPI
#define APIENTRY
#define CALLBACK

#define __forceinline inline __attribute__((always_inline))

// -fms-extensions makes __int64 and friends keywords; this covers builds without it.
#if !defined(_MSC_EXTENSIONS)
#define __int8 char
#define __int16 short
#define __int32 int
#define __int64 long long
#endif

// Windows types. Windows is LLP64, so LONG / ULONG / DWORD are 32-bit even though `long` is 64-bit here.
// Objective-C has its own BOOL; the AppKit glue (.mm) uses that one. Shared code doesn't use the Win32 BOOL.
#if !defined(__OBJC__)
typedef int32_t BOOL;
#endif
typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef uint32_t UINT;
typedef int64_t LONGLONG;
typedef uint64_t ULONGLONG;
typedef void* HANDLE;
// Distinct handle types like <windows.h> under STRICT, which shared headers forward-declare (HWND__*, HINSTANCE__*).
typedef struct HWND__* HWND;
typedef struct HINSTANCE__* HINSTANCE;
typedef HINSTANCE HMODULE;
typedef void* LPVOID;
typedef const char* LPCSTR;
typedef char* LPSTR;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

// Win32 types that shared headers declare. CRITICAL_SECTION is held by value (Blizzard::Lock::Mutex, SCritSect,
// CCritSect), so it gets a real type: a recursive pthread mutex, matching critical-section semantics. DebugInfo is
// there because decompiled code clears it after DeleteCriticalSection, as the MSVC inlining did. The others only
// appear as pointer parameters of Windows-implementation functions, so they stay opaque: shared code that touches
// their members fails to compile, which marks it for a Mac version.
typedef struct _CRITICAL_SECTION {
    pthread_mutex_t mutex;
    void* DebugInfo;
} CRITICAL_SECTION;
typedef struct _OVERLAPPED OVERLAPPED;
typedef struct _SYSTEMTIME SYSTEMTIME;
typedef union _LARGE_INTEGER LARGE_INTEGER;
typedef struct _CONTEXT CONTEXT;
typedef struct _EXCEPTION_POINTERS EXCEPTION_POINTERS;

typedef struct tagRECT {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECT;

#ifndef GUID_DEFINED
#define GUID_DEFINED
typedef struct _GUID {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t Data4[8];
} GUID;
#endif

// Interlocked operations. Type-generic so they work on int32_t, uint32_t and pointers alike. Return values follow
// the Win32 API: Increment / Decrement return the new value, Exchange / ExchangeAdd / CompareExchange the old one.
#define InterlockedIncrement(p) __atomic_add_fetch((p), 1, __ATOMIC_SEQ_CST)
#define InterlockedDecrement(p) __atomic_sub_fetch((p), 1, __ATOMIC_SEQ_CST)
#define InterlockedExchange(p, v) __atomic_exchange_n((p), (v), __ATOMIC_SEQ_CST)
#define InterlockedExchangeAdd(p, v) __atomic_fetch_add((p), (v), __ATOMIC_SEQ_CST)
#define InterlockedCompareExchange(p, exchange, comparand)                                                     \
    ({                                                                                                         \
        __typeof__(*(p)) wowdecomp_expected_ = (comparand);                                                    \
        __atomic_compare_exchange_n((p), &wowdecomp_expected_, (exchange), 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); \
        wowdecomp_expected_;                                                                                   \
    })
#define InterlockedExchangePointer(p, v) InterlockedExchange((p), (v))
#define InterlockedCompareExchangePointer(p, exchange, comparand) InterlockedCompareExchange((p), (exchange), (comparand))
#define _InterlockedIncrement InterlockedIncrement
#define _InterlockedDecrement InterlockedDecrement
#define _InterlockedExchange InterlockedExchange
#define _InterlockedExchangeAdd InterlockedExchangeAdd
#define _InterlockedCompareExchange InterlockedCompareExchange

// MSVC intrinsics. Clang provides these as builtins under -fms-extensions.
#if defined(__has_builtin)
#if __has_builtin(_rotl) && __has_builtin(_BitScanForward)
#define WOWDECOMP_HAS_MS_BUILTINS 1
#endif
#endif

#if !defined(WOWDECOMP_HAS_MS_BUILTINS)
static inline uint32_t _rotl(uint32_t value, int shift) {
    shift &= 31;
    return shift ? (value << shift) | (value >> (32 - shift)) : value;
}

static inline uint32_t _rotr(uint32_t value, int shift) {
    shift &= 31;
    return shift ? (value >> shift) | (value << (32 - shift)) : value;
}

static inline unsigned char _BitScanForward(uint32_t* index, uint32_t mask) {
    if (!mask) {
        return 0;
    }
    *index = (uint32_t)__builtin_ctz(mask);
    return 1;
}

static inline unsigned char _BitScanReverse(uint32_t* index, uint32_t mask) {
    if (!mask) {
        return 0;
    }
    *index = 31u - (uint32_t)__builtin_clz(mask);
    return 1;
}
#endif

#define _byteswap_ushort(x) __builtin_bswap16(x)
#define _byteswap_ulong(x) __builtin_bswap32(x)
#define _byteswap_uint64(x) __builtin_bswap64(x)
#define __debugbreak() __builtin_debugtrap()

// MSVC CRT names.
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define stricmp strcasecmp
#define strnicmp strncasecmp
#define _snprintf snprintf
#define _vsnprintf vsnprintf
#define _strdup strdup
#define _alloca alloca
#define _finite(x) isfinite(x)
#define _isnan(x) isnan(x)
#define _msize(p) malloc_size(p)
#define _hypot hypot
#define _copysign copysign
#define _atoi64 atoll
#define _strtoi64 strtoll
#define _strtoui64 strtoull
// Single-byte check is right for the UTF-8 and C locales used here.
#define _ismbcspace(c) isspace(c)

static inline int fopen_s(FILE** stream, const char* name, const char* mode) {
    *stream = fopen(name, mode);
    return *stream ? 0 : errno;
}

// MSVC's _cexit runs atexit handlers and flushes streams without ending the process; Storm then calls _exit.
// POSIX has no way to run atexit handlers alone, so this only flushes. The Mac Storm layer should call exit() instead.
static inline void _cexit(void) {
    fflush(NULL);
}

static inline char* _strupr(char* str) {
    for (char* c = str; *c; ++c) {
        if (*c >= 'a' && *c <= 'z') {
            *c = (char)(*c - 'a' + 'A');
        }
    }
    return str;
}

static inline char* _strlwr(char* str) {
    for (char* c = str; *c; ++c) {
        if (*c >= 'A' && *c <= 'Z') {
            *c = (char)(*c - 'A' + 'a');
        }
    }
    return str;
}

#if defined(__cplusplus)
// MSVC's std::type_info::raw_name() returns the decorated name. Shared code only passes it to the allocators as a
// debug tag, so the portable name() serves.
#define raw_name name
#endif

// Critical sections are recursive mutexes, like on Windows. Shared code that sits in Win64/ folders but has no other
// Windows dependency (Filestack_Streaming.cpp) builds on macOS through these.
static inline void InitializeCriticalSection(CRITICAL_SECTION* section) {
    pthread_mutexattr_t attributes;
    pthread_mutexattr_init(&attributes);
    pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&section->mutex, &attributes);
    pthread_mutexattr_destroy(&attributes);
}

static inline void DeleteCriticalSection(CRITICAL_SECTION* section) {
    pthread_mutex_destroy(&section->mutex);
}

static inline void EnterCriticalSection(CRITICAL_SECTION* section) {
    pthread_mutex_lock(&section->mutex);
}

static inline void LeaveCriticalSection(CRITICAL_SECTION* section) {
    pthread_mutex_unlock(&section->mutex);
}

// Small Win32 calls that shared code makes directly.
static inline void Sleep(DWORD milliseconds) {
    usleep((useconds_t)milliseconds * 1000);
}

static inline void OutputDebugStringA(LPCSTR text) {
    fputs(text, stderr);
}

// ANSI code page, reported with IME events. macOS text is UTF-8.
static inline UINT GetACP(void) {
    return 65001;
}

#endif // !_WIN32

#endif
