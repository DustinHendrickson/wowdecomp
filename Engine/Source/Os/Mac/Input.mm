// macOS implementation of the Os input layer (Os/Win64/Input.cpp) and the game window behind it.
//
// Windows turns window messages into OSINPUT events in OsWindowProc. Here the game view (WowGameView) does the same
// for AppKit events, with the same parameters:
//
//   KEY_DOWN / KEY_UP          KEY, repeat count
//   CHAR                       UTF-16 code unit, repeat count
//   MOUSE_DOWN / MOUSE_UP      MOUSEBUTTON, x, y
//   MOUSE_MOVE                 0, x, y
//   MOUSE_MOVE_RELATIVE        0, dx, dy
//   MOUSE_WHEEL                delta (120 per notch), x, y
//   FOCUS                      active
//   SIZE                       width, height (from OsInputGet, like Windows)
//   CLOSE, CAPTURE_CHANGED     -, x, y
//
// OsQueuePut stamps the time into the last parameter. Positions are content-area points, top left origin.
//
// Keys map from hardware key codes: special keys through a table, character keys through the current keyboard layout
// (UCKeyTranslate with no modifiers), which is what MapVirtualKey gives Windows: letters as upper case, punctuation
// unshifted. The digit row maps by position to '0'..'9', as VK_0..VK_9 do. Command isn't a game key; Command shortcuts
// go to the menu (Cmd+Q asks the game to close).

#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>
#include <cmath>
#include <cstdint>
#include "Engine/Source/Event/Types.h"
#include "Engine/Source/Os/Gui.h"
#include "Engine/Source/Os/Input.h"
#include "Engine/Source/Os/Joystick.h"
#include "Engine/Source/Os/Mac/OsMacWindow.h"
#include "Engine/Source/Os/TimeManager.h"
#include "Engine/Source/Storm/H/storm.h"

static int32_t s_appActive = 1;
static int32_t s_queueHead;
static int32_t s_queueTail;
static OSEVENT s_queue[16];
static int32_t s_mouseSpeed = 10;
static uint32_t s_osButtonState;
static RECT s_defaultWindowRect;
static int32_t s_windowSize;
static OS_MOUSE_MODE s_osMouseMode;
static int32_t s_WindowResizeLock;
static void (*s_eventPollProc)();
static C2iVector s_mousePos;
static bool s_keyDown[KEY_LAST];

@class WowGameView;

