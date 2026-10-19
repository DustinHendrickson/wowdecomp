#ifndef ENGINE_SOURCE_OS_MAC_WIN32FILES_H
#define ENGINE_SOURCE_OS_MAC_WIN32FILES_H

// Win32 file-API constants for Os/Win64/OsFile.cpp, which builds on macOS unchanged: it wraps Blizzard::File, and its
// callers pass these values through the OS_* names in OsFile.h.

#include <cstdint>

#define GENERIC_READ 0x80000000
#define GENERIC_WRITE 0x40000000

#define FILE_SHARE_READ 0x00000001
#define FILE_SHARE_WRITE 0x00000002

#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define TRUNCATE_EXISTING 5

#define FILE_ATTRIBUTE_READONLY 0x00000001
#define FILE_ATTRIBUTE_HIDDEN 0x00000002
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define FILE_ATTRIBUTE_NORMAL 0x00000080

#define INVALID_HANDLE_VALUE (reinterpret_cast<HANDLE>(static_cast<intptr_t>(-1)))

#define ERROR_INVALID_PARAMETER 87

// The thread's last-error code isn't kept on macOS; Storm's SErrSetLastError is the error state callers read.
inline void SetLastError(DWORD error) {
}

#endif
