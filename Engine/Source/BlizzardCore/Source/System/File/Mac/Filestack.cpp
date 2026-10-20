// macOS implementation of the native file layer (Win64/Filestack.cpp) on POSIX.
//
// StreamRecord::m_fileHandle holds the file descriptor; (void*)-1 plays the part of INVALID_HANDLE_VALUE. Paths arrive
// in Blizzard form (either separator, UTF-8) and are converted to '/' before use. They are UTF-8 natively, so the
// Windows code page conversions (FromNativeName, ToNative) become plain copies. APFS is case-insensitive by default,
// which the game's mixed-case paths rely on.

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>
#include "Engine/Source/BlizzardCore/Source/System/Debug/Debug.h"
#include "Engine/Source/BlizzardCore/Source/System/File/File.h"
#include "Engine/Source/BlizzardCore/Source/System/Lock/Lock.h"
#include "Engine/Source/BlizzardCore/Source/System/Memory/MemoryStorm.h"
#include "Engine/Source/BlizzardCore/Source/System/String/Path.h"
#include "Engine/Source/BlizzardCore/Source/System/String/String.h"
#include "Engine/Source/BlizzardCore/Source/System/Time/Time.h"
#include "Engine/Source/Storm/H/storm.h"

#undef CreateDirectory
#undef GetFreeSpace
#undef RemoveDirectory

// IOFunc takes an OVERLAPPED* (opaque in Platform.h); on macOS it carries the position for a positional transfer.
// nullptr means "at the current position", as on Windows.
struct _OVERLAPPED {
    int64_t m_offset;
};

System_File::IOFunc System_File::read_func = System_File::read_first;
System_File::IOFunc System_File::write_func = System_File::write_first;
bool System_File::s_read_init;
Blizzard::Lock::Atomic System_File::s_readInitCount;
bool System_File::s_write_init;
Blizzard::Lock::Atomic System_File::s_writeInitCount;
Blizzard::Lock::Mutex System_File::s_ioMutex;
Blizzard::Lock::Mutex* System_File::s_ioLock;

