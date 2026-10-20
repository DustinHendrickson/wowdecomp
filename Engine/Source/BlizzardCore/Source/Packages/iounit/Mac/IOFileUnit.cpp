// macOS implementation of the native parts of IOFileUnit (Win64/IOFileUnit.cpp).

#include <algorithm>
#include <cstdint>
#include <fcntl.h>
#include <string>
#if defined(__APPLE__)
#include <sys/mount.h>
#endif
#include <unistd.h>
#include "Engine/Source/BlizzardCore/Source/Packages/iounit/IOFileUnit.h"
#include "Engine/Source/BlizzardCore/Source/System/File/File.h"
#include "Engine/Source/BlizzardCore/Source/System/Memory/MemoryStorm.h"
#include "Engine/Source/BlizzardCore/Source/System/String/String.h"
#include "Engine/Source/Storm/H/storm.h"

namespace {

std::string NativePath(const char* path) {
    std::string native = path ? path : "";
    std::replace(native.begin(), native.end(), '\\', '/');
    return native;
}

}

bool IOFileUnit::ReadNative(uint32_t offset, const char* path, void* buffer, uint32_t count, uint32_t* bytesRead) {
    int fd = open(NativePath(path).c_str(), O_RDONLY | O_CLOEXEC);

    if (fd < 0) {
        return false;
    }

    // Uncached, like FILE_FLAG_NO_BUFFERING; the page-aligned window is kept so the reads match the Windows ones.
#if defined(F_NOCACHE)
    fcntl(fd, F_NOCACHE, 1);
#endif

    uint32_t head = offset & 0xFFF;
    uint32_t size = (head + count + 0xFFF) & ~0xFFFu;
    bool result = false;
    uint8_t* data = static_cast<uint8_t*>(SMemAlloc(size, __FILE__, __LINE__, 0));
    ssize_t got = pread(fd, data, size, static_cast<off_t>(offset - head));

    if (got >= 0) {
        uint32_t read = static_cast<uint32_t>(got);
        uint32_t available = read > head ? read - head : 0;
        uint32_t amount = available >= count ? count : available;
        Blizzard::String::MemCopy(buffer, data + head, amount);

        if (bytesRead) {
            *bytesRead = amount;
        }

        result = true;
    }

    SMemFree(data, __FILE__, __LINE__, 0);
    close(fd);

    return result;
}

IOUnbufferedFileUnit::IOUnbufferedFileUnit(Blizzard::File::StreamRecord* stream, bool ownsMemory) : IOFileUnit(stream, ownsMemory) {
    Blizzard::File::FileInfo* info = Blizzard::File::GetFileInfo(stream);
    this->m_size = info->m_size;

    // Transfer alignment is the volume's block size, falling back to 64 KB as on Windows.
#if defined(__APPLE__)
    struct statfs volume;
    std::string path = NativePath(info->m_path);

    if (statfs(path.c_str(), &volume) != 0 || !volume.f_bsize || ((volume.f_bsize - 1) & volume.f_bsize)) {
        this->m_alignMask = 0xFFFF;
    } else {
        this->m_alignMask = volume.f_bsize - 1;
    }
#else
    // The Switch reports no volume block size.
    this->m_alignMask = 0xFFFF;
#endif

    this->m_buffer = Blizzard::Memory::Allocate(this->m_alignMask + 0x100000);
    this->m_alignedBuffer = reinterpret_cast<void*>(~static_cast<uintptr_t>(this->m_alignMask) & (reinterpret_cast<uintptr_t>(this->m_buffer) + this->m_alignMask));
}
