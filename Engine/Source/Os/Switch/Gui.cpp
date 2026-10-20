// Nintendo Switch implementation of the Os GUI functions (Os/Win64/Gui.cpp) the client uses: the Gx window registry,
// message boxes (the system's error applet, which has one button: every box answers as if OK or Yes was chosen), and
// modifier state.

#include <cstdint>
#include "Engine/Source/Os/Gui.h"
#include "Engine/Source/Os/Switch/SwitchInput.h"

static void* s_GxDevWindow;

void OsGuiSetWindowTitle(void* window, const char* title) {
}

void* OsGuiGetWindow(int32_t type) {
    // The game window is the screen, and always the key and main window.
    return type >= 0 && type <= 2 ? s_GxDevWindow : nullptr;
}

void OsGuiSetGxWindow(void* window) {
    s_GxDevWindow = window;
}

int32_t OsGuiMessageBox(void* parentWindow, int32_t style, const char* message, const char* title) {
    SwitchShowMessage(title, message);

    // 0 is OK / Yes, as on Windows.
    return 0;
}

int32_t OsGuiIsModifierKeyDown(int32_t key) {
    return SwitchIsModifierDown(key);
}

void OsGuiBeep() {
}

int32_t OsGuiProcessMessage(void* message) {
    return 0;
}
