// POSIX calls the shared Mac/ layers make that newlib on the Switch doesn't provide.
//
// pread and pwrite seek, transfer and seek back under one lock. That keeps positioned transfers from different threads
// apart, which is how the game uses them (each file's stream position belongs to one thread at a time). There are no
// processes to start or wait for.

#include <cerrno>
#include <cstdarg>
#include <mutex>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

std::mutex s_positionedLock;

}

extern "C" {

ssize_t pread(int fd, void* buffer, size_t count, off_t offset) {
    std::lock_guard<std::mutex> lock(s_positionedLock);
    off_t position = lseek(fd, 0, SEEK_CUR);

    if (position < 0 || lseek(fd, offset, SEEK_SET) < 0) {
        return -1;
    }

    ssize_t result = read(fd, buffer, count);
    int error = errno;
    lseek(fd, position, SEEK_SET);
    errno = error;

    return result;
}

ssize_t pwrite(int fd, const void* buffer, size_t count, off_t offset) {
    std::lock_guard<std::mutex> lock(s_positionedLock);
    off_t position = lseek(fd, 0, SEEK_CUR);

    if (position < 0 || lseek(fd, offset, SEEK_SET) < 0) {
        return -1;
    }

    ssize_t result = write(fd, buffer, count);
    int error = errno;
    lseek(fd, position, SEEK_SET);
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