namespace {

void* const kInvalidHandle = reinterpret_cast<void*>(static_cast<intptr_t>(-1));

// Blizzard::File attribute bits (see ToNativeAttributes on Windows).
const int32_t kAttributeReadOnly = 0x1;
const int32_t kAttributeHidden = 0x2;
const int32_t kAttributeNormal = 0x20;
const int32_t kAttributeDirectory = 0x40;

const int64_t kEpochOffsetSeconds = 946684800;

int Descriptor(const Blizzard::File::StreamRecord* stream) {
    return static_cast<int>(reinterpret_cast<intptr_t>(stream->m_fileHandle));
}

void* Handle(int fd) {
    return reinterpret_cast<void*>(static_cast<intptr_t>(fd));
}

bool Valid(const Blizzard::File::StreamRecord* stream) {
    return stream->m_fileHandle != kInvalidHandle;
}

std::string NativePath(const char* path) {
    std::string native = path ? path : "";
    std::replace(native.begin(), native.end(), '\\', '/');
    return native;
}

Blizzard::Time::Timestamp FromTimespec(const timespec& time) {
    return (static_cast<int64_t>(time.tv_sec) - kEpochOffsetSeconds) * 1000000000 + time.tv_nsec;
}

timespec ToTimespec(Blizzard::Time::Timestamp time) {
    int64_t seconds = time / 1000000000;
    int64_t nanoseconds = time % 1000000000;

    if (nanoseconds < 0) {
        nanoseconds += 1000000000;
        seconds--;
    }

    timespec result;
    result.tv_sec = static_cast<time_t>(seconds + kEpochOffsetSeconds);
    result.tv_nsec = static_cast<long>(nanoseconds);
    return result;
}

const char* BaseName(const std::string& path) {
    size_t slash = path.find_last_of('/');
    return path.c_str() + (slash == std::string::npos ? 0 : slash + 1);
}

int32_t AttributesFromStat(const struct stat& info, const std::string& path) {
    int32_t attributes = 0;

    if (!(info.st_mode & S_IWUSR)) {
        attributes |= kAttributeReadOnly;
    }

#if defined(__APPLE__)
    bool hidden = (info.st_flags & UF_HIDDEN) != 0;
#else
    bool hidden = false;
#endif

    if (BaseName(path)[0] == '.' || hidden) {
        attributes |= kAttributeHidden;
    }

    if (S_ISDIR(info.st_mode)) {
        attributes |= kAttributeDirectory;
    } else {
        attributes |= kAttributeNormal;
    }

    return attributes;
}

void FillInfo(Blizzard::File::FileInfo* info, const struct stat& data, const std::string& path) {
    info->m_size = data.st_size;
#if defined(__APPLE__)
    info->m_createTime = FromTimespec(data.st_birthtimespec);
    info->m_modTime = FromTimespec(data.st_mtimespec);
    info->m_accessTime = FromTimespec(data.st_atimespec);
#else
    // No birth time: the change time stands in, as Linux tools use it.
    info->m_createTime = FromTimespec(data.st_ctim);
    info->m_modTime = FromTimespec(data.st_mtim);
    info->m_accessTime = FromTimespec(data.st_atim);
#endif
    info->m_attributes = AttributesFromStat(data, path);
    info->m_regularFile = (info->m_attributes & kAttributeNormal) != 0;
    info->m_existence = ((info->m_attributes & kAttributeDirectory) != 0) + 1;
}

// Mirrors ToCreateFlags on Windows: bit 0 read, bit 1 write, 0x100 / 0x400 create or truncate, 0x800 must not exist,
// 0x1000 must exist, otherwise open or create. Share-mode bits have no POSIX meaning.
int OpenFlags(int32_t mode) {
    int flags;

    if ((mode & 3) == 3) {
        flags = O_RDWR;
    } else if (mode & 2) {
        flags = O_WRONLY;
    } else {
        flags = O_RDONLY;
    }

    if ((mode & 0x100) || (mode & 0x400)) {
        flags |= O_CREAT | ((mode & 0x800) ? O_EXCL : O_TRUNC);
    } else if (mode & 0x800) {
        flags |= O_CREAT | O_EXCL;
    } else if (!(mode & 0x1000)) {
        flags |= O_CREAT;
    }

    return flags | O_CLOEXEC;
}

int OpenNative(const std::string& path, int32_t mode) {
    int fd = open(path.c_str(), OpenFlags(mode), 0666);

    if (fd < 0) {
        return -1;
    }

    // Keep descriptors off 0-2 so a handle never reads as nullptr (no stream) in shared code. (The Switch's file
    // descriptors for files start above the standard streams.)
#if defined(__APPLE__)
    if (fd < 3) {
        int moved = fcntl(fd, F_DUPFD_CLOEXEC, 3);
        close(fd);
        fd = moved;
    }

    if (fd >= 0 && (mode & 0x40)) {
        fcntl(fd, F_NOCACHE, 1);
    }
#endif

    return fd;
}

// Removes "." and ".." segments and doubled separators from an absolute '/' path.
std::string Canonicalize(const std::string& path) {
    std::vector<std::string> parts;
    size_t start = 0;

    while (start <= path.size()) {
        size_t end = path.find('/', start);

        if (end == std::string::npos) {
            end = path.size();
        }

        std::string part = path.substr(start, end - start);

        if (part == "..") {
            if (!parts.empty()) {
                parts.pop_back();
            }
        } else if (!part.empty() && part != ".") {
            parts.push_back(part);
        }

        start = end + 1;
    }

    std::string result;

    for (const std::string& part : parts) {
        result += '/';
        result += part;
    }

    return result.empty() ? "/" : result;
}

}

int32_t System_File::ToNativeAttributes(int32_t attributes) {
    // Only the read-only bit has a POSIX counterpart; SetAttributes applies it as permission bits.
    return attributes & kAttributeReadOnly;
}