namespace {

NSWindow* GameWindow() {
    return (__bridge NSWindow*)OsGuiGetWindow(0);
}

WowGameView* GameView() {
    NSWindow* window = GameWindow();
    return window ? (WowGameView*)window.contentView : nil;
}

NSPoint ViewPoint(NSView* view, NSPoint windowPoint) {
    // The view is flipped, so this is already top-left based.
    return [view convertPoint:windowPoint fromView:nil];
}

int32_t SpecialKey(uint16_t keyCode, KEY* key) {
    switch (keyCode) {
        case kVK_Escape: *key = KEY_ESCAPE; return 1;
        case kVK_Return: *key = KEY_ENTER; return 1;
        case kVK_ANSI_KeypadEnter: *key = KEY_ENTER; return 1;
        case kVK_Delete: *key = KEY_BACKSPACE; return 1;
        case kVK_Tab: *key = KEY_TAB; return 1;
        case kVK_Space: *key = KEY_SPACE; return 1;
        case kVK_LeftArrow: *key = KEY_LEFT; return 1;
        case kVK_UpArrow: *key = KEY_UP; return 1;
        case kVK_RightArrow: *key = KEY_RIGHT; return 1;
        case kVK_DownArrow: *key = KEY_DOWN; return 1;
        case kVK_Help: *key = KEY_INSERT; return 1;
        case kVK_ForwardDelete: *key = KEY_DELETE; return 1;
        case kVK_Home: *key = KEY_HOME; return 1;
        case kVK_End: *key = KEY_END; return 1;
        case kVK_PageUp: *key = KEY_PAGEUP; return 1;
        case kVK_PageDown: *key = KEY_PAGEDOWN; return 1;
        case kVK_CapsLock: *key = KEY_CAPSLOCK; return 1;
        case kVK_ANSI_KeypadClear: *key = KEY_NUMLOCK; return 1;
        case kVK_ANSI_Keypad0: *key = KEY_NUMPAD0; return 1;
        case kVK_ANSI_Keypad1: *key = KEY_NUMPAD1; return 1;
        case kVK_ANSI_Keypad2: *key = KEY_NUMPAD2; return 1;
        case kVK_ANSI_Keypad3: *key = KEY_NUMPAD3; return 1;
        case kVK_ANSI_Keypad4: *key = KEY_NUMPAD4; return 1;
        case kVK_ANSI_Keypad5: *key = KEY_NUMPAD5; return 1;
        case kVK_ANSI_Keypad6: *key = KEY_NUMPAD6; return 1;
        case kVK_ANSI_Keypad7: *key = KEY_NUMPAD7; return 1;
        case kVK_ANSI_Keypad8: *key = KEY_NUMPAD8; return 1;
        case kVK_ANSI_Keypad9: *key = KEY_NUMPAD9; return 1;
        case kVK_ANSI_KeypadPlus: *key = KEY_NUMPAD_PLUS; return 1;
        case kVK_ANSI_KeypadMinus: *key = KEY_NUMPAD_MINUS; return 1;
        case kVK_ANSI_KeypadMultiply: *key = KEY_NUMPAD_MULTIPLY; return 1;
        case kVK_ANSI_KeypadDivide: *key = KEY_NUMPAD_DIVIDE; return 1;
        case kVK_ANSI_KeypadDecimal: *key = KEY_NUMPAD_DECIMAL; return 1;
        case kVK_ANSI_KeypadEquals: *key = KEY_NUMPAD_EQUALS; return 1;
        case kVK_F1: *key = KEY_F1; return 1;
        case kVK_F2: *key = KEY_F2; return 1;
        case kVK_F3: *key = KEY_F3; return 1;
        case kVK_F4: *key = KEY_F4; return 1;
        case kVK_F5: *key = KEY_F5; return 1;
        case kVK_F6: *key = KEY_F6; return 1;
        case kVK_F7: *key = KEY_F7; return 1;
        case kVK_F8: *key = KEY_F8; return 1;
        case kVK_F9: *key = KEY_F9; return 1;
        case kVK_F10: *key = KEY_F10; return 1;
        case kVK_F11: *key = KEY_F11; return 1;
        case kVK_F12: *key = KEY_F12; return 1;
        // F13 sits where Print Screen is on PC keyboards; the KEY enum aliases the two.
        case kVK_F13: *key = KEY_PRINTSCREEN; return 1;
        case kVK_F14: *key = KEY_F14; return 1;
        case kVK_F15: *key = KEY_F15; return 1;
        case kVK_F16: *key = KEY_F16; return 1;
        case kVK_F17: *key = KEY_F17; return 1;
        case kVK_F18: *key = KEY_F18; return 1;
        case kVK_F19: *key = KEY_F19; return 1;
        case kVK_ANSI_0: *key = KEY_0; return 1;
        case kVK_ANSI_1: *key = KEY_1; return 1;
        case kVK_ANSI_2: *key = KEY_2; return 1;
        case kVK_ANSI_3: *key = KEY_3; return 1;
        case kVK_ANSI_4: *key = KEY_4; return 1;
        case kVK_ANSI_5: *key = KEY_5; return 1;
        case kVK_ANSI_6: *key = KEY_6; return 1;
        case kVK_ANSI_7: *key = KEY_7; return 1;
        case kVK_ANSI_8: *key = KEY_8; return 1;
        case kVK_ANSI_9: *key = KEY_9; return 1;
        default: return 0;
    }
}

int32_t LayoutKey(uint16_t keyCode, KEY* key) {
    TISInputSourceRef source = TISCopyCurrentKeyboardLayoutInputSource();

    if (!source) {
        return 0;
    }

    CFDataRef layoutData = static_cast<CFDataRef>(TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData));
    int32_t result = 0;

