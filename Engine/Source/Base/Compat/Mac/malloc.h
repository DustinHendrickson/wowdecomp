#ifndef ENGINE_SOURCE_BASE_COMPAT_MAC_MALLOC_H
#define ENGINE_SOURCE_BASE_COMPAT_MAC_MALLOC_H

// macOS has no <malloc.h>. Decompiled code includes it for alloca and _msize, which live in these headers here.
#include <alloca.h>
#include <malloc/malloc.h>
#include <stdlib.h>

#endif
