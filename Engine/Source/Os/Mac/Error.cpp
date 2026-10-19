// macOS implementation of Os/Win64/Error.cpp: the text for the last OS error (errno).

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include "Engine/Source/Os/Error.h"

char* OsGetLastErrorStr() {
    return strdup(strerror(errno));
}

void OsFreeLastErrorStr(char* str) {
    free(str);
}