    if (layoutData) {
        const UCKeyboardLayout* layout = reinterpret_cast<const UCKeyboardLayout*>(CFDataGetBytePtr(layoutData));
        UInt32 deadKeyState = 0;
        UniChar characters[4];
        UniCharCount length = 0;

        if (UCKeyTranslate(layout, keyCode, kUCKeyActionDown, 0, LMGetKbdType(), kUCKeyTranslateNoDeadKeysMask, &deadKeyState, 4, &length, characters) == noErr && length == 1) {
            uint32_t ch = characters[0];

            if (ch >= 'a' && ch <= 'z') {
                ch -= 'a' - 'A';
            }

            if (ch > 0x20 && ch <= 0xFF) {
                *key = static_cast<KEY>(ch);
                result = 1;
            }
        }
    }

    CFRelease(source);

    return result;
}

int32_t ConvertKeyCode(uint16_t keyCode, KEY* key) {
    return SpecialKey(keyCode, key) || LayoutKey(keyCode, key);
}

void PutKey(KEY key, bool down) {
    if (key >= 0 && key < KEY_LAST) {
        s_keyDown[key] = down;
    }

    OsQueuePut(down ? OS_INPUT_KEY_DOWN : OS_INPUT_KEY_UP, key, 1, 0, 0);
}

MOUSEBUTTON ConvertButton(NSEvent* event) {
    switch (event.type) {
        case NSEventTypeLeftMouseDown:
        case NSEventTypeLeftMouseUp:
            return MOUSE_BUTTON_LEFT;
        case NSEventTypeRightMouseDown:
        case NSEventTypeRightMouseUp:
            return MOUSE_BUTTON_RIGHT;
        default:
            switch (event.buttonNumber) {
                case 2:
                    return MOUSE_BUTTON_MIDDLE;
                case 3:
                    return MOUSE_BUTTON_XBUTTON1;
                case 4:
                    return MOUSE_BUTTON_XBUTTON2;
                default:
                    return MOUSE_BUTTON_NONE;
            }
    }
}

void SaveMouse(const C2iVector& point) {
    if (s_osMouseMode == OS_MOUSE_MODE_RELATIVE) {
        return;
    }

    s_mousePos = point;
}

void WarpToViewPoint(NSView* view, NSPoint point) {
    NSWindow* window = view.window;

    if (!window) {
        return;
    }

    NSPoint windowPoint = [view convertPoint:point toView:nil];
    NSPoint screenPoint = [window convertPointToScreen:windowPoint];

    // Quartz display coordinates put the origin at the top left of the main screen.
    CGFloat mainHeight = NSScreen.screens.firstObject.frame.size.height;
    CGWarpMouseCursorPosition(CGPointMake(screenPoint.x, mainHeight - screenPoint.y));
    CGAssociateMouseAndMouseCursorPosition(s_osMouseMode != OS_MOUSE_MODE_RELATIVE);
}

// Relative mode locks the cursor in place (CGAssociateMouseAndMouseCursorPosition) and reports deltas. It is suspended
// while the app is in the background so the user keeps a working cursor.
void ApplyMouseLock() {
    bool lock = s_osMouseMode == OS_MOUSE_MODE_RELATIVE && s_appActive;
    static bool s_locked;

    if (lock == s_locked) {
        return;
    }

    s_locked = lock;
    CGAssociateMouseAndMouseCursorPosition(!lock);

    if (lock) {
        [NSCursor hide];
    } else {
        [NSCursor unhide];
    }
}

}

@interface WowGameView : NSView <NSTextInputClient, NSWindowDelegate>
@end

@implementation WowGameView {
    NSMutableAttributedString* m_markedText;
    double m_wheelRemainder;
    double m_relativeX;
    double m_relativeY;
    NSTimer* m_pollTimer;
}

