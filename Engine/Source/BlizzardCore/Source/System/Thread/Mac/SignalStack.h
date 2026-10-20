#ifndef ENGINE_SOURCE_BLIZZARDCORE_SOURCE_SYSTEM_THREAD_MAC_SIGNALSTACK_H
#define ENGINE_SOURCE_BLIZZARDCORE_SOURCE_SYSTEM_THREAD_MAC_SIGNALSTACK_H

// An alternate signal stack for the current thread, for as long as the object lives. The crash handler (Storm's
// SErrCatchUnhandledExceptions) runs on the alternate stack so a stack overflow can still be reported, and sigaltstack is
// per thread: without one, an overflow on any thread but the main one leaves the handler no stack to run on and the
// process hangs or dies with no log. Every thread the Mac layers start (Storm, BlizzardCore, FMOD) declares one first.

#include <csignal>
#include <cstdlib>

#if defined(__SWITCH__)

// The Switch has no signals: crashes reach libnx's exception handler, which runs on its own stack.
class MacThreadSignalStack {
    public:
    MacThreadSignalStack() {
    }

    MacThreadSignalStack(const MacThreadSignalStack&) = delete;
    MacThreadSignalStack& operator=(const MacThreadSignalStack&) = delete;
};

#else

class MacThreadSignalStack {
    public:
    MacThreadSignalStack() {
        this->m_stack.ss_sp = malloc(kSize);
        this->m_stack.ss_size = kSize;
        this->m_stack.ss_flags = 0;

        if (this->m_stack.ss_sp && sigaltstack(&this->m_stack, nullptr) != 0) {
            free(this->m_stack.ss_sp);
            this->m_stack.ss_sp = nullptr;
        }
    }

    ~MacThreadSignalStack() {
        if (!this->m_stack.ss_sp) {
            return;
        }

        stack_t disable = {};
        disable.ss_flags = SS_DISABLE;
        sigaltstack(&disable, nullptr);
        free(this->m_stack.ss_sp);
    }

    MacThreadSignalStack(const MacThreadSignalStack&) = delete;
    MacThreadSignalStack& operator=(const MacThreadSignalStack&) = delete;

    private:
    // As much as the main thread's: writing the crash log walks the stack and formats it there.
    static const size_t kSize = 256 * 1024;

    stack_t m_stack;
};

#endif

#endif
