#include "Engine/Source/BlizzardCore/Source/System/Lock/Mac/Waitable.h"
#include <atomic>
#include <cerrno>
#include <ctime>
#include <new>

namespace {

enum class Kind {
    Event,
    Mutex,
    Semaphore,
    Thread,
};

struct Object {
    Kind kind;
    std::atomic<int32_t> refs;
    // Event and thread objects.
    bool manualReset;
    bool signaled;
    // Semaphore.
    int32_t count;
    int32_t maximum;
    // Mutex: owner thread id and recursion depth.
    uint32_t owner;
    uint32_t recursion;
    // Thread.
    pthread_t thread;
    uint32_t threadId;
    bool hasThread;
};

pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t s_changed = PTHREAD_COND_INITIALIZER;

Object* NewObject(Kind kind) {
    Object* object = new (std::nothrow) Object();

    if (object) {
        object->kind = kind;
        object->refs.store(1);
    }

    return object;
}

// Caller holds s_lock.
bool IsSignaled(const Object* object, uint32_t self) {
    switch (object->kind) {
        case Kind::Event:
        case Kind::Thread:
            return object->signaled;
        case Kind::Semaphore:
            return object->count > 0;
        case Kind::Mutex:
            return object->recursion == 0 || object->owner == self;
    }

    return false;
}

// Caller holds s_lock and has checked IsSignaled. Applies the side effect a successful wait has on the object.
void Consume(Object* object, uint32_t self) {
    switch (object->kind) {
        case Kind::Event:
            if (!object->manualReset) {
                object->signaled = false;
            }
            break;
        case Kind::Semaphore:
            object->count--;
            break;
        case Kind::Mutex:
            object->owner = self;
            object->recursion++;
            break;
        case Kind::Thread:
            break;
    }
}

timespec Deadline(uint32_t timeout) {
    timespec now;
    clock_gettime(CLOCK_REALTIME, &now);

    timespec deadline;
    deadline.tv_sec = now.tv_sec + timeout / 1000;
    deadline.tv_nsec = now.tv_nsec + static_cast<long>(timeout % 1000) * 1000000;

    if (deadline.tv_nsec >= 1000000000) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000;
    }

    return deadline;
}

}

void* MacWaitable::CreateEvent(bool manualReset, bool initialState) {
    Object* object = NewObject(Kind::Event);

    if (object) {
        object->manualReset = manualReset;
        object->signaled = initialState;
    }

    return object;
}

void* MacWaitable::CreateMutex(bool initialOwner) {
    Object* object = NewObject(Kind::Mutex);

    if (object && initialOwner) {
        object->owner = CurrentThreadId();
        object->recursion = 1;
    }

    return object;
}

void* MacWaitable::CreateSemaphore(int32_t initialCount, int32_t maximumCount) {
    if (maximumCount <= 0 || initialCount < 0 || initialCount > maximumCount) {
        return nullptr;
    }

    Object* object = NewObject(Kind::Semaphore);

    if (object) {
        object->count = initialCount;
        object->maximum = maximumCount;
    }

    return object;
}

void* MacWaitable::CreateThreadObject() {
    return NewObject(Kind::Thread);
}

void MacWaitable::Retain(void* handle) {
    if (handle) {
        static_cast<Object*>(handle)->refs.fetch_add(1);
    }
}

void MacWaitable::Close(void* handle) {
    if (handle && static_cast<Object*>(handle)->refs.fetch_sub(1) == 1) {
        delete static_cast<Object*>(handle);
    }
}

bool MacWaitable::SetEvent(void* handle) {
    Object* object = static_cast<Object*>(handle);

    if (!object || object->kind != Kind::Event) {
        return false;
    }

    pthread_mutex_lock(&s_lock);
    object->signaled = true;
    pthread_cond_broadcast(&s_changed);
    pthread_mutex_unlock(&s_lock);

    return true;
}

bool MacWaitable::ResetEvent(void* handle) {
    Object* object = static_cast<Object*>(handle);

    if (!object || object->kind != Kind::Event) {
        return false;
    }

    pthread_mutex_lock(&s_lock);
    object->signaled = false;
    pthread_mutex_unlock(&s_lock);

    return true;
}

