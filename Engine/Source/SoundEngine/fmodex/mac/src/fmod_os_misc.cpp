/*
    macOS port of fmodex/win32/src/fmod_os_misc.cpp (wowdecomp): memory, files, time, threads, critical sections,
    semaphores and libraries for FMOD Ex on POSIX. Time keeps the Win32 units: "ns" is microseconds there
    (QueryPerformanceCounter * 1e6 / frequency), truncated to 32 bits.
*/

#include "fmod_settings.h"
#include "fmod.h"
#include "fmod_os_misc.h"
#include "fmod_memory.h"

#include <dispatch/dispatch.h>
#include <dlfcn.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct FMOD_OS_CRITICALSECTION
{
    pthread_mutex_t mutex;
};

struct FMOD_OS_SEMAPHORE
{
    dispatch_semaphore_t semaphore;
};

int gSizeofCriticalSection = sizeof(FMOD_OS_CRITICALSECTION);
int gSizeofSemaphore       = sizeof(FMOD_OS_SEMAPHORE);

namespace
{
    struct ThreadStart
    {
        THREAD_RETURNTYPE (THREAD_CALLCONV *func)(void *param);
        void                               *param;
        char                                name[64];
    };

    void *ThreadEntry(void *param)
    {
        ThreadStart *start = (ThreadStart *)param;

        if (start->name[0])
        {
            pthread_setname_np(start->name);
        }

        start->func(start->param);
        return 0;
    }

    uint64_t UptimeNs()
    {
        return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    }
}

void *FMOD_OS_Memory_Alloc(int size, FMOD_MEMORY_TYPE type)
{
    return malloc(size);
}

void *FMOD_OS_Memory_Realloc(void *ptr, int size, FMOD_MEMORY_TYPE type)
{
    return realloc(ptr, size);
}

void FMOD_OS_Memory_Free(void *ptr, FMOD_MEMORY_TYPE type)
{
    free(ptr);
}

