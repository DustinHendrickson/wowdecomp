#ifndef ENGINE_SOURCE_BASE_COMPAT_MACARM64_EMMINTRIN_H
#define ENGINE_SOURCE_BASE_COMPAT_MACARM64_EMMINTRIN_H

// arm64 has no SSE. sse2neon implements the _mm_* intrinsics on NEON with the same results.
#include "sse2neon.h"

#endif