bool MacWaitable::ReleaseMutex(void* handle) {
    Object* object = static_cast<Object*>(handle);

    if (!object || object->kind != Kind::Mutex) {
        return false;
    }

    uint32_t self = CurrentThreadId();
    bool released = false;

    pthread_mutex_lock(&s_lock);

    if (object->recursion && object->owner == self) {
        if (--object->recursion == 0) {
            object->owner = 0;
            pthread_cond_broadcast(&s_changed);
        }

        released = true;
    }

    pthread_mutex_unlock(&s_lock);

    return released;
}

bool MacWaitable::ReleaseSemaphore(void* handle, int32_t count, int32_t* previousCount) {
    Object* object = static_cast<Object*>(handle);

    if (!object || object->kind != Kind::Semaphore || count <= 0) {
        return false;
    }

    bool released = false;

    pthread_mutex_lock(&s_lock);

    if (previousCount) {
        *previousCount = object->count;
    }

    if (object->count + count <= object->maximum) {
        object->count += count;
        pthread_cond_broadcast(&s_changed);
        released = true;
    }

    pthread_mutex_unlock(&s_lock);

    return released;
}

void MacWaitable::SetThread(void* handle, pthread_t thread, uint32_t threadId) {
    Object* object = static_cast<Object*>(handle);

    pthread_mutex_lock(&s_lock);
    object->thread = thread;
    object->threadId = threadId;
    object->hasThread = true;
    pthread_mutex_unlock(&s_lock);
}

bool MacWaitable::GetThread(void* handle, pthread_t* thread) {
    Object* object = static_cast<Object*>(handle);

    if (!object || object->kind != Kind::Thread) {
        return false;
    }

    pthread_mutex_lock(&s_lock);
    bool hasThread = object->hasThread;

    if (hasThread) {
        *thread = object->thread;
    }

    pthread_mutex_unlock(&s_lock);

    return hasThread;
}

void MacWaitable::SignalThreadExit(void* handle) {
    Object* object = static_cast<Object*>(handle);

    pthread_mutex_lock(&s_lock);
    object->signaled = true;
    pthread_cond_broadcast(&s_changed);
    pthread_mutex_unlock(&s_lock);
}

uint32_t MacWaitable::Wait(void* handle, uint32_t timeout) {
    return WaitMultiple(1, &handle, false, timeout);
}

uint32_t MacWaitable::WaitMultiple(uint32_t count, void* const* handles, bool waitAll, uint32_t timeout) {
    if (!count || count > kMaxWaitObjects) {
        return kWaitFailed;
    }

    for (uint32_t i = 0; i < count; i++) {
        if (!handles[i]) {
            return kWaitFailed;
        }
    }

    uint32_t self = CurrentThreadId();
    timespec deadline = Deadline(timeout == kInfinite ? 0 : timeout);
    uint32_t result = kWaitTimeout;

    pthread_mutex_lock(&s_lock);

    for (;;) {
        if (waitAll) {
            bool all = true;

            for (uint32_t i = 0; i < count && all; i++) {
                all = IsSignaled(static_cast<Object*>(handles[i]), self);
            }

            if (all) {
                for (uint32_t i = 0; i < count; i++) {
                    Consume(static_cast<Object*>(handles[i]), self);
                }

                result = kWaitObject0;
                break;
            }
        } else {
            uint32_t index = count;

            for (uint32_t i = 0; i < count; i++) {
                if (IsSignaled(static_cast<Object*>(handles[i]), self)) {
                    index = i;
                    break;
                }
            }

            if (index < count) {
                Consume(static_cast<Object*>(handles[index]), self);
                result = kWaitObject0 + index;
                break;
            }
        }

        if (timeout == 0) {
            break;
        }

        if (timeout == kInfinite) {
            pthread_cond_wait(&s_changed, &s_lock);
        } else if (pthread_cond_timedwait(&s_changed, &s_lock, &deadline) == ETIMEDOUT) {
            // One last look: the state may have changed together with the timeout.
            timeout = 0;
        }
    }

    pthread_mutex_unlock(&s_lock);

    return result;
}

uint32_t MacWaitable::CurrentThreadId() {
    return static_cast<uint32_t>(pthread_mach_thread_np(pthread_self()));
}