- (BOOL)isFlipped {
    return YES;
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event {
    return YES;
}

- (C2iVector)pointFor:(NSEvent*)event {
    if (s_osMouseMode == OS_MOUSE_MODE_RELATIVE) {
        return s_mousePos;
    }

    NSPoint point = ViewPoint(self, event.locationInWindow);
    C2iVector result;
    result.x = static_cast<int32_t>(std::floor(point.x));
    result.y = static_cast<int32_t>(std::floor(point.y));
    return result;
}

// Keyboard

- (void)keyDown:(NSEvent*)event {
    if (event.modifierFlags & NSEventModifierFlagCommand) {
        [super keyDown:event];
        return;
    }

    KEY key;

    if (ConvertKeyCode(event.keyCode, &key)) {
        PutKey(key, true);
    }

    // Characters (and dead-key composition) come back through insertText:.
    [self interpretKeyEvents:@[ event ]];
}

- (void)keyUp:(NSEvent*)event {
    KEY key;

    if (ConvertKeyCode(event.keyCode, &key) && (key < 0 || key >= KEY_LAST || s_keyDown[key])) {
        PutKey(key, false);
    }
}

- (void)flagsChanged:(NSEvent*)event {
    NSEventModifierFlags flags = event.modifierFlags;
    KEY key;
    bool down;

    // Device-dependent bits (NX_DEVICE*KEYMASK) tell left from right.
    switch (event.keyCode) {
        case kVK_Shift:
            key = KEY_LSHIFT;
            down = flags & 0x00000002;
            break;
        case kVK_RightShift:
            key = KEY_RSHIFT;
            down = flags & 0x00000004;
            break;
        case kVK_Control:
            key = KEY_LCONTROL;
            down = flags & 0x00000001;
            break;
        case kVK_RightControl:
            key = KEY_RCONTROL;
            down = flags & 0x00002000;
            break;
        case kVK_Option:
            key = KEY_LALT;
            down = flags & 0x00000020;
            break;
        case kVK_RightOption:
            key = KEY_RALT;
            down = flags & 0x00000040;
            break;
        case kVK_CapsLock:
            // macOS reports the lock state, not the key; Windows sends a press and a release.
            PutKey(KEY_CAPSLOCK, true);
            PutKey(KEY_CAPSLOCK, false);
            return;
        default:
            return;
    }

    PutKey(key, down);
}

- (void)insertText:(id)string replacementRange:(NSRange)replacementRange {
    NSString* text = [string isKindOfClass:[NSAttributedString class]] ? [(NSAttributedString*)string string] : (NSString*)string;
    m_markedText = nil;

    for (NSUInteger i = 0; i < text.length; i++) {
        unichar ch = [text characterAtIndex:i];

        // Like WM_CHAR: control characters are left to the key events, and AppKit's function-key range is skipped.
        if (ch < 0x20 || ch == 0x7F || (ch >= 0xF700 && ch <= 0xF8FF)) {
            continue;
        }

        OsQueuePut(OS_INPUT_CHAR, ch, 1, 0, 0);
    }
}

- (void)doCommandBySelector:(SEL)selector {
    // Enter, Backspace, arrows and the like already went out as key events.
}

- (void)setMarkedText:(id)string selectedRange:(NSRange)selectedRange replacementRange:(NSRange)replacementRange {
    m_markedText = [string isKindOfClass:[NSAttributedString class]] ? [[NSMutableAttributedString alloc] initWithAttributedString:string] : [[NSMutableAttributedString alloc] initWithString:string];
}

- (void)unmarkText {
    m_markedText = nil;
}

- (NSRange)selectedRange {
    return NSMakeRange(NSNotFound, 0);
}

- (NSRange)markedRange {
    return m_markedText.length ? NSMakeRange(0, m_markedText.length) : NSMakeRange(NSNotFound, 0);
}

- (BOOL)hasMarkedText {
    return m_markedText.length != 0;
}

- (NSAttributedString*)attributedSubstringForProposedRange:(NSRange)range actualRange:(NSRangePointer)actualRange {
    return nil;
}

- (NSArray<NSAttributedStringKey>*)validAttributesForMarkedText {
    return @[];
}

- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actualRange {
    // Composition windows open at the bottom left of the game window.
    return [self.window convertRectToScreen:NSMakeRect(0, 0, 0, 0)];
}

- (NSUInteger)characterIndexForPoint:(NSPoint)point {
    return NSNotFound;
}

// Mouse

- (void)mouseButton:(NSEvent*)event down:(bool)down {
    MOUSEBUTTON button = ConvertButton(event);

    if (button == MOUSE_BUTTON_NONE) {
        return;
    }

    if (down) {
        s_osButtonState |= button;
    } else {
        s_osButtonState &= ~button;
    }

    C2iVector point = [self pointFor:event];
    OsQueuePut(down ? OS_INPUT_MOUSE_DOWN : OS_INPUT_MOUSE_UP, button, point.x, point.y, 0);
}

- (void)mouseDown:(NSEvent*)event {
    [self mouseButton:event down:true];
}

- (void)mouseUp:(NSEvent*)event {
    [self mouseButton:event down:false];
}

- (void)rightMouseDown:(NSEvent*)event {
    [self mouseButton:event down:true];
}

- (void)rightMouseUp:(NSEvent*)event {
    [self mouseButton:event down:false];
}

- (void)otherMouseDown:(NSEvent*)event {
    [self mouseButton:event down:true];
}

- (void)otherMouseUp:(NSEvent*)event {
    [self mouseButton:event down:false];
}

- (void)mouseMovedTo:(NSEvent*)event {
    if (s_osMouseMode == OS_MOUSE_MODE_RELATIVE) {
        m_relativeX += event.deltaX;
        m_relativeY += event.deltaY;
        int32_t dx = static_cast<int32_t>(m_relativeX);
        int32_t dy = static_cast<int32_t>(m_relativeY);

        if (dx || dy) {
            m_relativeX -= dx;
            m_relativeY -= dy;
            OsQueuePut(OS_INPUT_MOUSE_MOVE_RELATIVE, 0, dx, dy, 0);
        }

        return;
    }

    C2iVector point = [self pointFor:event];
    OsQueuePut(OS_INPUT_MOUSE_MOVE, 0, point.x, point.y, 0);
    SaveMouse(point);
}

- (void)mouseMoved:(NSEvent*)event {
    [self mouseMovedTo:event];
}

- (void)mouseDragged:(NSEvent*)event {
    [self mouseMovedTo:event];
}

- (void)rightMouseDragged:(NSEvent*)event {
    [self mouseMovedTo:event];
}

- (void)otherMouseDragged:(NSEvent*)event {
    [self mouseMovedTo:event];
}

- (void)scrollWheel:(NSEvent*)event {
    // WHEEL_DELTA (120) per notch. Trackpads report pixels; about ten of them make a notch.
    double delta = event.hasPreciseScrollingDeltas ? event.scrollingDeltaY * 12.0 : event.scrollingDeltaY * 120.0;
    m_wheelRemainder += delta;
    int32_t notches = static_cast<int32_t>(m_wheelRemainder / 120.0);

    if (!notches) {
        return;
    }

    m_wheelRemainder -= notches * 120.0;
    C2iVector point = [self pointFor:event];
    OsQueuePut(OS_INPUT_MOUSE_WHEEL, notches * 120, point.x, point.y, 0);
}

// Window

- (BOOL)windowShouldClose:(NSWindow*)window {
    OsQueuePut(OS_INPUT_CLOSE, 0, 0, 0, 0);
    return NO;
}

- (void)windowDidResize:(NSNotification*)notification {
    s_windowSize = 1;
}

- (void)windowDidChangeBackingProperties:(NSNotification*)notification {
    s_windowSize = 1;
}

- (void)setActive:(int32_t)active {
    s_appActive = active;

    if (!active && s_osButtonState) {
        // Losing the window mid-drag ends the capture, as WM_CAPTURECHANGED does.
        s_osButtonState = 0;
        NSPoint point = ViewPoint(self, self.window.mouseLocationOutsideOfEventStream);
        OsQueuePut(OS_INPUT_CAPTURE_CHANGED, 0, static_cast<int32_t>(point.x), static_cast<int32_t>(point.y), 0);
    }

    s_osButtonState = 0;
    OsQueuePut(OS_INPUT_FOCUS, active, 0, 0, 0);
    ApplyMouseLock();
}

- (void)windowDidBecomeKey:(NSNotification*)notification {
    [self setActive:1];
}

- (void)windowDidResignKey:(NSNotification*)notification {
    [self setActive:0];
}

- (void)windowDidMiniaturize:(NSNotification*)notification {
    [self setActive:0];
}

// Live resizing runs a modal loop; keep the game ticking through it, as the Windows timer does during drags.
- (void)windowWillStartLiveResize:(NSNotification*)notification {
    if (s_eventPollProc && !m_pollTimer) {
        m_pollTimer = [NSTimer timerWithTimeInterval:0.033 repeats:YES block:^(NSTimer* timer) {
            if (s_eventPollProc) {
                s_eventPollProc();
            }
        }];
        [[NSRunLoop currentRunLoop] addTimer:m_pollTimer forMode:NSEventTrackingRunLoopMode];
    }
}

- (void)windowDidEndLiveResize:(NSNotification*)notification {
    [m_pollTimer invalidate];
    m_pollTimer = nil;
    s_windowSize = 1;
}

- (NSSize)windowWillResize:(NSWindow*)window toSize:(NSSize)size {
    return s_WindowResizeLock ? window.frame.size : size;
}

@end

@interface WowApplicationDelegate : NSObject <NSApplicationDelegate>
@end

@implementation WowApplicationDelegate

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
    // Quitting goes through the game, which shuts down in its own time.
    OsQueuePut(OS_INPUT_CLOSE, 0, 0, 0, 0);
    return NSTerminateCancel;
}

