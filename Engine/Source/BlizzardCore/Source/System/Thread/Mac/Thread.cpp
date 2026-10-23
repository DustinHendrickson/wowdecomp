// macOS implementation of Blizzard::Thread (Win64/Thread.cpp, ThreadAPI.cpp, ThreadLocalStorage.cpp, ThreadPools.cpp).
//
// Thread handles are MacWaitable thread objects, signaled when the thread finishes, so waits behave like waiting on a
// Win32 thread handle. TLS slots are pthread keys; TLSSlot::m_key keeps the inverted key, as Windows keeps the inverted
// TLS index, so 0 still means "not allocated". Threads the engine did not create (the main thread, OS threads) get
// their registered destructors from a pthread key destructor instead of the Windows watcher thread.

#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#if defined(__APPLE__)
#include <execinfo.h>
#endif
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include "Engine/Source/BlizzardCore/Source/System/Lock/Lock.h"
#include "Engine/Source/BlizzardCore/Source/System/Lock/Mac/Waitable.h"
#include "Engine/Source/BlizzardCore/Source/System/Thread/Mac/SignalStack.h"
#include "Engine/Source/BlizzardCore/Source/System/String/String.h"
#include "Engine/Source/BlizzardCore/Source/System/Thread/Thread.h"
#include "Engine/Source/BlizzardCore/Source/System/Thread/ThreadLocalStorage.h"

bool System_Thread::s_initialized;
Blizzard::Lock::Mutex System_Thread::s_mutex;
Blizzard::Lock::Mutex System_Thread::s_registryMutex;
Blizzard::Thread::TLSSlot System_Thread::s_threadRecordTLS;
Blizzard::Thread::TLSSlot System_Thread::s_stackTraceEntryPointTLS;
Blizzard::Thread::ThreadRecord* System_Thread::s_mainThread;
std::set<Blizzard::Thread::ThreadRecord*, std::less<Blizzard::Thread::ThreadRecord*>, Blizzard::Memory::AllocSTL_raw<Blizzard::Thread::ThreadRecord*>>* System_Thread::s_ThreadRegistry;
int32_t System_Thread::s_DefaultStackSize;
System_Thread::ExternalTLSRecord System_Thread::s_externalTLS[32];
int32_t System_Thread::s_externalTLSCount;

namespace {

// Secondary pthreads default to 512 KB on macOS; Windows gives the client's threads 1 MB.
const size_t kDefaultStackSize = 1024 * 1024;

pthread_key_t s_externalExitKey;
pthread_once_t s_externalExitOnce = PTHREAD_ONCE_INIT;

pthread_key_t NativeKey(const Blizzard::Thread::TLSSlot* slot) {
    return static_cast<pthread_key_t>(~slot->m_key);
}

void* ExternalThreadTag(uint32_t threadId) {
    return reinterpret_cast<void*>(static_cast<uintptr_t>(threadId));
}

void ExternalThreadExit(void* tag) {
    System_Thread::RunExternalTLSDestructors(tag);
}

void CreateExternalExitKey() {
    pthread_key_create(&s_externalExitKey, ExternalThreadExit);
}

// pthread entry for Blizzard::Thread::Create. ThreadStarter can free the record (Thread::Close), so the handle is
// taken first; this function holds its own reference until the exit is signaled.
void* PosixThreadEntry(void* param) {
    MacThreadSignalStack signalStack;
#if defined(__SWITCH__)
    wowdecomp_switch_thread_start(WOWDECOMP_THREAD_OTHER);
#endif
    Blizzard::Thread::ThreadRecord* thread = static_cast<Blizzard::Thread::ThreadRecord*>(param);
    void* handle = thread->m_handle;

    System_Thread::ThreadStarter(thread);

    MacWaitable::SignalThreadExit(handle);
    MacWaitable::Close(handle);

    return nullptr;
}

#if defined(__APPLE__)
// Blizzard levels 0 (idle) .. 6 (time critical) onto the SCHED_OTHER range, level 3 being the default priority.
int32_t NativePriority(int32_t level) {
    int32_t minimum = sched_get_priority_min(SCHED_OTHER);
    int32_t maximum = sched_get_priority_max(SCHED_OTHER);
    int32_t normal = (minimum + maximum) / 2;
    int32_t step = (maximum - minimum) / 6;

    return normal + (level - 3) * step;
}
#endif

}

