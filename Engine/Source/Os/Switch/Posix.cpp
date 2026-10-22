// POSIX calls the shared Mac/ layers make that newlib on the Switch doesn't provide, and reports for the ways the
// process can end that libnx's exception handler doesn't see.
//
// pread and pwrite seek, transfer and seek back. The file position is shared by every thread using the descriptor, so a
// plain read on it from another thread could land between the seek and the read and get the wrong bytes (an archive's
// tables read from the wrong offset). Every read, write and lseek on a descriptor therefore takes that descriptor's lock
// (the linker wraps them: -Wl,--wrap in cmake/SwitchToolchain.cmake), and pread / pwrite hold it across all three steps.
//
// abort() (failed assertions, std::terminate) and uncaught C++ exceptions print what happened and the stack to stderr
// (Logs/Console.txt) before the process ends; they never reach the CPU exception handler that writes Errors/.
//
// There are no processes to start or wait for.

#include <cerrno>
#include <cstdarg>
#include <cstring>
#include <dirent.h>
#include <string>
#include <strings.h>
#include <sys/stat.h>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <mutex>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern "C" {
ssize_t __real_read(int fd, void* buffer, size_t count);
ssize_t __real_write(int fd, const void* buffer, size_t count);
off_t __real_lseek(int fd, off_t offset, int whence);
void __real_abort(void) __attribute__((noreturn));

// The code segment, from libnx's linker script.
extern char __code_start[];
extern char __rodata_start[];
}

namespace {

const int kLockedDescriptors = 1024;

std::recursive_mutex& DescriptorLock(int fd) {
    static std::recursive_mutex s_locks[kLockedDescriptors];
    static std::recursive_mutex s_shared;
    return fd >= 0 && fd < kLockedDescriptors ? s_locks[fd] : s_shared;
}

// Return addresses along the frame records, as offsets into Wow.elf for aarch64-none-elf-addr2line.
void PrintStack(const void* frame) {
    uintptr_t codeStart = reinterpret_cast<uintptr_t>(__code_start);
    uintptr_t codeEnd = reinterpret_cast<uintptr_t>(__rodata_start);
    uintptr_t fp = reinterpret_cast<uintptr_t>(frame);

    fprintf(stderr, "Stack (Wow.elf offsets):\n");

    for (int depth = 0; fp && !(fp & 0xF) && depth < 48; depth++) {
        const uintptr_t* record = reinterpret_cast<const uintptr_t*>(fp);
        uintptr_t ret = record[1];

        if (ret >= codeStart && ret < codeEnd) {
            fprintf(stderr, "  0x%llx\n", static_cast<unsigned long long>(ret - codeStart));
        }

        if (record[0] <= fp) {
            break;
        }

        fp = record[0];
    }

    fflush(stderr);
}

void Terminate() {
    wowdecomp_switch_console_sync();
    fprintf(stderr, "std::terminate: ");

    if (std::exception_ptr exception = std::current_exception()) {
        try {
            std::rethrow_exception(exception);
        } catch (const std::exception& error) {
            fprintf(stderr, "uncaught exception: %s\n", error.what());
        } catch (...) {
            fprintf(stderr, "uncaught exception of an unknown type\n");
        }
    } else {
        fprintf(stderr, "called without an exception\n");
    }

    PrintStack(__builtin_frame_address(0));
    __real_abort();
}

const bool s_terminateInstalled = (std::set_terminate(Terminate), true);

}