@end

void OsMacInitializeApplication() {
    static WowApplicationDelegate* s_delegate;

    if (s_delegate) {
        return;
    }

    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        s_delegate = [[WowApplicationDelegate alloc] init];
        NSApp.delegate = s_delegate;

        NSString* name = NSProcessInfo.processInfo.processName;
        NSMenu* menuBar = [[NSMenu alloc] init];

        NSMenuItem* appItem = [[NSMenuItem alloc] init];
        NSMenu* appMenu = [[NSMenu alloc] init];
        [appMenu addItemWithTitle:[@"Hide " stringByAppendingString:name] action:@selector(hide:) keyEquivalent:@"h"];
        [appMenu addItem:[NSMenuItem separatorItem]];
        [appMenu addItemWithTitle:[@"Quit " stringByAppendingString:name] action:@selector(terminate:) keyEquivalent:@"q"];
        appItem.submenu = appMenu;
        [menuBar addItem:appItem];

        NSMenuItem* windowItem = [[NSMenuItem alloc] init];
        NSMenu* windowMenu = [[NSMenu alloc] initWithTitle:@"Window"];
        [windowMenu addItemWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
        NSMenuItem* fullScreen = [windowMenu addItemWithTitle:@"Enter Full Screen" action:@selector(toggleFullScreen:) keyEquivalent:@"f"];
        fullScreen.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagControl;
        windowItem.submenu = windowMenu;
        [menuBar addItem:windowItem];

        NSApp.mainMenu = menuBar;
        NSApp.windowsMenu = windowMenu;
        [NSApp finishLaunching];
    }
}

