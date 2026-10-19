// macOS implementation of the Os GUI functions (Os/Win64/Gui.cpp) the client uses: the Gx window registry, message
// boxes, window titles and modifier state. The Win32 tool dialogs (COsDialog and friends) have no Mac version; nothing
// outside the Windows build creates them.

#import <AppKit/AppKit.h>
#include <cstdint>
#include "Engine/Source/Os/Gui.h"
#include "Engine/Source/Os/Mac/OsMacWindow.h"

static void* s_GxDevWindow;

void OsGuiSetWindowTitle(void* window, const char* title) {
    if (window) {
        ((__bridge NSWindow*)window).title = [NSString stringWithUTF8String:title ? title : ""];
    }
}

void* OsGuiGetWindow(int32_t type) {
    switch (type) {
        case 0:
            return s_GxDevWindow;
        case 1:
            return (__bridge void*)NSApp.keyWindow;
        case 2:
            return (__bridge void*)NSApp.mainWindow;
        default:
            return nullptr;
    }
}

void OsGuiSetGxWindow(void* window) {
    s_GxDevWindow = window;
}

int32_t OsGuiMessageBox(void* parentWindow, int32_t style, const char* message, const char* title) {
    OsMacInitializeApplication();

    @autoreleasepool {
        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = [NSString stringWithUTF8String:title ? title : ""];
        alert.informativeText = [NSString stringWithUTF8String:message ? message : ""];

        // Results as on Windows: 0 for OK / Yes, 1 for No, 2 for Cancel.
        int32_t results[3];
        int32_t count = 0;

        switch (style) {
            case 1:
                [alert addButtonWithTitle:@"OK"];
                [alert addButtonWithTitle:@"Cancel"];
                results[count++] = 0;
                results[count++] = 2;
                break;
            case 2:
                [alert addButtonWithTitle:@"Yes"];
                [alert addButtonWithTitle:@"No"];
                results[count++] = 0;
                results[count++] = 1;
                break;
            case 3:
                [alert addButtonWithTitle:@"Yes"];
                [alert addButtonWithTitle:@"No"];
                [alert addButtonWithTitle:@"Cancel"];
                results[count++] = 0;
                results[count++] = 1;
                results[count++] = 2;
                break;
            default:
                [alert addButtonWithTitle:@"OK"];
                results[count++] = 0;
                break;
        }

        NSModalResponse response = [alert runModal];
        int32_t index = static_cast<int32_t>(response - NSAlertFirstButtonReturn);

        return index >= 0 && index < count ? results[index] : 2;
    }
}

int32_t OsGuiIsModifierKeyDown(int32_t key) {
    NSEventModifierFlags flags = [NSEvent modifierFlags];

    switch (key) {
        case 0:
            return (flags & NSEventModifierFlagControl) != 0;
        case 1:
            return (flags & NSEventModifierFlagShift) != 0;
        case 2:
            return (flags & NSEventModifierFlagOption) != 0;
        default:
            return 0;
    }
}

void OsGuiBeep() {
    NSBeep();
}

int32_t OsGuiProcessMessage(void* message) {
    // Windows routes messages to tool dialogs and menu accelerators here; AppKit dispatches those itself.
    return 0;
}
