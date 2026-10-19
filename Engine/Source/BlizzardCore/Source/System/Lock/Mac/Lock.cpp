// macOS implementation of Blizzard::Lock (Win64/Atomic.cpp, Event.cpp, Mutex.cpp).
// Mutex is a recursive pthread mutex (CRITICAL_SECTION in Platform.h); Event is a MacWaitable auto-reset event.

#include <cstdint>
#include "Engine/Source/BlizzardCore/Source/System/Lock/Lock.h"
#include "Engine/Source/BlizzardCore/Source/System/Lock/Mac/Waitable.h"

int32_t Blizzard::Lock::Atomic::Increment() {
    return __atomic_add_fetch(&this->m_value, 1, __ATOMIC_SEQ_CST);
}

int32_t Blizzard::Lock::Atomic::Decrement() {
    return __atomic_sub_fetch(&this->m_value, 1, __ATOMIC_SEQ_CST);
}

void Blizzard::Lock::EventCreate(Event& event) {
    event = MacWaitable::CreateEvent(false, false);
}

void Blizzard::Lock::EventDestroy(Event& event) {
    MacWaitable::Close(event);
}

void Blizzard::Lock::EventSet(Event& event) {
    MacWaitable::SetEvent(event);
}

void Blizzard::Lock::EventWaitForever(Event& event) {
    MacWaitable::Wait(event, MacWaitable::kInfinite);
}

Blizzard::Lock::Event* Blizzard::Lock::EventWait(uint32_t timeout, Event& event) {
    return MacWaitable::Wait(event, timeout) == MacWaitable::kWaitObject0 ? &event : nullptr;
}

Blizzard::Lock::EventObject::EventObject() {
    this->m_event = MacWaitable::CreateEvent(false, false);
}

Blizzard::Lock::EventObject::~EventObject() {
    EventDestroy(this->m_event);
}

void Blizzard::Lock::MutexCreate(Mutex& mutex) {
    InitializeCriticalSection(&mutex);
}

void Blizzard::Lock::MutexEnter(Mutex& mutex) {
    EnterCriticalSection(&mutex);
}

void Blizzard::Lock::MutexLeave(Mutex& mutex) {
    LeaveCriticalSection(&mutex);
}

void Blizzard::Lock::MutexDestroy(Mutex& mutex) {
    DeleteCriticalSection(&mutex);
}

Blizzard::Lock::ScopedMutex::ScopedMutex(Mutex& mutex) {
    this->m_mutex = &mutex;
    MutexEnter(mutex);
}

Blizzard::Lock::ScopedMutex::~ScopedMutex() {
    MutexLeave(*this->m_mutex);
}

Blizzard::Lock::RMutex::RMutex() {
    MutexCreate(this->m_mutex);
}

Blizzard::Lock::RMutex::~RMutex() {
    MutexDestroy(this->m_mutex);
}