int32_t System_File::FromNativeAttributes(int32_t attributes) {
    return attributes;
}

bool System_File::ReadP(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    OVERLAPPED overlapped;
    overlapped.m_offset = parms->m_pos;

    return read_func(parms->m_stream, parms->m_buffer, &overlapped, &parms->m_amount);
}

bool System_File::WriteP(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    OVERLAPPED overlapped;
    overlapped.m_offset = parms->m_pos;

    return write_func(parms->m_stream, parms->m_buffer, &overlapped, &parms->m_amount);
}

bool System_File::Close(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    Blizzard::File::StreamRecord* stream = parms->m_stream;

    if (stream->m_fileHandle && Valid(stream)) {
        close(Descriptor(stream));
    }

    SMemFree(stream, __FILE__, __LINE__, 0);

    return true;
}

bool System_File::GetRootChars(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    const char* path = parms->m_path;
    parms->m_pos = path && (path[0] == '/' || path[0] == '\\') ? 1 : 0;

    return true;
}

void System_File::GetFileInfoByFile(Blizzard::File::StreamRecord* file) {
    if (file->m_infoValid) {
        return;
    }

    file->m_infoValid = false;
    struct stat data;

    if (fstat(Descriptor(file), &data) != 0) {
        return;
    }

    int64_t size = file->m_info.m_size;
    FillInfo(&file->m_info, data, NativePath(file->m_pathname));

    // A streamed file reports the size of the whole file, not of what has been downloaded so far.
    if (file->m_streaming && file->m_streaming->m_entry) {
        file->m_info.m_size = size;
    }

    file->m_infoValid = true;
}

bool System_File::Exists(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    struct stat data;

    if (stat(NativePath(parms->m_path).c_str(), &data) != 0) {
        parms->m_info->m_existence = 0;
    } else if (S_ISDIR(data.st_mode)) {
        parms->m_info->m_existence = 2;
    } else {
        parms->m_info->m_existence = 1;
    }

    return true;
}

bool System_File::MakeAbsolutePath(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    std::string path = NativePath(parms->m_path);

    if (path.empty() || path[0] != '/') {
        char cwd[PATH_MAX];

        if (!getcwd(cwd, sizeof(cwd))) {
            cwd[0] = '\0';
        }

        path = std::string(cwd) + "/" + path;
    }

    std::string absolute = Canonicalize(path);
    Blizzard::String::Copy(parms->m_cwdbuf, absolute.c_str(), parms->m_cwdbuflen);

    const char* original = parms->m_path;
    size_t length = original ? strlen(original) : 0;

    if (length && (original[length - 1] == '/' || original[length - 1] == '\\')) {
        Blizzard::String::ForceTrailingSeparator(parms->m_cwdbuf, parms->m_cwdbuflen, '/');
    }

    return true;
}

bool System_File::Move(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    std::string src = NativePath(parms->m_path);
    std::string dst = NativePath(parms->m_newpath);

    // MoveFile fails when the destination exists; rename would replace it.
    struct stat data;

    if (lstat(dst.c_str(), &data) == 0) {
        return false;
    }

    return rename(src.c_str(), dst.c_str()) == 0;
}

bool System_File::Delete(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    return unlink(NativePath(parms->m_path).c_str()) == 0;
}

bool System_File::read_overlapped(Blizzard::File::StreamRecord* file, void* buffer, OVERLAPPED* overlapped, int32_t* count) {
    if (!Valid(file)) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    if (!*count) {
        return true;
    }

    int fd = Descriptor(file);
    size_t remaining = static_cast<size_t>(*count);
    uint8_t* cursor = static_cast<uint8_t*>(buffer);
    int64_t offset = overlapped ? overlapped->m_offset : -1;

    // Like ReadFile, return what is there: a short count at end of file is success.
    while (remaining) {
        ssize_t got = overlapped ? pread(fd, cursor, remaining, offset) : read(fd, cursor, remaining);

        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }

            *count = static_cast<int32_t>(cursor - static_cast<uint8_t*>(buffer));
            Blizzard::File::SetLastError(5);
            Blizzard::File::AddToLastErrorStack(5, Blizzard::String::QuickFormat<1024>("POSIX Read - %s", file->m_pathname).m_buffer, 1);
            return false;
        }

        if (got == 0) {
            break;
        }

        cursor += got;
        remaining -= static_cast<size_t>(got);
        offset += got;
    }

    *count = static_cast<int32_t>(cursor - static_cast<uint8_t*>(buffer));

    return true;
}

