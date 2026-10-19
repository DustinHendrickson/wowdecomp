// macOS implementation of Os/Win64/OsClipboard.cpp on the general pasteboard. Strings are UTF-8 both ways; the
// returned string is allocated with SMemAlloc, as OsClipboardFreeString expects.

#import <AppKit/AppKit.h>
#include <cstdint>
#include <cstring>
#include "Engine/Source/Os/OsClipboard.h"
#include "Engine/Source/Storm/H/storm.h"

char* IOsClipboardGetString(void* window) {
    @autoreleasepool {
        NSString* text = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];

        if (!text) {
            return nullptr;
        }

        const char* utf8 = text.UTF8String;
        size_t size = strlen(utf8) + 1;
        char* string = static_cast<char*>(SMemAlloc(static_cast<uint32_t>(size), __FILE__, __LINE__, 0));
        memcpy(string, utf8, size);

        return string;
    }
}

void OsClipboardFreeString(char* string) {
    if (string) {
        SMemFree(string, __FILE__, __LINE__, 0);
    }
}

int32_t IOsClipboardPutString(const char* string, void* window) {
    @autoreleasepool {
        NSString* text = [NSString stringWithUTF8String:string ? string : ""];

        if (!text) {
            return 0;
        }

        NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
        [pasteboard clearContents];

        return [pasteboard setString:text forType:NSPasteboardTypeString];
    }
}
