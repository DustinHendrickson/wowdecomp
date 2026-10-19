// macOS implementation of the Os/Win64/OsVersionHash.cpp entry points.
//
// IOsVersionChecksum hashes a file's bytes, as Windows does for unloaded files. For a loaded module, Windows hashes the
// PE image with relocations undone; there are no PE images here, so that part contributes nothing to the checksum.
// OsFileVersionInfo reads Windows version resources, which Mach-O files don't have: versions read as 0.

#include <cstdint>
#include "Engine/Source/Os/OsFile.h"
#include "Engine/Source/Os/OsVersionHash.h"
#include "Engine/Source/Storm/H/storm.h"

int32_t IOsVersionChecksum(IOsVersionHashCallback& callback, const char* path, int32_t loaded) {
    if (!path || loaded) {
        return 0;
    }

    int32_t result = 0;
    void* buffer = nullptr;
    HOSFILE file = OsCreateFile(path, OS_GENERIC_READ, 1, OS_OPEN_EXISTING, OS_FILE_ATTRIBUTE_NORMAL, 0x3F3F3F3F);

    if (file != OS_INVALID_FILE) {
        uint64_t size = OsGetFileSize(file);

        if (size == static_cast<uint32_t>(size)) {
            buffer = SMemAlloc(static_cast<uint32_t>(size), __FILE__, __LINE__, 0);
            uint32_t bytesRead;

            if (buffer && OsReadFile(file, buffer, static_cast<uint32_t>(size), &bytesRead) && bytesRead == size) {
                callback.Process(buffer, static_cast<uint32_t>(size));
                result = 1;
            }
        }

        OsCloseFile(file);
    }

    if (buffer) {
        SMemFree(buffer, __FILE__, __LINE__, 0);
    }

    return result;
}

OsFileVersionInfo::OsFileVersionInfo(const char* filename) : m_fileVersion(), m_productVersion() {
    m_data.Clear();
}
