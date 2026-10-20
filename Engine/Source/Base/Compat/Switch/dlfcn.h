#ifndef ENGINE_SOURCE_BASE_COMPAT_SWITCH_DLFCN_H
#define ENGINE_SOURCE_BASE_COMPAT_SWITCH_DLFCN_H

// <dlfcn.h> for the Switch. Homebrew can't load dynamic libraries, so every load fails and callers take their
// no-library path.

#define RTLD_LAZY 0x1
#define RTLD_NOW 0x2
#define RTLD_LOCAL 0x4
#define RTLD_GLOBAL 0x8

typedef struct {
    const char* dli_fname;
    void* dli_fbase;
    const char* dli_sname;
    void* dli_saddr;
} Dl_info;

static inline void* dlopen(const char* path, int mode) {
    (void)path;
    (void)mode;
    return 0;
}

static inline void* dlsym(void* handle, const char* name) {
    (void)handle;
    (void)name;
    return 0;
}

static inline int dlclose(void* handle) {
    (void)handle;
    return 0;
}

static inline const char* dlerror(void) {
    return "dynamic libraries are not supported";
}

static inline int dladdr(const void* address, Dl_info* info) {
    (void)address;
    (void)info;
    return 0;
}

#endif
