// OsWindowTest: the macOS game window on its own, for checking the Os layer without the rest of the client.
//
// By default it opens the window and prints every OSINPUT event the game would receive (press R to toggle relative
// mouse mode, close the window to quit). With --self-test it sends the window AppKit events instead and checks the
// events that come out of OsInputGet, exiting non-zero on failure.
#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "Engine/Source/Event/Types.h"
#include "Engine/Source/Os/Gui.h"
#include "Engine/Source/Os/Input.h"
#include "Engine/Source/Os/Mac/OsMacWindow.h"
#include "Engine/Source/Os/OsClipboard.h"
#include "Engine/Source/Os/Time.h"

static int s_failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); s_failures++; } else { printf("ok   %s\n", #cond); } } while (0)

struct Event {
    OSINPUT id;
    int32_t p[4];
};

static const char* Name(OSINPUT id) {
    static const char* names[] = { "CAPTURE_CHANGED", "CHAR", "STRING", "IME", "SIZE", "CLOSE", "FOCUS", "KEY_DOWN", "KEY_UP", "MOUSE_DOWN", "MOUSE_MOVE", "MOUSE_WHEEL", "MOUSE_MOVE_RELATIVE", "MOUSE_UP", "14", "15", "16", "17", "18", "SHUTDOWN" };
    return id >= 0 && id <= 19 ? names[id] : "?";
}

static std::vector<Event> Drain() {
    std::vector<Event> events;
    Event e;

    for (int i = 0; i < 50; i++) {
        while (OsInputGet(&e.id, &e.p[0], &e.p[1], &e.p[2], &e.p[3])) {
            if (e.id != static_cast<OSINPUT>(-1)) {
                events.push_back(e);
            }
        }

        usleep(2000);
    }

    for (const Event& event : events) {
        printf("     event %-20s %d %d %d\n", Name(event.id), event.p[0], event.p[1], event.p[2]);
    }

    return events;
}

static bool Has(const std::vector<Event>& events, OSINPUT id, int32_t p0, int32_t p1 = INT32_MIN, int32_t p2 = INT32_MIN) {
    for (const Event& e : events) {
        if (e.id == id && e.p[0] == p0 && (p1 == INT32_MIN || e.p[1] == p1) && (p2 == INT32_MIN || e.p[2] == p2)) {
            return true;
        }
    }

    return false;
}

static NSEvent* Key(NSWindow* window, NSEventType type, unsigned short keyCode, NSString* characters, NSEventModifierFlags flags = 0) {
    return [NSEvent keyEventWithType:type location:NSZeroPoint modifierFlags:flags timestamp:0 windowNumber:window.windowNumber context:nil characters:characters charactersIgnoringModifiers:characters isARepeat:NO keyCode:keyCode];
}

static NSEvent* Mouse(NSWindow* window, NSEventType type, NSPoint viewPoint) {
    // Window coordinates have their origin at the bottom left.
    NSPoint point = NSMakePoint(viewPoint.x, window.contentView.bounds.size.height - viewPoint.y);
    return [NSEvent mouseEventWithType:type location:point modifierFlags:0 timestamp:0 windowNumber:window.windowNumber context:nil eventNumber:0 clickCount:1 pressure:1.0];
}

int main(int argc, char** argv) {
    bool interactive = !(argc > 1 && !strcmp(argv[1], "--self-test"));

    OsTimeStartup(OsTimeManager::BestAvailable);
    OsInputInitialize();

    void* handle = OsMacCreateGameWindow("OsWindowTest", -1, -1, 1024, 768);
    CHECK(handle != nullptr);
    OsGuiSetGxWindow(handle);
    NSWindow* window = (__bridge NSWindow*)handle;
    CHECK(window.visible);
    CHECK(OsGuiGetWindow(0) == handle);
    // Sizes and positions are render pixels: S per point (2 with Retina rendering on a Retina display).
    int32_t S = static_cast<int32_t>(OsMacGetRenderScale(handle));
    printf("     backing scale %.1f, render scale %d\n", OsMacGetBackingScale(handle), S);

    RECT rect;
    CHECK(OsGetDefaultWindowRect(&rect) && rect.right == 1024 && rect.bottom == 768);
    Drain();

    if (interactive) {
        printf("Window open. Type, click, scroll, resize; press R to toggle relative mouse; close the window to quit.\n");
        bool relative = false;

        for (;;) {
            Event e;

            while (OsInputGet(&e.id, &e.p[0], &e.p[1], &e.p[2], &e.p[3])) {
                if (e.id == static_cast<OSINPUT>(-1)) {
                    continue;
                }

                printf("%-20s %6d %6d %6d  t=%u\n", Name(e.id), e.p[0], e.p[1], e.p[2], static_cast<uint32_t>(e.p[3]));
                fflush(stdout);

                if (e.id == OS_INPUT_KEY_DOWN && e.p[0] == KEY_R) {
                    relative = !relative;
                    OsInputSetMouseMode(relative ? OS_MOUSE_MODE_RELATIVE : OS_MOUSE_MODE_NORMAL);
                }

                if (e.id == OS_INPUT_CLOSE) {
                    OsMacDestroyGameWindow(handle);
                    return 0;
                }
            }

            usleep(5000);
        }
    }

    // Key with text.
    [window sendEvent:Key(window, NSEventTypeKeyDown, kVK_ANSI_A, @"a")];
    [window sendEvent:Key(window, NSEventTypeKeyUp, kVK_ANSI_A, @"a")];
    std::vector<Event> events = Drain();
    CHECK(Has(events, OS_INPUT_KEY_DOWN, KEY_A, 1));
    CHECK(Has(events, OS_INPUT_CHAR, 'a', 1));
    CHECK(Has(events, OS_INPUT_KEY_UP, KEY_A));

    // Special keys, digit row, punctuation, keypad, function keys.
    struct { unsigned short code; NSString* chars; KEY key; } keys[] = {
        { kVK_Return, @"\r", KEY_ENTER },
        { kVK_Escape, @"\x1b", KEY_ESCAPE },
        { kVK_ANSI_7, @"7", KEY_7 },
        { kVK_ANSI_Minus, @"-", KEY_MINUS },
        { kVK_ANSI_LeftBracket, @"[", KEY_BRACKET_OPEN },
        { kVK_ANSI_Keypad5, @"5", KEY_NUMPAD5 },
        { kVK_F5, @"", KEY_F5 },
        { kVK_LeftArrow, @"", KEY_LEFT },
    };

    // One key at a time: the queue holds 16 events, and real input is pulled out between OS events.
    events.clear();

    for (const auto& k : keys) {
        [window sendEvent:Key(window, NSEventTypeKeyDown, k.code, k.chars)];
        [window sendEvent:Key(window, NSEventTypeKeyUp, k.code, k.chars)];
        std::vector<Event> keyEvents = Drain();
        CHECK(Has(keyEvents, OS_INPUT_KEY_DOWN, k.key) && Has(keyEvents, OS_INPUT_KEY_UP, k.key));
        events.insert(events.end(), keyEvents.begin(), keyEvents.end());
    }

    CHECK(!Has(events, OS_INPUT_CHAR, '\r') && !Has(events, OS_INPUT_CHAR, 0x1b) && !Has(events, OS_INPUT_CHAR, 0xF708));
    CHECK(Has(events, OS_INPUT_CHAR, '7') && Has(events, OS_INPUT_CHAR, '-'));

    // Modifiers: left and right shift come apart.
    [window sendEvent:Key(window, NSEventTypeFlagsChanged, kVK_Shift, @"", NSEventModifierFlagShift | 0x2)];
    [window sendEvent:Key(window, NSEventTypeFlagsChanged, kVK_RightShift, @"", NSEventModifierFlagShift | 0x2 | 0x4)];
    [window sendEvent:Key(window, NSEventTypeFlagsChanged, kVK_RightShift, @"", NSEventModifierFlagShift | 0x2)];
    [window sendEvent:Key(window, NSEventTypeFlagsChanged, kVK_Shift, @"", 0)];
    [window sendEvent:Key(window, NSEventTypeFlagsChanged, kVK_Option, @"", NSEventModifierFlagOption | 0x20)];
    [window sendEvent:Key(window, NSEventTypeFlagsChanged, kVK_Option, @"", 0)];
    events = Drain();
    CHECK(Has(events, OS_INPUT_KEY_DOWN, KEY_LSHIFT) && Has(events, OS_INPUT_KEY_UP, KEY_LSHIFT));
    CHECK(Has(events, OS_INPUT_KEY_DOWN, KEY_RSHIFT) && Has(events, OS_INPUT_KEY_UP, KEY_RSHIFT));
    CHECK(Has(events, OS_INPUT_KEY_DOWN, KEY_LALT) && Has(events, OS_INPUT_KEY_UP, KEY_LALT));

    // Command shortcuts aren't game keys.
    [window sendEvent:Key(window, NSEventTypeKeyDown, kVK_ANSI_C, @"c", NSEventModifierFlagCommand)];
    events = Drain();
    CHECK(!Has(events, OS_INPUT_KEY_DOWN, KEY_C) && !Has(events, OS_INPUT_CHAR, 'c'));

    // Mouse, top-left based content coordinates (events in points, reported in render pixels).
    [window sendEvent:Mouse(window, NSEventTypeMouseMoved, NSMakePoint(200, 100))];
    [window sendEvent:Mouse(window, NSEventTypeLeftMouseDown, NSMakePoint(200, 100))];
    [window sendEvent:Mouse(window, NSEventTypeLeftMouseUp, NSMakePoint(210, 120))];
    [window sendEvent:Mouse(window, NSEventTypeRightMouseDown, NSMakePoint(300, 300))];
    [window sendEvent:Mouse(window, NSEventTypeRightMouseUp, NSMakePoint(300, 300))];
    events = Drain();
    CHECK(Has(events, OS_INPUT_MOUSE_MOVE, 0, 200 * S, 100 * S));
    CHECK(Has(events, OS_INPUT_MOUSE_DOWN, MOUSE_BUTTON_LEFT, 200 * S, 100 * S));
    CHECK(Has(events, OS_INPUT_MOUSE_UP, MOUSE_BUTTON_LEFT, 210 * S, 120 * S));
    CHECK(Has(events, OS_INPUT_MOUSE_DOWN, MOUSE_BUTTON_RIGHT, 300 * S, 300 * S) && Has(events, OS_INPUT_MOUSE_UP, MOUSE_BUTTON_RIGHT, 300 * S, 300 * S));

    // Wheel: one notch up is +120.
    CGEventRef scroll = CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitLine, 1, 1);
    NSRect frame = [window convertRectToScreen:window.contentView.frame];
    CGFloat mainHeight = NSScreen.screens.firstObject.frame.size.height;
    CGEventSetLocation(scroll, CGPointMake(frame.origin.x + 50, mainHeight - (frame.origin.y + frame.size.height - 60)));
    // A synthetic scroll event has no window to hit-test against; hand it to the view as AppKit would.
    NSEvent* wheel = [NSEvent eventWithCGEvent:scroll];
    printf("     wheel event: precise %d, scrollingDeltaY %.2f\n", wheel.hasPreciseScrollingDeltas, wheel.scrollingDeltaY);
    [window.contentView scrollWheel:wheel];
    CFRelease(scroll);
    events = Drain();
    CHECK(Has(events, OS_INPUT_MOUSE_WHEEL, 120));

    // Resize: reported once through OsInputGet.
    [window setContentSize:NSMakeSize(800, 600)];
    events = Drain();
    CHECK(Has(events, OS_INPUT_SIZE, 800 * S, 600 * S));
    CHECK(OsGetDefaultWindowRect(&rect) && rect.right == 800 * S && rect.bottom == 600 * S);

    // Close asks the game instead of closing.
    [window performClose:nil];
    events = Drain();
    CHECK(Has(events, OS_INPUT_CLOSE, 0));
    CHECK(window.visible);

    // Title and clipboard (the clipboard is restored afterwards).
    OsGuiSetWindowTitle(handle, "Wörld of Wärcraft");
    CHECK([window.title isEqualToString:@"Wörld of Wärcraft"]);
    NSString* saved = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
    CHECK(IOsClipboardPutString("clipboard ✓ test", handle));
    char* text = IOsClipboardGetString(handle);
    CHECK(text && !strcmp(text, "clipboard ✓ test"));
    OsClipboardFreeString(text);
    [[NSPasteboard generalPasteboard] clearContents];

    if (saved) {
        [[NSPasteboard generalPasteboard] setString:saved forType:NSPasteboardTypeString];
    }

    OsMacDestroyGameWindow(handle);
    CHECK(OsGuiGetWindow(0) == nullptr);

    printf("%s (%d failures)\n", s_failures ? "FAILED" : "PASSED", s_failures);
    return s_failures != 0;
}