void* OsMacCreateGameWindow(const char* title, int32_t x, int32_t y, int32_t width, int32_t height) {
    OsMacInitializeApplication();

    @autoreleasepool {
        NSRect content = NSMakeRect(0, 0, width, height);
        NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
        NSWindow* window = [[NSWindow alloc] initWithContentRect:content styleMask:style backing:NSBackingStoreBuffered defer:NO];

        if (!window) {
            return nullptr;
        }

        window.releasedWhenClosed = NO;
        window.title = [NSString stringWithUTF8String:title ? title : ""];
        window.acceptsMouseMovedEvents = YES;
        window.collectionBehavior = NSWindowCollectionBehaviorFullScreenPrimary;

        WowGameView* view = [[WowGameView alloc] initWithFrame:content];
        view.wantsLayer = YES;
        view.layer.backgroundColor = NSColor.blackColor.CGColor;
        window.contentView = view;
        window.delegate = view;
        [window makeFirstResponder:view];

        if (x < 0 || y < 0) {
            [window center];
        } else {
            NSRect screen = NSScreen.mainScreen.frame;
            [window setFrameTopLeftPoint:NSMakePoint(screen.origin.x + x, screen.origin.y + screen.size.height - y)];
        }

        [window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];

        // The window owns itself until OsMacDestroyGameWindow.
        return (__bridge_retained void*)window;
    }
}

void* OsMacGetGameView(void* window) {
    return (__bridge void*)((__bridge NSWindow*)window).contentView;
}

