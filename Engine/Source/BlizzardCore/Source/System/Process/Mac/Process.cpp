// macOS implementation of Blizzard::Process (Win64/Process.cpp).

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mach-o/dyld.h>
#include <pthread.h>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include "Engine/Source/BlizzardCore/Source/System/Memory/TempBuffer.h"
#include "Engine/Source/BlizzardCore/Source/System/Process/Process.h"
#include "Engine/Source/BlizzardCore/Source/System/String/Path.h"
#include "Engine/Source/BlizzardCore/Source/System/String/String.h"

extern char** environ;

namespace {

struct ChildProcess {
    pid_t m_process;
    void (*m_callback)(void*);
    void* m_param;
};

void* WaitForChild(void* param) {
    ChildProcess* child = static_cast<ChildProcess*>(param);
    int status;

    while (waitpid(child->m_process, &status, 0) < 0 && errno == EINTR) {
    }

    // Windows polls children from a UI timer and runs the callback on the main thread; here it runs on this watcher
    // thread.
    child->m_callback(child->m_param);
    delete child;

    return nullptr;
}

}

bool Blizzard::Process::GetProcessPath(char* buffer, int32_t buffersize) {
    if (!buffer || buffersize <= 0) {
        return false;
    }

    char path[PATH_MAX];
    uint32_t length = sizeof(path);

    if (_NSGetExecutablePath(path, &length) != 0) {
        *buffer = '\0';
        return true;
    }

    char resolved[PATH_MAX];
    Blizzard::String::Copy(buffer, realpath(path, resolved) ? resolved : path, buffersize);

    return true;
}

bool Blizzard::Process::GetProcessDirectory(char* buffer, int32_t buffersize) {
    Blizzard::Memory::TempBuffer<char, 1024> path(buffersize);

    if (!GetProcessPath(path.m_data, buffersize)) {
        return false;
    }

    if (path.m_data) {
        // In a bundle (Wow.app/Contents/MacOS/Wow) the game folder is the one holding the .app, where Data/ lives.
        char* bundle = strstr(path.m_data, ".app/Contents/MacOS/");

        if (bundle) {
            bundle[4] = '\0';
        }

        if (buffer) {
            int32_t length = static_cast<int32_t>(Blizzard::String::FindFilename(path.m_data) - path.m_data + 1);
            Blizzard::String::Copy(buffer, path.m_data, std::min(length, buffersize));
        }
    } else if (buffer && buffersize > 0) {
        *buffer = '\0';
    }

    return true;
}

bool Blizzard::Process::LaunchChildProcessWithArguments(const char* path, const char* arguments, void (*callback)(void*), void* param) {
    if (!arguments) {
        arguments = "";
    }

    // Arguments are separated by newlines, as on Windows; each becomes one argv entry.
    std::vector<std::string> parts;
    parts.emplace_back(path);

    while (*arguments) {
        size_t length = strcspn(arguments, "\n");
        parts.emplace_back(arguments, length);
        arguments += length;
        arguments += strspn(arguments, "\n");
    }

    std::vector<char*> argv;

    for (std::string& part : parts) {
        argv.push_back(&part[0]);
    }

    argv.push_back(nullptr);

    pid_t process;

    if (posix_spawn(&process, path, nullptr, nullptr, argv.data(), environ) != 0) {
        return false;
    }

    if (!callback) {
        return true;
    }

    ChildProcess* child = new ChildProcess;
    child->m_process = process;
    child->m_callback = callback;
    child->m_param = param;

    pthread_t watcher;

    if (pthread_create(&watcher, nullptr, WaitForChild, child) != 0) {
        delete child;
        return false;
    }

    pthread_detach(watcher);

    return true;
}

void Blizzard::Process::Sleep(uint32_t milliseconds) {
    usleep(static_cast<useconds_t>(milliseconds) * 1000);
}