extern "C" {

ssize_t __wrap_read(int fd, void* buffer, size_t count) {
    std::lock_guard<std::recursive_mutex> lock(DescriptorLock(fd));
    return __real_read(fd, buffer, count);
}

ssize_t __wrap_write(int fd, const void* buffer, size_t count) {
    std::lock_guard<std::recursive_mutex> lock(DescriptorLock(fd));
    return __real_write(fd, buffer, count);
}

off_t __wrap_lseek(int fd, off_t offset, int whence) {
    std::lock_guard<std::recursive_mutex> lock(DescriptorLock(fd));
    return __real_lseek(fd, offset, whence);
}

void __wrap_abort(void) {
    wowdecomp_switch_console_sync();
    fprintf(stderr, "abort() called\n");
    PrintStack(__builtin_frame_address(0));
    __real_abort();
}

ssize_t pread(int fd, void* buffer, size_t count, off_t offset) {
    std::lock_guard<std::recursive_mutex> lock(DescriptorLock(fd));
    off_t position = __real_lseek(fd, 0, SEEK_CUR);

    if (position < 0 || __real_lseek(fd, offset, SEEK_SET) < 0) {
        return -1;
    }

    ssize_t result = __real_read(fd, buffer, count);
    int error = errno;
    __real_lseek(fd, position, SEEK_SET);
    errno = error;

    return result;
}

ssize_t pwrite(int fd, const void* buffer, size_t count, off_t offset) {
    std::lock_guard<std::recursive_mutex> lock(DescriptorLock(fd));
    off_t position = __real_lseek(fd, 0, SEEK_CUR);

    if (position < 0 || __real_lseek(fd, offset, SEEK_SET) < 0) {
        return -1;
    }

    ssize_t result = __real_write(fd, buffer, count);
    int error = errno;
    __real_lseek(fd, position, SEEK_SET);
    errno = error;

    return result;
}

int getpagesize(void) {
    return 0x1000;
}

long sysconf(int name) {
    switch (name) {
        case _SC_PAGESIZE:
            return 0x1000;
        case _SC_NPROCESSORS_CONF:
        case _SC_NPROCESSORS_ONLN:
            // The three cores applications get.
            return 3;
        default:
            errno = EINVAL;
            return -1;
    }
}

int execl(const char* path, const char* arg, ...) {
    errno = ENOSYS;
    return -1;
}

pid_t waitpid(pid_t pid, int* status, int options) {
    errno = ECHILD;
    return -1;
}

}

// Paths without regard to case. The Switch's SD card file system matches names case-sensitively; Windows and macOS don't,
// and the game and its add-ons rely on that (an add-on loading Libs\LibStub\LibStub.lua from a folder named libs, say).
// A path that doesn't exist as written is matched one component at a time, ignoring case; components with no match stay
// as written, for files and folders being created. Directory listings are kept a couple of seconds, as the game probes
// for files that don't exist (saved variables, Bindings.xml) many times over.
namespace {

const uint64_t kListingLifeMs = 2000;

struct Listing {
    uint64_t readMs;
    std::vector<std::string> names;
};

std::mutex s_caseLock;
std::unordered_map<std::string, Listing> s_listings;

uint64_t NowMs() {
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW) / 1000000;
}

// Called with s_caseLock held.
bool FindName(const std::string& directory, const std::string& name, std::string* match) {
    uint64_t now = NowMs();

    for (int attempt = 0; attempt < 2; attempt++) {
        Listing& listing = s_listings[directory];

        if (attempt == 1 || !listing.readMs || now - listing.readMs > kListingLifeMs) {
            if (attempt == 1 && listing.readMs == now) {
                break;
            }

            listing.names.clear();
            listing.readMs = now;

            if (DIR* handle = opendir(directory.c_str())) {
                while (dirent* entry = readdir(handle)) {
                    listing.names.emplace_back(entry->d_name);
                }

                closedir(handle);
            }
        }

        for (const std::string& candidate : listing.names) {
            if (!strcasecmp(candidate.c_str(), name.c_str())) {
                *match = candidate;
                return true;
            }
        }
    }

    return false;
}

}

extern "C" int wowdecomp_switch_resolve_case(const char* path, char* resolved, size_t size) {
    struct stat info;

    if (!path || !*path || stat(path, &info) == 0) {
        return 0;
    }

    std::string result;
    std::string current;
    const char* cursor = path;

    if (*cursor == '/') {
        result = "/";
        cursor++;
    }

    bool missing = false;
    bool changed = false;
    std::lock_guard<std::mutex> lock(s_caseLock);

    while (*cursor) {
        const char* slash = strchr(cursor, '/');
        std::string part = slash ? std::string(cursor, slash - cursor) : std::string(cursor);
        cursor = slash ? slash + 1 : cursor + part.size();

        if (part.empty()) {
            continue;
        }

        std::string directory = result.empty() ? "." : result;
        std::string candidate = result.empty() || result.back() == '/' ? result + part : result + "/" + part;

        if (!missing && part != "." && part != ".." && stat(candidate.c_str(), &info) != 0) {
            std::string match;

            if (FindName(directory, part, &match)) {
                candidate = result.empty() || result.back() == '/' ? result + match : result + "/" + match;
                changed = true;
            } else {
                missing = true;
            }
        }

        result = candidate;
    }

    if (!changed || result.size() + 1 > size) {
        return 0;
    }

    if (path[strlen(path) - 1] == '/') {
        result += '/';
    }

    memcpy(resolved, result.c_str(), result.size() + 1);
    return 1;
}