bool System_File::write_overlapped(Blizzard::File::StreamRecord* file, void* buffer, OVERLAPPED* overlapped, int32_t* count) {
    if (!Valid(file)) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    if (!*count) {
        return true;
    }

    file->m_infoValid = false;

    int fd = Descriptor(file);
    size_t remaining = static_cast<size_t>(*count);
    const uint8_t* cursor = static_cast<const uint8_t*>(buffer);
    int64_t offset = overlapped ? overlapped->m_offset : -1;

    while (remaining) {
        ssize_t put = overlapped ? pwrite(fd, cursor, remaining, offset) : write(fd, cursor, remaining);

        if (put < 0) {
            if (errno == EINTR) {
                continue;
            }

            *count = static_cast<int32_t>(cursor - static_cast<const uint8_t*>(buffer));
            Blizzard::File::SetLastError(6);
            Blizzard::File::AddToLastErrorStack(6, Blizzard::String::QuickFormat<1024>("POSIX Write - %s", file->m_pathname).m_buffer, 1);
            return false;
        }

        cursor += put;
        remaining -= static_cast<size_t>(put);
        offset += put;
    }

    *count = static_cast<int32_t>(cursor - static_cast<const uint8_t*>(buffer));

    return true;
}

bool System_File::ToCreateFlags(uint32_t flags, DWORD* sharemode, DWORD* desiredaccess, DWORD* creationdisposition, DWORD* flagsandattributes) {
    // Windows-only; OpenFlags does this on macOS.
    return false;
}

bool System_File::SetWorkingDirectory(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    if (!parms->m_path) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    return chdir(NativePath(parms->m_path).c_str()) == 0;
}

bool System_File::Create(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    Blizzard::File::SetLastError(9);

    return false;
}

bool System_File::GetWorkingDirectory(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    if (!parms->m_cwdbuf || parms->m_cwdbuflen <= 0) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    *parms->m_cwdbuf = '\0';
    char buffer[PATH_MAX];

    if (getcwd(buffer, sizeof(buffer))) {
        FromNativeName(parms->m_cwdbuf, parms->m_cwdbuflen, buffer);
    }

    return true;
}

bool System_File::ProcessDirFast(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    void* cookie = parms->m_cookie;
    const char* path = parms->m_path;
    Blizzard::File::ProcessDirCallback callback = parms->m_dircallback;
    std::string native = NativePath(path);

    DIR* directory = opendir(native.c_str());

    if (!directory) {
        Blizzard::Debug::Printf(Blizzard::Debug::StderrOutput, "failed to open %s\n", path);
        Blizzard::File::SetLastError(8);
        return false;
    }

    Blizzard::File::ProcessDirParms dirParms;
    dirParms.m_pathRoot = path;
    dirParms.m_cookie = cookie;

    while (dirent* entry = readdir(directory)) {
        const char* name = entry->d_name;

        if (name[0] == '.' && (!name[1] || (name[1] == '.' && !name[2]))) {
            continue;
        }

        bool isDir = entry->d_type == DT_DIR;

        if (entry->d_type == DT_UNKNOWN || entry->d_type == DT_LNK) {
            struct stat data;
            isDir = stat((native + "/" + name).c_str(), &data) == 0 && S_ISDIR(data.st_mode);
        }

        char filename[1024];
        FromNativeName(filename, sizeof(filename), name);
        dirParms.m_filename = filename;
        dirParms.m_isDir = isDir;

        if (!callback(dirParms)) {
            closedir(directory);
            return false;
        }
    }

    closedir(directory);

    return true;
}

