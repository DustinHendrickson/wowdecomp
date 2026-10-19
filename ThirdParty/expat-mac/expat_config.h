/* expat configuration for non-Windows builds. Mirrors expat/lib/winconfig.h, which the Windows build uses. */
#ifndef EXPAT_CONFIG_H
#define EXPAT_CONFIG_H

#include <string.h>

#define XML_NS 1
#define XML_DTD 1
#define XML_CONTEXT_BYTES 1024

/* arm64 and x86-64 are both little endian. */
#define BYTEORDER 1234

#define HAVE_MEMMOVE 1

#endif
