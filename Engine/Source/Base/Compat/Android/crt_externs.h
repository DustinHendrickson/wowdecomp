#ifndef ENGINE_SOURCE_BASE_COMPAT_ANDROID_CRT_EXTERNS_H
#define ENGINE_SOURCE_BASE_COMPAT_ANDROID_CRT_EXTERNS_H

// Darwin's <crt_externs.h> for Android, where an app has no command line: the activity keeps a stand-in argument list
// (WoW/Source/Android/main.cpp).

#if defined(__cplusplus)
extern "C" {
#endif

extern int wowdecomp_android_argc;
extern char** wowdecomp_android_argv;

static inline int* _NSGetArgc(void) {
    return &wowdecomp_android_argc;
}

static inline char*** _NSGetArgv(void) {
    return &wowdecomp_android_argv;
}

#if defined(__cplusplus)
}
#endif

#endif
