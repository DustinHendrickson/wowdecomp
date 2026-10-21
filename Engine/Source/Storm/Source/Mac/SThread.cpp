// macOS implementation of Storm threads and processes (Win64/SThread.cpp).
//
// Thread handles are MacWaitable thread objects, signaled when the thread function returns, so SSyncObject waits work on
// them. Thread ids are Mach port numbers (32-bit, like Windows thread ids). The thread-tracking table and CInitCritSect
// behave as on Windows.

#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstring>
#include <new>
#include <pthread.h>
#include <sys/wait.h>
#include <unistd.h>
#include "Engine/Source/BlizzardCore/Source/System/Lock/Mac/Waitable.h"
#include "Engine/Source/BlizzardCore/Source/System/Thread/Mac/SignalStack.h"
#include "Engine/Source/Storm/Source/SThread.h"
#include "Engine/Source/Storm/H/storm.h"

// The smallest stack a thread gets; see SCreateThread.
static const size_t kMinimumStackSize = 1024 * 1024;

namespace {

struct PROCESSCOMPLETION {
    void (*callback)(void*);
    void* data;
    pid_t process;
};

struct THREADLAUNCH {
    uint32_t (STORMAPI* threadProc)(void*);
    void* param;
    void* threadH;
};

// Thread priorities as Windows reports them (THREAD_PRIORITY_NORMAL); macOS threads stay at the default.
const int32_t kThreadPriorityNormal = 0;

void* PosixLaunch(void* param) {
    MacThreadSignalStack signalStack;
#if defined(__SWITCH__)
    wowdecomp_switch_thread_start(WOWDECOMP_THREAD_OTHER);
#endif
    THREADLAUNCH* launch = static_cast<THREADLAUNCH*>(param);
    void* handle = launch->threadH;

    S_Thread::s_SLaunchThread(launch);

    MacWaitable::SignalThreadExit(handle);
    MacWaitable::Close(handle);

    return nullptr;
}

uint32_t STORMAPI ProcessCompletionCallbackThread(void* vdata) {
    PROCESSCOMPLETION* completion = static_cast<PROCESSCOMPLETION*>(vdata);
    int status;

    while (waitpid(completion->process, &status, 0) < 0 && errno == EINTR) {
    }

    if (completion->callback) {
        completion->callback(completion->data);
    }

    SMemFree(completion, __FILE__, __LINE__, 0);

    return 0;
}

}

namespace S_Thread {
    SThreadTrack s_threads[s_maxthreads];
    int32_t s_numthreads;
    CInitCritSect s_threadCrit;
}

uint32_t STORMAPI SGetCurrentThreadId() {
    return MacWaitable::CurrentThreadId();
}

uint32_t STORMAPI SGetCurrentProcessId() {
    return static_cast<uint32_t>(getpid());
}

int32_t STORMAPI SGetCurrentThreadPriority() {
    return kThreadPriorityNormal;
}

int32_t STORMAPI SSetCurrentThreadPriority(int32_t priority) {
    return 1;
}

int32_t STORMAPI SSetCurrentProcessAffinityMask(size_t mask) {
    // macOS has no affinity masks; report success like a single-group Windows machine would.
    return 1;
}

int32_t CInitCritSect::Enter() {
    int32_t result = 0;

    if (!this->m_critsect) {
        while (InterlockedExchange(&this->m_spinLock, 1)) {
        }

        if (!this->m_critsect) {
            new (this->m_critsectData) CCritSect();
            this->m_critsect = reinterpret_cast<CCritSect*>(this->m_critsectData);
            result = 1;
        }

        this->m_spinLock = 0;
    }

    this->m_critsect->Enter();
    return result;
}

void CInitCritSect::Leave() {
    this->m_critsect->Leave();
}

uint32_t STORMAPI S_Thread::s_SLaunchThread(void* threadParam) {
    s_threadCrit.Enter();

    THREADLAUNCH* launch = static_cast<THREADLAUNCH*>(threadParam);
    uint32_t (STORMAPI* threadProc)(void*) = launch->threadProc;
    void* param = launch->param;
    uint32_t threadId = SGetCurrentThreadId();
    const char* threadName = nullptr;

    for (int32_t i = 0; i < s_numthreads; i++) {
        if (s_threads[i].threadId == threadId) {
            threadName = s_threads[i].name;
            break;
        }
    }

    SMemFree(launch, __FILE__, __LINE__, 0);

    if (threadName && *threadName) {
        pthread_setname_np(threadName);
    }

    s_threadCrit.Leave();

    uint32_t threadVal = threadProc(param);

    s_threadCrit.Enter();

    int32_t numthreads = s_numthreads;

    for (int32_t i = 0; i < numthreads; i++) {
        if (s_threads[i].live && s_threads[i].threadId == threadId) {
            memmove(&s_threads[i], &s_threads[i + 1], (s_maxthreads - 1 - i) * sizeof(SThreadTrack));
            numthreads--;
        }
    }

    s_numthreads = numthreads;
    s_threadCrit.Leave();

    return threadVal;
}

