// macOS implementation of Storm's file IOUnits (Win64/IOFileUnit.cpp) on POSIX descriptors.
//
// IOFileParam carries Win32 CreateFile values (access, share, creation disposition, flags); they are decoded here.
// IOWin32FileUnit keeps its name and holds the descriptor in m_handle (never 0-2, so nullptr still means "none").

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#if defined(__APPLE__)
#include <sys/mount.h>
#endif
#include <sys/stat.h>
#include <unistd.h>
#include "Engine/Source/Storm/Source/IOUnit/IOFileUnit.h"
#include "Engine/Source/Storm/H/storm.h"

namespace {

const uint32_t kGenericRead = 0x80000000;
const uint32_t kGenericWrite = 0x40000000;
const uint32_t kCreateNew = 1;
const uint32_t kCreateAlways = 2;
const uint32_t kOpenExisting = 3;
const uint32_t kOpenAlways = 4;
const uint32_t kTruncateExisting = 5;
const uint32_t kFlagNoBuffering = 0x20000000;

int Descriptor(void* handle) {
    return static_cast<int>(reinterpret_cast<intptr_t>(handle));
}

std::string NativePath(const char* path) {
    std::string native = path ? path : "";

    for (char& c : native) {
        if (c == '\\') {
            c = '/';
        }
    }

    return native;
}

void* OpenFileHandle(const Storm::IOFileParam* param) {
    int flags;
    bool read = (param->m_access & kGenericRead) != 0;
    bool write = (param->m_access & kGenericWrite) != 0;

    if (read && write) {
        flags = O_RDWR;
    } else if (write) {
        flags = O_WRONLY;
    } else {
        flags = O_RDONLY;
    }

    switch (param->m_creation) {
        case kCreateNew:
            flags |= O_CREAT | O_EXCL;
            break;
        case kCreateAlways:
            flags |= O_CREAT | O_TRUNC;
            break;
        case kOpenAlways:
            flags |= O_CREAT;
            break;
        case kTruncateExisting:
            flags |= O_TRUNC;
            break;
        case kOpenExisting:
        default:
            break;
    }

    int fd = open(NativePath(param->m_name).c_str(), flags | O_CLOEXEC, 0666);

#if defined(__APPLE__)
    if (fd >= 0 && fd < 3) {
        int moved = fcntl(fd, F_DUPFD_CLOEXEC, 3);
        close(fd);
        fd = moved;
    }
#endif

    if (fd < 0) {
        return nullptr;
    }

    if (param->m_flags & kFlagNoBuffering) {
#if defined(F_NOCACHE)
        fcntl(fd, F_NOCACHE, 1);
#endif
    }

    return reinterpret_cast<void*>(static_cast<intptr_t>(fd));
}

}