bool System_File::Flush(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    if (!Valid(parms->m_stream)) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    return fsync(Descriptor(parms->m_stream)) == 0;
}

bool System_File::GetFileInfo(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    if (parms->m_path) {
        std::string path = NativePath(parms->m_path);
        struct stat data;

        if (stat(path.c_str(), &data) != 0) {
            Blizzard::File::SetLastError(8);
            return false;
        }

        FillInfo(parms->m_info, data, path);
        parms->m_info->m_path = nullptr;

        return true;
    }

    if (parms->m_stream) {
        GetFileInfoByFile(parms->m_stream);
        Blizzard::File::StreamRecord* stream = parms->m_stream;
        parms->m_info = &stream->m_info;

        if (!stream->m_info.m_path) {
            stream->m_info.m_path = stream->m_pathname;
        }

        return parms->m_stream->m_infoValid;
    }

    Blizzard::File::SetLastError(8);

    return false;
}

bool System_File::GetFreeSpace(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    const char* path = parms->m_path;

    if (path && *path) {
        std::string native = NativePath(path);
        native.resize(Blizzard::String::FindFilename(native.c_str()) - native.c_str());

        if (native.empty()) {
            native = ".";
        }

        struct statvfs data;

        if (statvfs(native.c_str(), &data) == 0) {
            parms->m_pos = static_cast<int64_t>(data.f_bavail) * static_cast<int64_t>(data.f_frsize);
            return true;
        }
    }

    Blizzard::File::SetLastError(8);

    return false;
}

bool System_File::GetPos(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    Blizzard::File::StreamRecord* stream = parms->m_stream;
    off_t position;

    if (!Valid(stream) || (position = lseek(Descriptor(stream), 0, SEEK_CUR)) < 0) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    parms->m_pos = position;

    return true;
}

bool System_File::IsAbsolutePath(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    const char* path = parms->m_path;

    if (path && (path[0] == '/' || path[0] == '\\')) {
        return true;
    }

    Blizzard::File::SetLastError(8);

    return false;
}

bool System_File::IsReadOnly(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    struct stat data;

    if (stat(NativePath(parms->m_path).c_str(), &data) == 0) {
        return !(data.st_mode & S_IWUSR);
    }

    Blizzard::File::SetLastError(8);

    return false;
}

bool System_File::CreateDirectory(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    const char* path = parms->m_path;

    if (!path) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    // grantEveryone asks for a directory all users can write to.
    mode_t mode = parms->m_grantEveryone ? 0777 : 0755;
    std::string native = NativePath(path);

    while (native.size() > 1 && native.back() == '/') {
        native.pop_back();
    }

    auto makeOne = [mode](const std::string& directory) {
        if (mkdir(directory.c_str(), mode) == 0) {
            return true;
        }

        struct stat data;
        return (errno == EEXIST || errno == EACCES) && stat(directory.c_str(), &data) == 0 && S_ISDIR(data.st_mode);
    };

    if (parms->m_recursive) {
        for (size_t slash = native.find('/', 1); slash != std::string::npos; slash = native.find('/', slash + 1)) {
            if (!makeOne(native.substr(0, slash))) {
                Blizzard::File::SetLastError(8);
                return false;
            }
        }
    }

    if (!makeOne(native)) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    if (parms->m_grantEveryone) {
        chmod(native.c_str(), 0777);
    }

    return true;
}