float OsMacGetBackingScale(void* window) {
    return static_cast<float>(((__bridge NSWindow*)window).backingScaleFactor);
}

void OsMacDestroyGameWindow(void* window) {
    if (!window) {
        return;
    }

    if (OsGuiGetWindow(0) == window) {
        OsInputSetMouseMode(OS_MOUSE_MODE_NORMAL);
        OsGuiSetGxWindow(nullptr);
    }

    NSWindow* object = (__bridge_transfer NSWindow*)window;
    object.delegate = nil;
    [object close];
}

void OsQueuePut(OSINPUT id, int32_t param0, int32_t param1, int32_t param2, int32_t param3) {
    int32_t next = s_queueTail == 15 ? 0 : s_queueTail + 1;

    if (next == s_queueHead) {
        s_queueHead = s_queueHead == 15 ? 0 : s_queueHead + 1;
    }

    OSEVENT& event = s_queue[s_queueTail];
    event.id = id;
    event.param[0] = param0;
    event.param[1] = param1;
    event.param[2] = param2;
    event.param[3] = OsGetAsyncTimeMs();

    s_queueTail = next;
}

int32_t OsQueueGet(OSINPUT* id, int32_t* param0, int32_t* param1, int32_t* param2, int32_t* param3) {
    if (s_queueHead == s_queueTail) {
        return 0;
    }

    OSEVENT& event = s_queue[s_queueHead];
    *id = event.id;
    *param0 = event.param[0];
    *param1 = event.param[1];
    *param2 = event.param[2];
    *param3 = event.param[3];

    s_queueHead = s_queueHead == 15 ? 0 : s_queueHead + 1;

    return 1;
}

void OsInputSetWindowResizeLock(int32_t resizeLock) {
    s_WindowResizeLock = resizeLock;
}

float OsInputGetMouseSpeed() {
    return s_mouseSpeed * 0.1f;
}

int32_t OsInputIsKeyDown(KEY key) {
    NSEventModifierFlags flags = [NSEvent modifierFlags];

    switch (key) {
        case KEY_LSHIFT:
            return (flags & 0x00000002) != 0;
        case KEY_RSHIFT:
            return (flags & 0x00000004) != 0;
        case KEY_LCONTROL:
            return (flags & 0x00000001) != 0;
        case KEY_RCONTROL:
            return (flags & 0x00002000) != 0;
        case KEY_LALT:
            return (flags & 0x00000020) != 0;
        case KEY_RALT:
            return (flags & 0x00000040) != 0;
        case KEY_CAPSLOCK:
            return (flags & NSEventModifierFlagCapsLock) != 0;
        default:
            return key >= 0 && key < KEY_LAST && s_keyDown[key];
    }
}

int32_t OsInputIsMouseButtonDown(MOUSEBUTTON button) {
    NSUInteger pressed = [NSEvent pressedMouseButtons];

    switch (button) {
        case MOUSE_BUTTON_LEFT:
            return (pressed & 0x1) != 0;
        case MOUSE_BUTTON_RIGHT:
            return (pressed & 0x2) != 0;
        case MOUSE_BUTTON_MIDDLE:
            return (pressed & 0x4) != 0;
        case MOUSE_BUTTON_XBUTTON1:
            return (pressed & 0x8) != 0;
        case MOUSE_BUTTON_XBUTTON2:
            return (pressed & 0x10) != 0;
        default:
            return 0;
    }
}

void OsInputInitialize() {
    // Windows saves the system mouse speed here to restore it later; the Mac port never changes system settings.
    s_mouseSpeed = 10;
}

void OsInputDestroy() {
    OsInputSetMouseMode(OS_MOUSE_MODE_NORMAL);
}

void OsInputSetEventPollProc(void (*pollProc)()) {
    s_eventPollProc = pollProc;
}

