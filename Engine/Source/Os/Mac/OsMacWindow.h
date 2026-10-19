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

#endif