bool Blizzard::Thread::Sleep(uint32_t milliseconds) {
    usleep(static_cast<useconds_t>(milliseconds) * 1000);
    return true;
}

bool Blizzard::Thread::IsRunning(ThreadRecord* thread) {
    return thread->m_state >= 0;
}

bool System_Thread::SetPriorityLevel(Blizzard::Thread::ThreadRecord* thread, int32_t level) {
    if (level < 0 || level > 6) {
        return false;
    }

    pthread_t native;

    if (!MacWaitable::GetThread(thread->m_handle, &native)) {
        return false;
    }

#if defined(__SWITCH__) || defined(__ANDROID__)
    // newlib on the Switch has no POSIX scheduling calls, and Android's SCHED_OTHER has a single priority: threads keep
    // the priority they were created with.
    return true;
#else
    sched_param param;
    param.sched_priority = NativePriority(level);

    return pthread_setschedparam(native, SCHED_OTHER, &param) == 0;
#endif
}

bool System_Thread::GetPriorityLevel(Blizzard::Thread::ThreadRecord* thread, int32_t* level) {
    // The Windows version falls through every case and reports failure; callers only keep the value.
    *level = 3;

    return false;
}

void System_Thread::ShutdownThreadLocalStorage() {
    if (!s_initialized) {
        return;
    }

    // Threads still alive at shutdown never reach their key destructor; run what they registered now.
    Blizzard::Lock::ScopedMutex lock(s_mutex);

    while (s_externalTLSCount > 0) {
        ExternalTLSRecord& record = s_externalTLS[--s_externalTLSCount];

        if (record.m_destructor) {
            record.m_destructor(record.m_value);
        }
    }
}

void System_Thread::RunExternalTLSDestructors(HANDLE thread) {
    Blizzard::Lock::ScopedMutex lock(s_mutex);

    for (int32_t i = s_externalTLSCount; i != 0;) {
        i--;
        ExternalTLSRecord& record = s_externalTLS[i];

        if (record.m_thread == thread) {
            if (record.m_destructor) {
                record.m_destructor(record.m_value);
            }

            s_externalTLSCount--;
            record = s_externalTLS[s_externalTLSCount];
        }
    }
}

void System_Thread::AddToRegistry(Blizzard::Thread::ThreadRecord* thread) {
    Blizzard::Lock::ScopedMutex lock(s_registryMutex);

    if (!s_ThreadRegistry) {
        s_ThreadRegistry = new std::set<Blizzard::Thread::ThreadRecord*, std::less<Blizzard::Thread::ThreadRecord*>, Blizzard::Memory::AllocSTL_raw<Blizzard::Thread::ThreadRecord*>>();
    }

    s_ThreadRegistry->insert(thread);
}

Blizzard::Thread::ThreadRecord* System_Thread::NewThread(uint32_t (*function)(void*), void* param, const char* name) {
    if (!name) {
        name = "";
    }

    uint32_t length = static_cast<uint32_t>(strlen(name));
    Blizzard::Thread::ThreadRecord* thread = static_cast<Blizzard::Thread::ThreadRecord*>(malloc(sizeof(Blizzard::Thread::ThreadRecord) + length + 1));
    memset(thread, 0, sizeof(Blizzard::Thread::ThreadRecord));
    thread->m_param = param;
    thread->m_function = function;
    thread->m_state = 0;
    thread->m_refCount.m_value = 2;
    Blizzard::String::Copy(thread->m_name, name, length + 1);
    AddToRegistry(thread);

    return thread;
}

