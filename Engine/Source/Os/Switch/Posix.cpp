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
