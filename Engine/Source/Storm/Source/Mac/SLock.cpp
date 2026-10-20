// macOS implementation of the Storm locks and sync objects (Win64/SLock.cpp).
//
// SSyncObject handles are MacWaitable objects, so SEvent / SMutex / SSemaphore / SThread keep their Win32 semantics,
// WaitMultiplePtr included. CSRWLock replaces Storm's event-based reader/writer lock with a pthread_rwlock_t, allocated
// in the constructor; its pointer lives in the lock's opaque bytes.

#include <cstdint>
#include <cstring>
#include <new>
#include <pthread.h>
#include "Engine/Source/BlizzardCore/Source/System/Lock/Mac/Waitable.h"
#include "Engine/Source/Storm/Source/SLock.h"
#include "Engine/Source/Storm/Source/SThread.h"
#include "Engine/Source/Storm/H/storm.h"

static_assert(sizeof(CSRWLock::m_opaqueData) >= sizeof(pthread_rwlock_t*), "CSRWLock must hold a pointer");

namespace {

pthread_rwlock_t* RWLock(const CSRWLock* lock) {
    pthread_rwlock_t* rwlock;
    memcpy(&rwlock, const_cast<SRWLock::SURWLOCK*>(&lock->m_opaqueData), sizeof(rwlock));
    return rwlock;
}

}

SCritSect::SCritSect() {
    InitializeCriticalSection(&this->m_critsect);
}

SCritSect::~SCritSect() {
    DeleteCriticalSection(&this->m_critsect);
}

void SCritSect::Enter() {
    EnterCriticalSection(&this->m_critsect);
}

void SCritSect::Leave() {
    LeaveCriticalSection(&this->m_critsect);
}

SSyncObject::SSyncObject() {
    this->m_opaqueData = nullptr;
}

SSyncObject::~SSyncObject() {
    this->Close();
}

void SSyncObject::Close() {
    if (this->m_opaqueData) {
        MacWaitable::Close(this->m_opaqueData);
        this->m_opaqueData = nullptr;
    }
}

int32_t SSyncObject::Valid() {
    return this->m_opaqueData != nullptr;
}

uint32_t SSyncObject::Wait(uint32_t timeout) {
    return MacWaitable::Wait(this->m_opaqueData, timeout);
}

uint32_t STORMAPI SSyncObject::WaitMultiplePtr(uint32_t count, SSyncObject** objects, int32_t waitAll, uint32_t timeout) {
    void* handles[MacWaitable::kMaxWaitObjects];

    if (count > MacWaitable::kMaxWaitObjects) {
        return 0xFFFFFFFF;
    }

    uint32_t handleCount = 0;

    for (uint32_t i = 0; i < count; i++) {
        if (objects[i] && objects[i]->m_opaqueData) {
            handles[handleCount++] = objects[i]->m_opaqueData;
        }
    }

    return MacWaitable::WaitMultiple(handleCount, handles, waitAll != 0, timeout);
}

SEvent::SEvent(int32_t manualReset, int32_t initialState) {
    this->m_opaqueData = MacWaitable::CreateEvent(manualReset != 0, initialState != 0);

    if (!this->m_opaqueData) {
        DebugLog("SEvent::SEvent: CreateEvent failed");
    }
}

int32_t SEvent::Set() {
    return MacWaitable::SetEvent(this->m_opaqueData);
}

int32_t SEvent::Reset() {
    return MacWaitable::ResetEvent(this->m_opaqueData);
}

SMutex::SMutex() {
    this->Create(0, nullptr);
}

void SMutex::Create(int32_t initialOwner, const char* name) {
    // Named mutexes are only used on Windows to detect other running clients; macOS gets an unnamed one.
    if (this->m_opaqueData) {
        MacWaitable::Close(this->m_opaqueData);
    }

    this->m_opaqueData = MacWaitable::CreateMutex(initialOwner != 0);

    if (!this->m_opaqueData) {
        DebugLog("SMutex::Create: CreateMutex failed");
    }
}

int32_t SMutex::Release() {
    return MacWaitable::ReleaseMutex(this->m_opaqueData);
}

SSemaphore::SSemaphore(int32_t initialCount, int32_t maximumCount) {
    this->m_opaqueData = MacWaitable::CreateSemaphore(initialCount, maximumCount);
}

int32_t SSemaphore::Signal(uint32_t count) {
    return MacWaitable::ReleaseSemaphore(this->m_opaqueData, static_cast<int32_t>(count), nullptr);
}

int32_t SThread::Create(uint32_t (STORMAPI* threadProc)(void*), void* param, SThread& thread, char* threadName, uint32_t stackSize) {
    uint32_t threadId;
    thread.m_opaqueData = SCreateThread(threadProc, param, &threadId, nullptr, nullptr, stackSize);
    return thread.m_opaqueData != nullptr;
}

int32_t SThread::SetPriority(int32_t priority) {
    // 0 (lowest) .. 5 (time critical), as Win64/SLock.cpp maps them onto THREAD_PRIORITY_*.
    if (priority < 0 || priority >= 6) {
        return 0;
    }

    pthread_t native;

    if (!MacWaitable::GetThread(this->m_opaqueData, &native)) {
        return 0;
    }

#if defined(__SWITCH__)
    // newlib on the Switch has no POSIX scheduling calls; threads keep the priority they were created with.
    return 1;
#else
    int32_t minimum = sched_get_priority_min(SCHED_OTHER);
    int32_t maximum = sched_get_priority_max(SCHED_OTHER);
    int32_t normal = (minimum + maximum) / 2;
    int32_t step = (maximum - minimum) / 6;

    sched_param param;
    param.sched_priority = normal + (priority - 2) * step;

    return pthread_setschedparam(native, SCHED_OTHER, &param) == 0;
#endif
}

CSRWLock::CSRWLock() {
    memset(&this->m_opaqueData, 0, sizeof(this->m_opaqueData));

    pthread_rwlock_t* rwlock = new pthread_rwlock_t;
    pthread_rwlock_init(rwlock, nullptr);
    memcpy(&this->m_opaqueData, &rwlock, sizeof(rwlock));
}

CSRWLock::~CSRWLock() {
    pthread_rwlock_t* rwlock = RWLock(this);

    if (rwlock) {
        pthread_rwlock_destroy(rwlock);
        delete rwlock;
    }

    memset(&this->m_opaqueData, 0, sizeof(this->m_opaqueData));
}

void CSRWLock::Enter(int32_t forwriting) {
    if (forwriting) {
        pthread_rwlock_wrlock(RWLock(this));
    } else {
        pthread_rwlock_rdlock(RWLock(this));
    }
}

void CSRWLock::Leave(int32_t fromwriting) {
    pthread_rwlock_unlock(RWLock(this));
}