void System_Thread::RemoveFromRegistry(Blizzard::Thread::ThreadRecord* thread) {
    Blizzard::Lock::ScopedMutex lock(s_registryMutex);
    s_ThreadRegistry->erase(thread);

    if (s_ThreadRegistry->empty()) {
        delete s_ThreadRegistry;
        s_ThreadRegistry = nullptr;
    }
}

void System_Thread::InitMainThread() {
    s_mainThread = NewThread(nullptr, nullptr, nullptr);
    InternalSetLocalStorage(&s_threadRecordTLS, s_mainThread);
    s_mainThread->m_state = 1;
    s_mainThread->m_refCount.m_value = 1;
}

void System_Thread::InitThreadSystem() {
    if (s_initialized) {
        return;
    }

    s_initialized = true;
    Blizzard::Lock::MutexCreate(s_mutex);
    Blizzard::Lock::MutexCreate(s_registryMutex);
    AllocateLocalStorage(&s_threadRecordTLS, nullptr);
    AllocateLocalStorage(&s_stackTraceEntryPointTLS, nullptr);
    InitMainThread();
}

void System_Thread::SetStackTraceEntryPoint(void* entryPoint) {
    InitThreadSystem();
    InternalSetLocalStorage(&s_stackTraceEntryPointTLS, entryPoint);
}

Blizzard::Thread::ThreadRecord* System_Thread::GetThreadRecord() {
    InitThreadSystem();

    return static_cast<Blizzard::Thread::ThreadRecord*>(pthread_getspecific(NativeKey(&s_threadRecordTLS)));
}

bool System_Thread::AllocateTLSSlot(Blizzard::Thread::TLSSlot* slot, Blizzard::Thread::TLSDestructor destructor) {
    InitThreadSystem();
    Blizzard::Lock::ScopedMutex lock(s_mutex);

    return slot->m_key || AllocateLocalStorage(slot, destructor);
}

void System_Thread::InitStackTraceEntryPoint() {
    // Same frame as RtlCaptureStackBackTrace(2, 1, ...): skip this function and ThreadStarter.
#if defined(__SWITCH__) || defined(__ANDROID__)
    // No backtrace() (bionic has it from API 33 only); the frame records hold the same return address.
    SetStackTraceEntryPoint(__builtin_return_address(1));
#else
    void* frames[3];

    if (backtrace(frames, 3) == 3) {
        SetStackTraceEntryPoint(frames[2]);
    }
#endif
}

uint32_t System_Thread::ExternalTLSWatcher(void* param) {
    // Windows-only mechanism; external threads run their destructors from the exit key on macOS.
    return 0;
}

uint32_t __stdcall System_Thread::ThreadStarter(void* param) {
    Blizzard::Thread::ThreadRecord* thread = static_cast<Blizzard::Thread::ThreadRecord*>(param);

    InternalSetLocalStorage(&s_threadRecordTLS, thread);
    InitStackTraceEntryPoint();

    if (thread->m_name[0]) {
        pthread_setname_np(thread->m_name);
    }

    while (!thread->m_state) {
        Blizzard::Thread::Sleep(1);
    }

    uint32_t result = thread->m_function(thread->m_param);
    thread->m_result = result;
    RunTLSDestructors();
    thread->m_state = -1;
    Blizzard::Thread::Close(thread);

    return result;
}

