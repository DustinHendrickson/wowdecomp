// Nintendo Switch implementation of Os/Win64/OsClipboard.cpp. The console has no clipboard, so the game keeps its own:
// copy and paste work within the game, and the software keyboard passes text to and from text boxes through it (see
// Input.cpp). The returned string is allocated with SMemAlloc, as OsClipboardFreeString expects.

#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include "Engine/Source/Os/OsClipboard.h"
#include "Engine/Source/Os/Switch/SwitchInput.h"
#include "Engine/Source/Storm/H/storm.h"

namespace {

std::mutex s_lock;
std::string* s_text;
uint32_t s_generation;

}

uint32_t SwitchClipboardGeneration() {
    std::lock_guard<std::mutex> lock(s_lock);
    return s_generation;
}

char* IOsClipboardGetString(void* window) {
    std::lock_guard<std::mutex> lock(s_lock);

    if (!s_text) {
        return nullptr;
    }

    size_t size = s_text->size() + 1;
    char* string = static_cast<char*>(SMemAlloc(static_cast<uint32_t>(size), __FILE__, __LINE__, 0));
    memcpy(string, s_text->c_str(), size);

    return string;
}

void OsClipboardFreeString(char* string) {
    if (string) {
        SMemFree(string, __FILE__, __LINE__, 0);
    }
}

int32_t IOsClipboardPutString(const char* string, void* window) {
    std::lock_guard<std::mutex> lock(s_lock);

    if (!s_text) {
        s_text = new std::string();
    }

    *s_text = string ? string : "";
    s_generation++;

    return 1;
}
