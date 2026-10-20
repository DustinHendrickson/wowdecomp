#ifndef ENGINE_SOURCE_BASE_COMPAT_SWITCH_CRT_EXTERNS_H
#define ENGINE_SOURCE_BASE_COMPAT_SWITCH_CRT_EXTERNS_H

// Darwin's <crt_externs.h> for the Switch, where libnx keeps the arguments the loader passed.

#if defined(__cplusplus)
extern "C" {
#endif

extern int __system_argc;
extern char** __system_argv;

static inline int* _NSGetArgc(void) {
    return &__system_argc;
}

static inline char*** _NSGetArgv(void) {
    return &__system_argv;
}

#if defined(__cplusplus)
}
#endif

#endif