bool Blizzard::Thread::Create(uint32_t (*function)(void*), void* param, ThreadRecord** thread, const char* name, int32_t flags, int32_t stackSize) {
    System_Thread::InitThreadSystem();
    ThreadRecord* record = System_Thread::NewThread(function, param, name);
    *thread = record;

    // Flag 1 creates the thread suspended on Windows. Here the thread starts but waits in ThreadStarter until m_state
    // is set, which is what resuming it does.
    if (!stackSize) {
        stackSize = System_Thread::s_DefaultStackSize;
    }

    size_t size = stackSize > 0 ? static_cast<size_t>(stackSize) : kDefaultStackSize;
    size_t page = static_cast<size_t>(getpagesize());
    size = (size + page - 1) / page * page;

    // Windows only commits the requested size and reserves at least the executable's 1 MB default; a pthread stack is
    // exactly the size given.
    if (size < kDefaultStackSize) {
        size = kDefaultStackSize;
    }

    void* handle = MacWaitable::CreateThreadObject();
    record->m_handle = handle;

    // One reference for the record, one for the running thread.
    MacWaitable::Retain(handle);

    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&attributes, size);

    pthread_t native;
    int32_t created = pthread_create(&native, &attributes, PosixThreadEntry, record);
    pthread_attr_destroy(&attributes);

    if (created != 0) {
        MacWaitable::Close(handle);
        MacWaitable::Close(handle);
        record->m_handle = nullptr;
        System_Thread::RemoveFromRegistry(record);
        free(record);
        *thread = nullptr;
        return false;
    }

    record->m_threadId = static_cast<uint32_t>(pthread_mach_thread_np(native));
    MacWaitable::SetThread(handle, native, record->m_threadId);
    (*thread)->m_state = (flags & 1) == 0;

    return true;
}

void System_Thread::RegisterExternalTLSDestructor(void* value, Blizzard::Thread::TLSDestructor destructor) {
    uint32_t threadId = MacWaitable::CurrentThreadId();
    Blizzard::Lock::ScopedMutex lock(s_mutex);

    if (s_externalTLSCount >= 32) {
        return;
    }

    pthread_once(&s_externalExitOnce, CreateExternalExitKey);

    ExternalTLSRecord& record = s_externalTLS[s_externalTLSCount++];
    record.m_thread = ExternalThreadTag(threadId);
    record.m_event = nullptr;
    record.m_watcher = nullptr;
    record.m_threadId = threadId;
    record.m_value = value;
    record.m_destructor = destructor;

    // The key destructor runs this thread's records when it exits.
    pthread_setspecific(s_externalExitKey, ExternalThreadTag(threadId));
}

void System_Thread::InternalSetLocalStorage(const Blizzard::Thread::TLSSlot* slot, const void* value) {
    pthread_setspecific(NativeKey(slot), value);

    if (slot->m_destructor && value) {
        Blizzard::Thread::ThreadRecord* thread = GetThreadRecord();

        if (thread && thread->m_worker) {
            thread = thread->m_worker->m_task;
        }

        if (!thread) {
            RegisterExternalTLSDestructor(const_cast<void*>(value), slot->m_destructor);
        }
    }
}

// Win64/ThreadAPI.cpp

bool Blizzard::Thread::Close(ThreadRecord* thread) {
    if (thread->m_refCount.Decrement() > 0) {
        return true;
    }

    if (thread->m_handle) {
        MacWaitable::Close(thread->m_handle);
        thread->m_handle = nullptr;
    }

    System_Thread::RemoveFromRegistry(thread);
    free(thread);

    return true;
}

bool Blizzard::Thread::WaitForever(ThreadRecord* thread) {
    if (thread->m_state < 0) {
        return true;
    }

    System_Thread::ThreadWorker* worker = thread->m_worker;

    if (worker && thread != worker->m_thread) {
        if (thread == worker->m_task) {
            Blizzard::Lock::EventWaitForever(worker->m_readyEvent.m_event);
            Blizzard::Lock::EventSet(worker->m_readyEvent.m_event);
        }

        return true;
    }

    return MacWaitable::Wait(thread->m_handle, MacWaitable::kInfinite) == MacWaitable::kWaitObject0;
}

// Win64/ThreadLocalStorage.cpp