void* SCreateThread(uint32_t dwStackSize, uint32_t (STORMAPI* lpStartAddress)(void*), void* lpParameter, uint32_t dwCreationFlags, uint32_t* lpThreadId, char* threadName) {
    if (!threadName) {
        threadName = const_cast<char*>("");
    }

    S_Thread::s_threadCrit.Enter();

    if (!S_Thread::s_numthreads) {
        S_Thread::SThreadTrack* track = &S_Thread::s_threads[S_Thread::s_numthreads];
        track->threadId = SGetCurrentThreadId();
        track->threadH = nullptr;
        track->live = 1;
        track->suspended = 0;
        SStrCopy(track->name, "main", sizeof(track->name));
        S_Thread::s_numthreads++;
    }

    S_Thread::s_threadCrit.Leave();

    THREADLAUNCH* launch = static_cast<THREADLAUNCH*>(SMemAlloc(sizeof(THREADLAUNCH), __FILE__, __LINE__, SMEM_FLAG_ZEROMEMORY));
    launch->threadProc = lpStartAddress;
    launch->param = lpParameter;

    // One reference for the caller's handle, one for the running thread.
    void* hThread = MacWaitable::CreateThreadObject();
    MacWaitable::Retain(hThread);
    launch->threadH = hThread;

    // Holding s_threadCrit until the track entry exists keeps the new thread from looking up its name too early.
    S_Thread::s_threadCrit.Enter();

    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);

    // On Windows the stack size is only the initial commit; the reservation is never below the executable's 1 MB
    // default. A pthread stack is exactly the size given (512 KB when unset), so callers asking for small stacks (or
    // none) get at least the 1 MB they effectively had.
    size_t page = static_cast<size_t>(getpagesize());
    size_t size = (static_cast<size_t>(dwStackSize) + page - 1) / page * page;
    pthread_attr_setstacksize(&attributes, size < kMinimumStackSize ? kMinimumStackSize : size);

    // CREATE_SUSPENDED isn't supported: no caller in the client asks for it.
    pthread_t native;
    int32_t created = pthread_create(&native, &attributes, PosixLaunch, launch);
    pthread_attr_destroy(&attributes);

    if (created != 0) {
        S_Thread::s_threadCrit.Leave();
        MacWaitable::Close(hThread);
        MacWaitable::Close(hThread);
        SMemFree(launch, __FILE__, __LINE__, 0);
        return nullptr;
    }

    uint32_t threadId = static_cast<uint32_t>(pthread_mach_thread_np(native));
    MacWaitable::SetThread(hThread, native, threadId);

    if (lpThreadId) {
        *lpThreadId = threadId;
    }

    S_Thread::SThreadTrack* track = &S_Thread::s_threads[S_Thread::s_numthreads];
    track->threadId = threadId;
    track->threadH = hThread;
    track->live = 1;
    track->suspended = 0;
    SStrCopy(track->name, threadName, sizeof(track->name));

    S_Thread::s_numthreads++;
    S_Thread::s_threadCrit.Leave();

    return hThread;
}

int32_t STORMAPI SGetThreadName(uint32_t threadId, char* buffer, int32_t bufferchars) {
    bool found = false;

    if (!buffer || bufferchars < 1) {
        return 0;
    }

    if (bufferchars > static_cast<int32_t>(sizeof(S_Thread::s_threads[0].name))) {
        bufferchars = sizeof(S_Thread::s_threads[0].name);
    }

    *buffer = 0;

    S_Thread::s_threadCrit.Enter();

    for (int32_t i = 0; i < S_Thread::s_numthreads; i++) {
        if (S_Thread::s_threads[i].threadId == threadId) {
            SStrCopy(buffer, S_Thread::s_threads[i].name, bufferchars);
            found = true;
            break;
        }
    }

    S_Thread::s_threadCrit.Leave();

    return found;
}

void* STORMAPI SCreateThread(uint32_t (STORMAPI* lpStartAddress)(void*), void* lpParameter, uint32_t* lpThreadId, void* linuxData, char* threadName, uint32_t stackSize) {
    return SCreateThread(stackSize, lpStartAddress, lpParameter, 0, lpThreadId, threadName);
}

int32_t STORMAPI SCreateProcess(const char* appName, char* commandLine, void (*callbackWhenProcessCompletes)(void*), void* callbackData) {
    // CreateProcess with an application name doesn't search PATH and fails when the file isn't there (the client's
    // crash reporter asks for WowError.exe, which has no Mac counterpart).
    if (appName && access(appName, X_OK) != 0) {
        return 0;
    }

    // CreateProcess takes one command line; the shell splits it the way the Windows loader would.
    pid_t process = fork();

    if (process < 0) {
        return 0;
    }

    if (process == 0) {
        if (appName && (!commandLine || !*commandLine)) {
            execl(appName, appName, static_cast<char*>(nullptr));
        } else {
            execl("/bin/sh", "sh", "-c", commandLine ? commandLine : appName, static_cast<char*>(nullptr));
        }

        _exit(127);
    }

    // Always reap the child so it doesn't linger as a zombie; the callback is optional.
    PROCESSCOMPLETION* completion = static_cast<PROCESSCOMPLETION*>(SMemAlloc(sizeof(PROCESSCOMPLETION), __FILE__, __LINE__, 0));
    completion->data = callbackData;
    completion->callback = callbackWhenProcessCompletes;
    completion->process = process;

    uint32_t threadId;
    void* thread = SCreateThread(ProcessCompletionCallbackThread, completion, &threadId, nullptr, nullptr, 0);
    MacWaitable::Close(thread);

    return 1;
}