bool System_File::Copy(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    bool overwrite = parms->m_overwrite;
    std::string src = NativePath(parms->m_path);
    std::string dst = NativePath(parms->m_newpath);

    Blizzard::File::StreamRecord* input;

    if (!Blizzard::File::Open(src.c_str(), 0x1005, input)) {
        Blizzard::File::SetLastError(2);
        return false;
    }

    Blizzard::File::StreamRecord* output;

    if (!Blizzard::File::Open(dst.c_str(), (overwrite ? 0x400 : 0xC00) | 0x2, output)) {
        Blizzard::File::Close(input);
        Blizzard::File::SetLastError(4);
        return false;
    }

    int64_t size = Blizzard::File::GetFileInfo(input)->m_size;
    int64_t offset = 0;
    bool result = true;
    void* buffer = Blizzard::Memory::Allocate(size >= 0xA00000 ? 0xA00000 : static_cast<uint32_t>(size));

    while (offset < size) {
        int32_t count = size - offset >= 0xA00000 ? 0xA00000 : static_cast<int32_t>(size - offset);

        if (!Blizzard::File::Read(input, buffer, offset, &count) || !count || !Blizzard::File::Write(output, buffer, offset, &count)) {
            result = false;
            break;
        }

        offset += count;
    }

    Blizzard::Memory::Free(buffer);
    Blizzard::File::Close(input);
    Blizzard::File::Close(output);

    if (!result) {
        Blizzard::File::Delete(dst.c_str());
    }

    return result;
}

bool System_File::Open(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    int32_t mode = parms->m_mode;
    std::string path = NativePath(parms->m_path);
    int fd = OpenNative(path, mode);

    if (fd < 0) {
        Blizzard::File::SetLastError(4);
        Blizzard::File::AddToLastErrorStack(4, Blizzard::String::QuickFormat<1024>("POSIX Open - %s", path.c_str()).m_buffer, 1);
        return false;
    }

    Blizzard::File::StreamRecord* stream = parms->m_stream;

    if (!stream) {
        uint32_t length = static_cast<uint32_t>(path.size());
        stream = static_cast<Blizzard::File::StreamRecord*>(SMemAlloc(sizeof(Blizzard::File::StreamRecord) + length + 1, __FILE__, __LINE__, SMEM_FLAG_ZEROMEMORY));
        stream->m_pathname = reinterpret_cast<char*>(stream + 1);
        Blizzard::String::Copy(stream->m_pathname, path.c_str(), length + 1);
    }

    stream->m_fileHandle = Handle(fd);
    stream->m_mode = mode;
    stream->m_infoValid = false;
    stream->m_info.m_path = stream->m_pathname;
    GetFileInfoByFile(stream);

    if (mode & 0x200) {
        Blizzard::File::SetPos(stream, stream->m_info.m_size, 0);
    }

    parms->m_stream = stream;

    return true;
}

bool System_File::Read(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    return read_overlapped(parms->m_stream, parms->m_buffer, nullptr, &parms->m_amount);
}

bool System_File::RemoveDirectory(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    const char* path = parms->m_path;

    if (parms->m_recursive) {
        return Blizzard::File::RemoveDirectoryAndContents(path, false);
    }

    if (!path) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    return rmdir(NativePath(path).c_str()) == 0;
}

bool System_File::SetCacheMode(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    // Windows reopens the file with or without FILE_FLAG_NO_BUFFERING; F_NOCACHE toggles the same thing in place.
    Blizzard::File::StreamRecord* stream = parms->m_stream;
    stream->m_mode = parms->m_mode | (stream->m_mode & ~0x40);

#if defined(F_NOCACHE)
    bool failed = !Valid(stream) || fcntl(Descriptor(stream), F_NOCACHE, (stream->m_mode & 0x40) ? 1 : 0) == -1;
#else
    // The SD card has no cache to bypass.
    bool failed = !Valid(stream);
#endif

    if (failed) {
        Blizzard::File::SetLastError(4);
        Blizzard::File::AddToLastErrorStack(4, "POSIX SetCacheMode", 1);
        return false;
    }

    return true;
}

bool System_File::SetEOF(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    int64_t pos = parms->m_pos;
    Blizzard::File::StreamRecord* stream = parms->m_stream;
    int32_t whence = parms->m_posMode;
    int64_t offset;

    if (!Blizzard::File::GetPos(stream, &offset) || !Blizzard::File::SetPos(stream, pos, whence)) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    off_t end = lseek(Descriptor(stream), 0, SEEK_CUR);
    bool result = end >= 0 && ftruncate(Descriptor(stream), end) == 0;
    stream->m_infoValid = false;
    Blizzard::File::SetPos(stream, offset, 0);

    return result;
}