bool System_Thread::AllocateLocalStorage(Blizzard::Thread::TLSSlot* slot, Blizzard::Thread::TLSDestructor destructor) {
    InitThreadSystem();

    pthread_key_t key;

    if (pthread_key_create(&key, nullptr) != 0) {
        return false;
    }

    slot->m_key = ~static_cast<uint32_t>(key);
    slot->m_destructor = destructor;
    Blizzard::Lock::MutexEnter(s_mutex);
    s_slotList[s_slotListUsed++] = slot;
    Blizzard::Lock::MutexLeave(s_mutex);

    return true;
}

void System_Thread::RunTLSDestructors() {
    if (!s_initialized) {
        return;
    }

    Blizzard::Lock::ScopedMutex lock(s_mutex);

    for (uint32_t i = 0; i < s_slotListUsed; i++) {
        Blizzard::Thread::TLSSlot* slot = s_slotList[i];
        Blizzard::Thread::TLSDestructor destructor = slot->m_destructor;

        if (destructor) {
            void* value = pthread_getspecific(NativeKey(slot));

            if (value) {
                destructor(value);
            }
        }
    }
}

void* Blizzard::Thread::RegisterLocalStorage(TLSSlot* slot, void* (*constructor)(void*), void* param, TLSDestructor destructor) {
    void* value;

    if (!slot->m_key) {
        if (!System_Thread::AllocateTLSSlot(slot, destructor)) {
            return nullptr;
        }
    } else {
        value = pthread_getspecific(NativeKey(slot));

        if (value) {
            return value;
        }
    }

    value = constructor(param);
    System_Thread::InternalSetLocalStorage(slot, value);

    return value;
}

// Win64/ThreadPools.cpp. macOS has no thread affinity API, so workers keep no affinity mask.

System_Thread::ThreadWorker::~ThreadWorker() {
    Blizzard::Lock::EventSet(this->m_wakeEvent.m_event);
    Blizzard::Lock::EventWaitForever(this->m_readyEvent.m_event);

    if (this->m_thread) {
        Blizzard::Lock::EventSet(this->m_wakeEvent.m_event);
        MacWaitable::Wait(this->m_thread->m_handle, MacWaitable::kInfinite);
        Blizzard::Thread::Close(this->m_thread);
        this->m_thread = nullptr;
    }
}

uint32_t System_Thread::ThreadWorker::Executive(void* param) {
    ThreadWorker* worker = static_cast<ThreadWorker*>(param);

    Blizzard::Lock::EventSet(worker->m_readyEvent.m_event);
    Blizzard::Lock::EventWaitForever(worker->m_wakeEvent.m_event);

    while (worker->m_taskFunction) {
        worker->m_task->m_state = 1;
        worker->m_task->m_result = worker->m_taskFunction(worker->m_taskParam);

        if (worker->m_thread) {
            SetPriorityLevel(worker->m_thread, worker->m_priority);
        }

        worker->m_task->m_state = -1;
        Blizzard::Thread::Close(worker->m_task);
        worker->m_task = nullptr;
        worker->m_taskFunction = nullptr;
        worker->m_taskParam = nullptr;

        {
            Blizzard::Lock::ScopedMutex lock(worker->m_pool->m_mutex.m_mutex);
            worker->m_pool->m_idle.push_back(worker);
        }

        Blizzard::Lock::EventSet(worker->m_readyEvent.m_event);
        Blizzard::Lock::EventWaitForever(worker->m_wakeEvent.m_event);
    }

    Blizzard::Lock::EventSet(worker->m_readyEvent.m_event);

    return 0;
}

bool System_Thread::ThreadWorker::Start(Blizzard::Thread::ThreadPoolRecord* pool, int32_t flags, int32_t stackSize) {
    if (!Blizzard::Thread::Create(Executive, this, &this->m_thread, "Blizzard Thread Pool Worker", flags, stackSize)) {
        return false;
    }

    if (this->m_thread) {
        GetPriorityLevel(this->m_thread, &this->m_priority);
    }

    this->m_affinity = 0;
    this->m_pool = pool;

    return true;
}