namespace Storm {

IOUnit::IOUnit() {
    this->m_refcount = nullptr;
    this->m_count = 1;
}

void IOUnit::Release() {
    IORefcount* refcount = this->m_refcount;

    if (--this->m_count == 0) {
        delete this;
    }

    if (refcount) {
        refcount->Release();
    }
}

IOUnit::~IOUnit() {
}

IOWin32FileUnit::IOWin32FileUnit(void* handle) {
    this->m_handle = handle;
    this->m_position = -1;
}

IOWin32FileUnit::~IOWin32FileUnit() {
    close(Descriptor(this->m_handle));
    this->m_handle = nullptr;
}

bool IOWin32FileUnit::Seek(int64_t offset) {
    // Reads and writes are positional; this only records where the next one starts.
    this->m_position = offset;
    return true;
}

bool IOWin32FileUnit::Read(void* buffer, int64_t offset, uint32_t count, uint32_t* bytesRead) {
    uint32_t minimum = 1;
    uint32_t localRead;

    if (!bytesRead) {
        minimum = count;
        bytesRead = &localRead;
    }

    *bytesRead = 0;

    // Like the Windows code, retry a failing read a few times before giving up.
    for (int32_t tries = 0; tries < 3; tries++) {
        uint8_t* cursor = static_cast<uint8_t*>(buffer);
        uint32_t remaining = count;
        bool failed = false;

        while (remaining) {
            ssize_t got = pread(Descriptor(this->m_handle), cursor, remaining, static_cast<off_t>(offset + (cursor - static_cast<uint8_t*>(buffer))));

            if (got < 0) {
                if (errno == EINTR) {
                    continue;
                }

                failed = true;
                break;
            }

            if (got == 0) {
                break;
            }

            cursor += got;
            remaining -= static_cast<uint32_t>(got);
        }

        if (!failed) {
            *bytesRead = count - remaining;
            this->m_position = offset + *bytesRead;
            return *bytesRead >= minimum;
        }

        this->m_position = -1;
        usleep(10 * 1000);
    }

    return false;
}

bool IOWin32FileUnit::Write(const void* buffer, int64_t offset, uint32_t count) {
    const uint8_t* cursor = static_cast<const uint8_t*>(buffer);
    uint32_t remaining = count;

    while (remaining) {
        ssize_t put = pwrite(Descriptor(this->m_handle), cursor, remaining, static_cast<off_t>(offset + (cursor - static_cast<const uint8_t*>(buffer))));

        if (put < 0) {
            if (errno == EINTR) {
                continue;
            }

            this->m_position = -1;
            return false;
        }

        cursor += put;
        remaining -= static_cast<uint32_t>(put);
    }

    this->m_position = offset + count;
    return true;
}

bool IOWin32FileUnit::Flush() {
    return fsync(Descriptor(this->m_handle)) == 0;
}

bool IOWin32FileUnit::GetFileSize(int64_t* size) {
    struct stat info;

    if (fstat(Descriptor(this->m_handle), &info) != 0) {
        *size = 0;
        return false;
    }

    *size = info.st_size;
    return true;
}

bool IOWin32FileUnit::SetFileSize(int64_t size) {
    return ftruncate(Descriptor(this->m_handle), static_cast<off_t>(size)) == 0;
}

void* IOWin32FileUnit::Win32Handle() {
    return this->m_handle;
}

IOWin32FileUnit* IOWin32FileUnit::Open(const IOFileParam* param) {
    void* handle = OpenFileHandle(param);

    if (!handle) {
        return nullptr;
    }

    return new IOWin32FileUnit(handle);
}

bool IOUnbufferedFileUnit::Read(void* buffer, int64_t offset, uint32_t count, uint32_t* bytesRead) {
    if (!(this->m_alignMask & reinterpret_cast<uintptr_t>(buffer)) && !(this->m_alignMask & count)) {
        return this->m_unit->Read(buffer, offset, count, bytesRead);
    }

    if (!this->m_buffer) {
        return false;
    }

    uint32_t total = 0;
    bool result = true;

    while (count) {
        uint32_t chunk = count >= 0x100000 ? 0x100000 : count;
        uint32_t size = chunk;

        if (chunk & this->m_alignMask) {
            size = (this->m_alignMask + chunk) & ~this->m_alignMask;
        }

        uint32_t read;
        result = this->m_unit->Read(this->m_buffer, offset, size, &read);

        if (!result) {
            break;
        }

        read = read < chunk ? read : chunk;
        memcpy(buffer, this->m_buffer, read);
        buffer = static_cast<uint8_t*>(buffer) + read;
        offset += read;
        count -= read;
        total += read;

        if (read < chunk) {
            break;
        }
    }

    if (!bytesRead) {
        return count == 0;
    }

    *bytesRead = total;
    return result;
}

bool IOUnbufferedFileUnit::Write(const void* buffer, int64_t offset, uint32_t count) {
    if (!(this->m_alignMask & reinterpret_cast<uintptr_t>(buffer)) && !(this->m_alignMask & count)) {
        bool result = this->m_unit->Write(buffer, offset, count);

        if (offset + count > this->m_size) {
            this->m_size = offset + count;
        }

        return result;
    }

    if (!this->m_buffer) {
        return false;
    }

    bool result = true;

    while (count) {
        uint32_t chunk = count >= 0x100000 ? 0x100000 : count;
        memcpy(this->m_buffer, buffer, chunk);
        buffer = static_cast<const uint8_t*>(buffer) + chunk;

        uint32_t size = chunk;

        if (chunk & this->m_alignMask) {
            size = (this->m_alignMask + chunk) & ~this->m_alignMask;
        }

        result = this->m_unit->Write(this->m_buffer, offset, size);

        if (!result) {
            break;
        }

        offset += chunk;
        count -= chunk;
    }

    if (offset > this->m_size) {
        this->m_size = offset;
    }

    return result;
}

bool IOUnbufferedFileUnit::Flush() {
    return this->m_unit->Flush();
}

bool IOUnbufferedFileUnit::SetFileSize(int64_t size) {
    this->m_size = size;
    return this->m_unit->SetFileSize((size + this->m_alignMask) & ~static_cast<int64_t>(this->m_alignMask));
}

bool IOUnbufferedFileUnit::GetFileSize(int64_t* size) {
    *size = this->m_size;
    return true;
}

IOUnbufferedFileUnit::IOUnbufferedFileUnit(void* handle, const IOFileParam* param) {
    this->m_unit = new IOWin32FileUnit(handle);
    this->m_size = 0;
    this->m_unit->GetFileSize(&this->m_size);
    SStrCopy(this->m_path, param->m_name, sizeof(this->m_path));
    this->m_buffer = malloc(0x100000);

    // Transfers stay block aligned like Windows unbuffered I/O; the block size comes from the volume.
#if defined(__APPLE__)
    struct statfs volume;

    if (statfs(NativePath(this->m_path).c_str(), &volume) != 0 || !volume.f_bsize || ((volume.f_bsize - 1) & volume.f_bsize)) {
        this->m_alignMask = 0xFFFF;
    } else {
        this->m_alignMask = volume.f_bsize - 1;
    }
#else
    // The Switch reports no volume block size.
    this->m_alignMask = 0xFFFF;
#endif
}

void* IOUnbufferedFileUnit::Win32Handle() {
    return this->m_unit->Win32Handle();
}

IOUnbufferedFileUnit* IOUnbufferedFileUnit::Open(const IOFileParam* param) {
    void* handle = OpenFileHandle(param);

    if (!handle) {
        return nullptr;
    }

    return new IOUnbufferedFileUnit(handle, param);
}

IOUnit* IOFileUnit::Create(const IOFileParam& param, IOUnit* next) {
    if (next) {
        return nullptr;
    }

    if (param.m_flags & kFlagNoBuffering) {
        return IOUnbufferedFileUnit::Open(&param);
    }

    return IOWin32FileUnit::Open(&param);
}

IOUnbufferedFileUnit::~IOUnbufferedFileUnit() {
    free(this->m_buffer);

    if (this->m_unit) {
        delete this->m_unit;
    }

    // Aligned writes may have grown the file past its real size; trim it, as Windows does after closing.
    IOFileParam param;
    param.m_name = this->m_path;
    param.m_access = kGenericWrite;
    param.m_share = 0;
    param.m_creation = kOpenExisting;
    param.m_flags = 0;
    IOUnit* unit = IOWin32FileUnit::Open(&param);

    if (unit) {
        unit->SetFileSize(this->m_size);
        unit->Release();
    }
}

}
