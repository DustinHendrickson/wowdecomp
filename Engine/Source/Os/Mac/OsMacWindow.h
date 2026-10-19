#ifndef ENGINE_SOURCE_OS_MAC_OSMACWINDOW_H
#define ENGINE_SOURCE_OS_MAC_OSMACWINDOW_H

// The macOS game window, for the Mac Gx device (Windows' Gx devices create their own HWND with CreateWindowEx).
//
// The window is an NSWindow whose content view feeds keyboard, text, mouse, focus, size and close events into the Os
// input queue, producing the same OSINPUT events OsWindowProc makes from window messages on Windows. Coordinates are in
// points with the origin at the top left of the content area, like a Win32 client area. All calls are main-thread only.

#include <cstdint>

// Sets up NSApplication (activation policy, menu bar with Quit and Minimize) once. Called by the functions below; call
// it directly only to show dialogs before any window exists.
void OsMacInitializeApplication();

// Creates and shows the game window with a content area of width x height points, at (x, y) from the top left of the
// main screen, or centered when x or y is negative. Returns the NSWindow as an opaque handle, or nullptr.
void* OsMacCreateGameWindow(const char* title, int32_t x, int32_t y, int32_t width, int32_t height);

// The window's content view (NSView), which a GL context or Metal layer attaches to.
void* OsMacGetGameView(void* window);

// Pixels per point of the window's screen (2 on Retina displays).
float OsMacGetBackingScale(void* window);

void OsMacDestroyGameWindow(void* window);

// Content area size in points, and its top-left corner in screen points (origin at the top left of the main screen).
void OsMacGetContentSize(void* window, int32_t* width, int32_t* height);
void OsMacGetContentScreenOrigin(void* window, int32_t* x, int32_t* y);

// Fullscreen is a borderless window covering the screen with the menu bar and Dock hidden; the display mode isn't
// changed. Windowed restores the title bar with a content area of width x height points.
void OsMacSetWindowFullscreen(void* window, bool fullscreen, int32_t width, int32_t height);

// Window events for the Gx device, the part of the window's life Windows reports to the device's own window procedure
// (WM_SIZE, WM_SETFOCUS, ...). Input events still go to the Os queue.
enum OsMacWindowEvent {
    OsMacWindowEvent_Resized = 0,
    OsMacWindowEvent_Minimized = 1,
    OsMacWindowEvent_Restored = 2,
    OsMacWindowEvent_FocusGained = 3,
    OsMacWindowEvent_FocusLost = 4,
};

typedef void (*OsMacWindowListener)(void* param, OsMacWindowEvent event);

void OsMacSetWindowListener(void* window, OsMacWindowListener listener, void* param);

#endif