FMOD_RESULT FMOD_OS_File_DriveStatus()
{
    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_File_Open(const char *name, char *mode, int unicode, unsigned int *filesize, void **handle)
{
    char path[1024];

    if (unicode)
    {
        /* FMOD's wide strings are UTF-16, as on Windows. */
        const unsigned short *wide = (const unsigned short *)name;
        size_t pos = 0;

        for (; *wide && pos + 4 < sizeof(path); wide++)
        {
            unsigned int c = *wide;

            if (c < 0x80)
            {
                path[pos++] = (char)c;
            }
            else if (c < 0x800)
            {
                path[pos++] = (char)(0xC0 | (c >> 6));
                path[pos++] = (char)(0x80 | (c & 0x3F));
            }
            else
            {
                path[pos++] = (char)(0xE0 | (c >> 12));
                path[pos++] = (char)(0x80 | ((c >> 6) & 0x3F));
                path[pos++] = (char)(0x80 | (c & 0x3F));
            }
        }

        path[pos] = 0;
        name = path;
    }

    FILE *file = fopen(name, "rb");

    if (!file)
    {
        *handle = 0;
        return FMOD_ERR_FILE_NOTFOUND;
    }

    fseek(file, 0, SEEK_END);
    *filesize = (unsigned int)ftell(file);
    fseek(file, 0, SEEK_SET);
    *handle = file;

    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_File_Close(void *handle)
{
    if (!handle)
    {
        return FMOD_ERR_FILE_BAD;
    }

    fclose((FILE *)handle);
    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_File_Cancel(void *handle)
{
    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_File_Read(void *handle, void *buf, unsigned int count, unsigned int *read)
{
    size_t rd = fread(buf, 1, count, (FILE *)handle);

    *read = (unsigned int)rd;

    if (rd != count)
    {
        return ferror((FILE *)handle) ? FMOD_ERR_FILE_BAD : FMOD_ERR_FILE_EOF;
    }

    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_File_Seek(void *handle, unsigned int offset)
{
    return fseek((FILE *)handle, offset, SEEK_SET) ? FMOD_ERR_FILE_BAD : FMOD_OK;
}

FMOD_RESULT FMOD_OS_Debug_OutputStr(const char *s)
{
    fputs(s, stderr);
    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_Time_GetNs(unsigned int *ns)
{
    *ns = (unsigned int)(UptimeNs() / 1000);
    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_Time_GetMs(unsigned int *ms)
{
    *ms = (unsigned int)(UptimeNs() / 1000000);
    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_Time_Sleep(unsigned int ms)
{
    usleep(ms * 1000);
    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_Thread_GetCurrentID(FMOD_UINT_NATIVE *id)
{
    *id = (FMOD_UINT_NATIVE)pthread_mach_thread_np(pthread_self());
    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_Thread_Create(const char *name, THREAD_RETURNTYPE (THREAD_CALLCONV *func)(void *param), void *param, FMOD_THREAD_PRIORITY priority, void *stack, int stacksize, void **handle)
{
    if (!handle)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    ThreadStart *start = (ThreadStart *)calloc(1, sizeof(ThreadStart));

    if (!start)
    {
        return FMOD_ERR_MEMORY;
    }

    start->func  = func;
    start->param = param;

    if (name)
    {
        strncpy(start->name, name, sizeof(start->name) - 1);
    }

    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);

    if (stacksize > 0)
    {
        size_t size = (size_t)stacksize < PTHREAD_STACK_MIN ? PTHREAD_STACK_MIN : (size_t)stacksize;
        size = (size + 0x3FFF) & ~(size_t)0x3FFF;
        pthread_attr_setstacksize(&attributes, size);
    }

    /* Audio mixing gets the user-interactive class, the closest to Windows' time-critical / "Pro Audio" setting. */
    if (priority >= FMOD_THREAD_PRIORITY_HIGH)
    {
        pthread_attr_set_qos_class_np(&attributes, QOS_CLASS_USER_INTERACTIVE, 0);
    }
    else if (priority <= FMOD_THREAD_PRIORITY_LOW)
    {
        pthread_attr_set_qos_class_np(&attributes, QOS_CLASS_UTILITY, 0);
    }

    pthread_t thread;
    int created = pthread_create(&thread, &attributes, ThreadEntry, start);
    pthread_attr_destroy(&attributes);

    if (created)
    {
        free(start);
        return FMOD_ERR_MEMORY;
    }

    *handle = start;
    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_Thread_Destroy(void *handle)
{
    /* Windows closes the thread handle here; the thread itself has already finished. */
    free(handle);
    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_CriticalSection_Create(FMOD_OS_CRITICALSECTION **crit, bool memorycrit)
{
    FMOD_OS_CRITICALSECTION *section = (FMOD_OS_CRITICALSECTION *)malloc(sizeof(FMOD_OS_CRITICALSECTION));

    if (!section)
    {
        return FMOD_ERR_MEMORY;
    }

    pthread_mutexattr_t attributes;
    pthread_mutexattr_init(&attributes);
    pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&section->mutex, &attributes);
    pthread_mutexattr_destroy(&attributes);

    *crit = section;
    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_CriticalSection_Free(FMOD_OS_CRITICALSECTION *crit)
{
    if (!crit)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    pthread_mutex_destroy(&crit->mutex);
    free(crit);
    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_CriticalSection_Enter(FMOD_OS_CRITICALSECTION *crit)
{
    if (!crit)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    pthread_mutex_lock(&crit->mutex);
    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_CriticalSection_Leave(FMOD_OS_CRITICALSECTION *crit)
{
    if (!crit)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    pthread_mutex_unlock(&crit->mutex);
    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_Semaphore_Create(FMOD_OS_SEMAPHORE **sema)
{
    FMOD_OS_SEMAPHORE *semaphore = (FMOD_OS_SEMAPHORE *)malloc(sizeof(FMOD_OS_SEMAPHORE));

    if (!semaphore)
    {
        return FMOD_ERR_MEMORY;
    }

    semaphore->semaphore = dispatch_semaphore_create(0);
    *sema = semaphore;
    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_Semaphore_Free(FMOD_OS_SEMAPHORE *sema)
{
    if (!sema)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    dispatch_release(sema->semaphore);
    free(sema);
    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_Semaphore_Wait(FMOD_OS_SEMAPHORE *sema)
{
    if (!sema)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    dispatch_semaphore_wait(sema->semaphore, DISPATCH_TIME_FOREVER);
    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_Semaphore_Signal(FMOD_OS_SEMAPHORE *sema, bool interrupt)
{
    if (!sema)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    dispatch_semaphore_signal(sema->semaphore);
    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_Library_Load(const char *dllname, FMOD_OS_LIBRARY **handle)
{
    void *library = dlopen(dllname, RTLD_NOW | RTLD_LOCAL);

    if (!library)
    {
        return FMOD_ERR_FILE_NOTFOUND;
    }

    *handle = (FMOD_OS_LIBRARY *)library;
    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_Library_GetProcAddress(FMOD_OS_LIBRARY *handle, const char *procname, void **address)
{
    *address = dlsym((void *)handle, procname);
    return *address ? FMOD_OK : FMOD_ERR_FILE_UNWANTED;
}

FMOD_RESULT FMOD_OS_Library_Free(FMOD_OS_LIBRARY *handle)
{
    dlclose((void *)handle);
    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_CheckDriverList(bool *devicelistchanged)
{
    /* The default output unit follows the system's output device by itself. */
    *devicelistchanged = false;
    return FMOD_OK;
}