int32_t OsInputGet(OSINPUT* id, int32_t* param0, int32_t* param1, int32_t* param2, int32_t* param3) {
    *id = static_cast<OSINPUT>(-1);

    NSWindow* window = GameWindow();

    if (s_windowSize && window && !window.miniaturized) {
        s_windowSize = 0;

        NSSize size = window.contentView.bounds.size;
        int32_t width = static_cast<int32_t>(size.width);
        int32_t height = static_cast<int32_t>(size.height);

        if (width != s_defaultWindowRect.right || height != s_defaultWindowRect.bottom) {
            s_defaultWindowRect.left = 0;
            s_defaultWindowRect.top = 0;
            s_defaultWindowRect.right = width;
            s_defaultWindowRect.bottom = height;

            *id = OS_INPUT_SIZE;
            *param0 = width;
            *param1 = height;
            *param2 = 0;
            *param3 = 0;

            return 1;
        }
    }

    if (s_queueHead == s_queueTail) {
        OsPollJoysticks(s_appActive);

        @autoreleasepool {
            while (s_queueHead == s_queueTail) {
                NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate distantPast] inMode:NSDefaultRunLoopMode dequeue:YES];

                if (!event) {
                    return 0;
                }

                [NSApp sendEvent:event];
            }
        }
    }

    OsQueueGet(id, param0, param1, param2, param3);

    return 1;
}

void OsInputSetMouseMode(OS_MOUSE_MODE mode) {
    if (mode >= OS_MOUSE_MODES) {
        SErrSetLastError(STORM_ERROR_INVALID_PARAMETER);
        return;
    }

    if (mode == s_osMouseMode) {
        return;
    }

    WowGameView* view = GameView();

    if (mode == OS_MOUSE_MODE_RELATIVE) {
        if (view) {
            NSPoint point = ViewPoint(view, view.window.mouseLocationOutsideOfEventStream);
            C2iVector saved;
            saved.x = static_cast<int32_t>(point.x);
            saved.y = static_cast<int32_t>(point.y);
            SaveMouse(saved);
        }

        s_osMouseMode = mode;
    } else {
        s_osMouseMode = OS_MOUSE_MODE_NORMAL;

        // Put the cursor back where relative mode started, as RestoreMouse does.
        if (view) {
            WarpToViewPoint(view, NSMakePoint(s_mousePos.x, s_mousePos.y));
        }
    }

    ApplyMouseLock();
}

void OsInputSetMouseSpeed(float speed) {
    float clamped = speed < 0.1f ? 0.1f : (speed >= 2.0f ? 2.0f : speed);
    s_mouseSpeed = static_cast<int32_t>(clamped * 10.0f);
}

void OsInputGetMousePosition(int32_t* x, int32_t* y) {
    WowGameView* view = GameView();
    C2iVector point = { 0, 0 };

    if (view) {
        NSPoint location = ViewPoint(view, view.window.mouseLocationOutsideOfEventStream);
        point.x = static_cast<int32_t>(location.x);
        point.y = static_cast<int32_t>(location.y);
    }

    SaveMouse(point);

    if (x) {
        *x = point.x;
    }

    if (y) {
        *y = point.y;
    }
}

void OsInputSetMousePosition(int32_t x, int32_t y) {
    C2iVector point;
    point.x = x;
    point.y = y;
    SaveMouse(point);

    WowGameView* view = GameView();

    if (view && s_osMouseMode != OS_MOUSE_MODE_RELATIVE) {
        WarpToViewPoint(view, NSMakePoint(x, y));
    }
}

int32_t OsGetDefaultWindowRect(tagRECT* rect) {
    if (!rect) {
        SErrSetLastError(STORM_ERROR_INVALID_PARAMETER);
        return 0;
    }

    if (!s_defaultWindowRect.right || !s_defaultWindowRect.bottom) {
        NSWindow* window = GameWindow();

        if (!window) {
            return 0;
        }

        NSSize size = window.contentView.bounds.size;
        s_defaultWindowRect.left = 0;
        s_defaultWindowRect.top = 0;
        s_defaultWindowRect.right = static_cast<int32_t>(size.width);
        s_defaultWindowRect.bottom = static_cast<int32_t>(size.height);
    }

    *rect = s_defaultWindowRect;

    return 1;
}

intptr_t OsWindowProc(void* window, uint32_t message, uintptr_t wparam, intptr_t lparam) {
    // There are no window messages on macOS: the game view queues input itself. The Gx device still receives this as
    // its window procedure, so it stays defined.
    return 0;
}

void OsInputEnableSecureMode(int32_t enable) {
}