bool System_File::SetAttributes(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    if (parms->m_has.m_all & 0x10) {
        Blizzard::Time::Timestamp time = parms->m_info->m_modTime;
#if defined(__SWITCH__)
        // The SD card's FAT has modification times, but libnx can't set them; they stay at the time of the write.
        bool failed = false;
        (void)time;
#else
        timespec times[2];
        times[0].tv_sec = 0;
        times[0].tv_nsec = UTIME_OMIT;
        times[1] = ToTimespec(time);
        bool failed = futimens(Descriptor(parms->m_stream), times) != 0;
#endif

        if (failed) {
            Blizzard::File::SetLastError(8);
            return false;
        }

        parms->m_stream->m_info.m_modTime = time;
        parms->m_has.m_all &= ~0x10u;
    }

    if (parms->m_has.m_all & 0x4) {
        std::string path = NativePath(parms->m_path);
        struct stat data;

        if (stat(path.c_str(), &data) != 0) {
            Blizzard::File::SetLastError(8);
            return false;
        }

        mode_t mode = data.st_mode & 07777;

        if (ToNativeAttributes(parms->m_info->m_attributes) & kAttributeReadOnly) {
            mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
        } else {
            mode |= S_IWUSR;
        }

        if (chmod(path.c_str(), mode) != 0) {
            Blizzard::File::SetLastError(8);
            return false;
        }

        parms->m_has.m_all &= ~0x4u;
    }

    return parms->m_has.m_all == 0;
}

bool System_File::SetPos(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    Blizzard::File::StreamRecord* stream = parms->m_stream;

    if (!Valid(stream)) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    // 0 = from the start, 1 = from the current position, 2 = from the end (FILE_BEGIN / CURRENT / END).
    int whence = parms->m_posMode == 0 ? SEEK_SET : (parms->m_posMode == 1 ? SEEK_CUR : SEEK_END);

    return lseek(Descriptor(stream), static_cast<off_t>(parms->m_pos), whence) >= 0;
}

bool System_File::Write(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    return write_overlapped(parms->m_stream, parms->m_buffer, nullptr, &parms->m_amount);
}

bool System_File::read_locked(Blizzard::File::StreamRecord* file, void* buffer, OVERLAPPED* overlapped, int32_t* count) {
    // pread/pwrite are atomic with respect to the file position, so the locked fallback is never selected.
    return read_overlapped(file, buffer, overlapped, count);
}

bool System_File::write_locked(Blizzard::File::StreamRecord* file, void* buffer, OVERLAPPED* overlapped, int32_t* count) {
    return write_overlapped(file, buffer, overlapped, count);
}

void System_File::detect_io_mode() {
    read_func = read_overlapped;
    write_func = write_overlapped;
}

bool System_File::read_first(Blizzard::File::StreamRecord* file, void* buffer, OVERLAPPED* overlapped, int32_t* count) {
    if (!s_read_init) {
        if (s_readInitCount.Increment() == 1) {
            detect_io_mode();
            s_read_init = true;
        } else {
            do {
                usleep(1000);
            } while (!s_read_init);
        }
    }

    return read_func(file, buffer, overlapped, count);
}

bool System_File::write_first(Blizzard::File::StreamRecord* file, void* buffer, OVERLAPPED* overlapped, int32_t* count) {
    if (!s_write_init) {
        if (s_writeInitCount.Increment() == 1) {
            detect_io_mode();
            s_write_init = true;
        } else {
            do {
                usleep(1000);
            } while (!s_write_init);
        }
    }

    return write_func(file, buffer, overlapped, count);
}

void System_File::FromNativeName(char* buffer, uint32_t buffersize, const char* str) {
    Blizzard::String::Copy(buffer, str ? str : "", buffersize);
}
