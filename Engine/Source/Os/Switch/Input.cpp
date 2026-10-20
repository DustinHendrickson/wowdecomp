// Nintendo Switch implementation of the Os input layer (Os/Win64/Input.cpp) and the game window behind it
// (Os/Mac/OsMacWindow.h). Events carry the same parameters as on Windows and macOS (see Os/Mac/Input.mm):
//
//   KEY_DOWN / KEY_UP          KEY, repeat count
//   CHAR                       UTF-16 code unit, repeat count
//   MOUSE_DOWN / MOUSE_UP      MOUSEBUTTON, x, y
//   MOUSE_MOVE                 0, x, y
//   MOUSE_MOVE_RELATIVE        0, dx, dy
//   MOUSE_WHEEL                delta (120 per notch), x, y
//   FOCUS                      active
//   SIZE                       width, height
//
// The window is the screen, rendered at 1280x720, or 1920x1080 when docked and chosen in the video options; the system
// scales it to the display. The game sees a keyboard and a mouse; everything else is turned into those:
//
// - A USB keyboard and mouse work as they do on a computer.
// - The touch screen is the mouse: a tap is a left click, a drag a left drag, and a touch held still for half a second
//   a right click.
// - A controller (handheld, Joy-Cons or Pro Controller) has two modes, switched with Minus or a right stick click:
//
//   Play mode (the default)            Cursor mode
//   Left stick   move (W S, Q E)       Left stick   move
//   Right stick  look (right-drag)     Right stick  move the cursor
//   B            jump (Space)          A / ZR       left click (hold to drag)
//   A X Y        1 2 3                 B / ZL       right click
//   D-pad        4 5 6 7 (> ^ < v)     D-pad ^ v    mouse wheel
//   L R          8, Tab (target)       Y            Enter (open chat)
//   ZL ZR (hold) Shift, Ctrl
//
//   In both: L3 is autorun (Num Lock), Plus is Escape.
//
//   While a text box has the keyboard focus, the face buttons and D-pad edit text instead: A is Enter, B Escape, X Tab,
//   Y opens the software keyboard, and the D-pad gives the arrow keys. Movement keys aren't sent then, so they don't
//   type letters.
//
// The software keyboard opens by itself when a text box gains the focus (OsIMEEnable, OsInputEnableSecureMode). Its text
// goes through the clipboard: Ctrl+A, Ctrl+C copy the box's text in as the keyboard's starting text, and Ctrl+A, Ctrl+V
// paste the result back (password boxes, which don't copy, get the result typed as characters).

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include "Engine/Source/Event/Types.h"
#include "Engine/Source/Os/Gui.h"
#include "Engine/Source/Os/Input.h"
#include "Engine/Source/Os/Joystick.h"
#include "Engine/Source/Os/Mac/OsMacWindow.h"
#include "Engine/Source/Os/OsClipboard.h"
#include "Engine/Source/Os/Switch/NxInput.h"
#include "Engine/Source/Os/Switch/SwitchInput.h"
#include "Engine/Source/Os/TimeManager.h"
#include "Engine/Source/Storm/H/storm.h"

namespace {

// Larger than the 16 events Windows keeps: one poll can produce a burst (a pasted password typed as characters).
const int32_t kQueueSize = 256;

int32_t s_appActive = 1;
int32_t s_queueHead;
int32_t s_queueTail;
OSEVENT s_queue[kQueueSize];
int32_t s_mouseSpeed = 10;
uint32_t s_osButtonState;
RECT s_defaultWindowRect;
int32_t s_windowSize;
OS_MOUSE_MODE s_osMouseMode;
void (*s_eventPollProc)();
C2iVector s_mousePos;
bool s_keyDown[KEY_LAST];

// The window: the screen, at a fixed size while the game runs.
void* s_window;
int32_t s_contentWidth;
int32_t s_contentHeight;
OsMacWindowListener s_listener;
void* s_listenerParam;

bool s_needPoll = true;
bool s_closeSent;
uint64_t s_lastPollMs;

// Controller state.
bool s_cursorMode;
NxInputState s_previous;

// What each controller button did when it went down, undone when it comes up whatever the mode is by then.
enum OutputType {
    Output_None,
    Output_Key,
    Output_Mouse,
    Output_Wheel,
    Output_ToggleCursor,
    Output_Keyboard,
};

struct Output {
    OutputType type;
    int32_t value;
};

const uint64_t kButtons[] = {
    NxButton_A, NxButton_B, NxButton_X, NxButton_Y, NxButton_StickL, NxButton_StickR, NxButton_L, NxButton_R,
    NxButton_ZL, NxButton_ZR, NxButton_Plus, NxButton_Minus, NxButton_Left, NxButton_Up, NxButton_Right, NxButton_Down,
};

Output s_outputs[16];

// Left stick movement keys, with hysteresis so they don't flicker at the threshold.
const float kMovePress = 0.5f;
const float kMoveRelease = 0.35f;
bool s_moveForward;
bool s_moveBack;
bool s_strafeLeft;
bool s_strafeRight;

// Right stick.
const float kStickDeadzone = 0.15f;
// Screen heights per second at full deflection.
const float kCursorSpeed = 1.0f;
const float kLookSpeed = 1.2f;
// How long the right stick rests before a look ends.
const uint64_t kLookReleaseMs = 200;

bool s_looking;
uint64_t s_lookIdleSince;
C2iVector s_lookSavedPos;
float s_cursorRemainderX;
float s_cursorRemainderY;

// Touch.
const uint64_t kTouchHoldMs = 500;
const int32_t kTouchSlop = 12;

enum TouchState {
    Touch_None,
    Touch_Pending,
    Touch_Dragging,
    Touch_Done,
};

TouchState s_touchState;
uint64_t s_touchStartMs;
C2iVector s_touchStart;

// Software keyboard.
enum KeyboardState {
    Keyboard_Idle,
    Keyboard_Copying,
    Keyboard_Open,
};

bool s_textFocus;
bool s_secureFocus;
KeyboardState s_keyboardState;
uint32_t s_copyClipboardGeneration;

C2iVector ScreenCenter() {
    C2iVector center;
    center.x = s_contentWidth / 2;
    center.y = s_contentHeight / 2;
    return center;
}

void ClampToContent(C2iVector& point) {
    point.x = point.x < 0 ? 0 : (point.x >= s_contentWidth ? s_contentWidth - 1 : point.x);
    point.y = point.y < 0 ? 0 : (point.y >= s_contentHeight ? s_contentHeight - 1 : point.y);
}

void PutKey(KEY key, bool down) {
    if (key >= 0 && key < KEY_LAST) {
        if (s_keyDown[key] == down) {
            return;
        }

        s_keyDown[key] = down;
    }

    OsQueuePut(down ? OS_INPUT_KEY_DOWN : OS_INPUT_KEY_UP, key, 1, 0, 0);
}

void TapKey(KEY key) {
    OsQueuePut(OS_INPUT_KEY_DOWN, key, 1, 0, 0);
    OsQueuePut(OS_INPUT_KEY_UP, key, 1, 0, 0);
}

// Ctrl+<key>, as the editing shortcuts edit boxes know.
void ControlKey(KEY key) {
    bool control = s_keyDown[KEY_LCONTROL] || s_keyDown[KEY_RCONTROL];

    if (!control) {
        OsQueuePut(OS_INPUT_KEY_DOWN, KEY_LCONTROL, 1, 0, 0);
    }

    TapKey(key);

    if (!control) {
        OsQueuePut(OS_INPUT_KEY_UP, KEY_LCONTROL, 1, 0, 0);
    }
}

void MoveMouseTo(const C2iVector& point) {
    if (s_osMouseMode == OS_MOUSE_MODE_RELATIVE) {
        return;
    }

    if (point.x == s_mousePos.x && point.y == s_mousePos.y) {
        return;
    }

    s_mousePos = point;
    OsQueuePut(OS_INPUT_MOUSE_MOVE, 0, point.x, point.y, 0);
}

// Pointer movement in pixels: relative events while the game holds the mouse (camera look), otherwise the cursor moves.
void MoveMouseBy(int32_t dx, int32_t dy) {
    if (!dx && !dy) {
        return;
    }

    if (s_osMouseMode == OS_MOUSE_MODE_RELATIVE) {
        OsQueuePut(OS_INPUT_MOUSE_MOVE_RELATIVE, 0, dx, dy, 0);
        return;
    }

    C2iVector point = s_mousePos;
    point.x += dx;
    point.y += dy;
    ClampToContent(point);
    MoveMouseTo(point);
}

void PutMouseButton(MOUSEBUTTON button, bool down) {
    if (down == ((s_osButtonState & button) != 0)) {
        return;
    }

    if (down) {
        s_osButtonState |= button;
    } else {
        s_osButtonState &= ~button;
    }

    OsQueuePut(down ? OS_INPUT_MOUSE_DOWN : OS_INPUT_MOUSE_UP, button, s_mousePos.x, s_mousePos.y, 0);
}

void PutWheel(int32_t notches) {
    if (notches) {
        OsQueuePut(OS_INPUT_MOUSE_WHEEL, notches * 120, s_mousePos.x, s_mousePos.y, 0);
    }
}

void Notify(OsMacWindowEvent event) {
    if (s_listener) {
        s_listener(s_listenerParam, event);
    }
}

// USB keyboards: HID usage to KEY, and the characters a US layout types.

KEY HidKey(uint32_t usage) {
    if (usage >= 4 && usage <= 29) {
        return static_cast<KEY>(KEY_A + (usage - 4));
    }

    if (usage >= 30 && usage <= 38) {
        return static_cast<KEY>(KEY_1 + (usage - 30));
    }

    if (usage >= 58 && usage <= 69) {
        return static_cast<KEY>(KEY_F1 + (usage - 58));
    }

    if (usage >= 89 && usage <= 97) {
        return static_cast<KEY>(KEY_NUMPAD1 + (usage - 89));
    }

    switch (usage) {
        case 39: return KEY_0;
        case 40: return KEY_ENTER;
        case 41: return KEY_ESCAPE;
        case 42: return KEY_BACKSPACE;
        case 43: return KEY_TAB;
        case 44: return KEY_SPACE;
        case 45: return KEY_MINUS;
        case 46: return KEY_PLUS;
        case 47: return KEY_BRACKET_OPEN;
        case 48: return KEY_BRACKET_CLOSE;
        case 49: return KEY_BACKSLASH;
        case 51: return KEY_SEMICOLON;
        case 52: return KEY_APOSTROPHE;
        case 53: return KEY_TILDE;
        case 54: return KEY_COMMA;
        case 55: return KEY_PERIOD;
        case 56: return KEY_SLASH;
        case 57: return KEY_CAPSLOCK;
        case 70: return KEY_PRINTSCREEN;
        case 71: return KEY_SCROLLLOCK;
        case 72: return KEY_PAUSE;
        case 73: return KEY_INSERT;
        case 74: return KEY_HOME;
        case 75: return KEY_PAGEUP;
        case 76: return KEY_DELETE;
        case 77: return KEY_END;
        case 78: return KEY_PAGEDOWN;
        case 79: return KEY_RIGHT;
        case 80: return KEY_LEFT;
        case 81: return KEY_DOWN;
        case 82: return KEY_UP;
        case 83: return KEY_NUMLOCK;
        case 84: return KEY_NUMPAD_DIVIDE;
        case 85: return KEY_NUMPAD_MULTIPLY;
        case 86: return KEY_NUMPAD_MINUS;
        case 87: return KEY_NUMPAD_PLUS;
        case 88: return KEY_ENTER;
        case 98: return KEY_NUMPAD0;
        case 99: return KEY_NUMPAD_DECIMAL;
        case 103: return KEY_NUMPAD_EQUALS;
        case 104: return KEY_F13;
        case 105: return KEY_F14;
        case 106: return KEY_F15;
        case 107: return KEY_F16;
        case 108: return KEY_F17;
        case 109: return KEY_F18;
        case 110: return KEY_F19;
        case 224: return KEY_LCONTROL;
        case 225: return KEY_LSHIFT;
        case 226: return KEY_LALT;
        case 228: return KEY_RCONTROL;
        case 229: return KEY_RSHIFT;
        case 230: return KEY_RALT;
        default: return KEY_NONE;
    }
}

uint16_t HidChar(uint32_t usage, bool shift, bool capsLock) {
    if (usage >= 4 && usage <= 29) {
        bool upper = shift != capsLock;
        return static_cast<uint16_t>((upper ? 'A' : 'a') + (usage - 4));
    }

    static const char s_digits[] = "1234567890";
    static const char s_shiftedDigits[] = "!@#$%^&*()";

    if (usage >= 30 && usage <= 39) {
        return static_cast<uint16_t>((shift ? s_shiftedDigits : s_digits)[usage - 30]);
    }

    if (usage >= 89 && usage <= 98) {
        return static_cast<uint16_t>(usage == 98 ? '0' : '1' + (usage - 89));
    }

    switch (usage) {
        case 44: return ' ';
        case 45: return shift ? '_' : '-';
        case 46: return shift ? '+' : '=';
        case 47: return shift ? '{' : '[';
        case 48: return shift ? '}' : ']';
        case 49: return shift ? '|' : '\\';
        case 51: return shift ? ':' : ';';
        case 52: return shift ? '"' : '\'';
        case 53: return shift ? '~' : '`';
        case 54: return shift ? '<' : ',';
        case 55: return shift ? '>' : '.';
        case 56: return shift ? '?' : '/';
        case 84: return '/';
        case 85: return '*';
        case 86: return '-';
        case 87: return '+';
        case 99: return '.';
        default: return 0;
    }
}

bool KeyBit(const uint64_t* keys, uint32_t usage) {
    return (keys[usage / 64] >> (usage % 64)) & 1;
}

void PollKeyboard(const NxInputState& state) {
    bool shift = KeyBit(state.keys, 225) || KeyBit(state.keys, 229);
    bool capsLock = (state.keyModifiers & NxKeyboardModifier_CapsLock) != 0;
    bool commandKeys = KeyBit(state.keys, 224) || KeyBit(state.keys, 228) || KeyBit(state.keys, 226) || KeyBit(state.keys, 230);

    for (uint32_t usage = 4; usage < 256; usage++) {
        bool down = KeyBit(state.keys, usage);
        bool was = KeyBit(s_previous.keys, usage);

        if (down == was) {
            continue;
        }

        KEY key = HidKey(usage);

        if (key != KEY_NONE) {
            PutKey(key, down);
        }

        if (down && !commandKeys) {
            uint16_t ch = HidChar(usage, shift, capsLock);

            if (ch) {
                OsQueuePut(OS_INPUT_CHAR, ch, 1, 0, 0);
            }
        }
    }
}

void PollMouse(const NxInputState& state) {
    if (!state.mouseConnected) {
        return;
    }

    // Deltas in the 1280x720 space libnx reports, scaled to the content.
    int32_t dx = state.mouseDeltaX * s_contentWidth / 1280;
    int32_t dy = state.mouseDeltaY * s_contentHeight / 720;
    MoveMouseBy(dx, dy);

    static const struct {
        uint32_t bit;
        MOUSEBUTTON button;
    } s_buttons[] = {
        { 1u << 0, MOUSE_BUTTON_LEFT },
        { 1u << 1, MOUSE_BUTTON_RIGHT },
        { 1u << 2, MOUSE_BUTTON_MIDDLE },
        { 1u << 3, MOUSE_BUTTON_XBUTTON2 },
        { 1u << 4, MOUSE_BUTTON_XBUTTON1 },
    };

    for (const auto& entry : s_buttons) {
        bool down = (state.mouseButtons & entry.bit) != 0;
        bool was = s_previous.mouseConnected && (s_previous.mouseButtons & entry.bit) != 0;

        if (down != was) {
            PutMouseButton(entry.button, down);
        }
    }

    PutWheel(state.mouseWheel);
}

// Touch: tap, drag or hold, at the touched point.
void PollTouch(const NxInputState& state, uint64_t now) {
    if (state.touchCount == 0) {
        if (s_touchState == Touch_Pending) {
            // A tap.
            PutMouseButton(MOUSE_BUTTON_LEFT, true);
            PutMouseButton(MOUSE_BUTTON_LEFT, false);
        } else if (s_touchState == Touch_Dragging) {
            PutMouseButton(MOUSE_BUTTON_LEFT, false);
        }

        s_touchState = Touch_None;
        return;
    }

    C2iVector point;
    point.x = state.touches[0].x * s_contentWidth / 1280;
    point.y = state.touches[0].y * s_contentHeight / 720;
    ClampToContent(point);

    switch (s_touchState) {
        case Touch_None:
            s_touchState = Touch_Pending;
            s_touchStartMs = now;
            s_touchStart = point;
            MoveMouseTo(point);
            break;

        case Touch_Pending:
            if (std::abs(point.x - s_touchStart.x) > kTouchSlop || std::abs(point.y - s_touchStart.y) > kTouchSlop) {
                PutMouseButton(MOUSE_BUTTON_LEFT, true);
                MoveMouseTo(point);
                s_touchState = Touch_Dragging;
            } else if (now - s_touchStartMs >= kTouchHoldMs) {
                PutMouseButton(MOUSE_BUTTON_RIGHT, true);
                PutMouseButton(MOUSE_BUTTON_RIGHT, false);
                s_touchState = Touch_Done;
            }

            break;

        case Touch_Dragging:
            MoveMouseTo(point);
            break;

        case Touch_Done:
            break;
    }
}

bool TextMode() {
    return s_textFocus || s_secureFocus;
}

// What a controller button does right now.
Output Mapping(uint64_t button) {
    Output output = { Output_None, 0 };
    auto key = [&](KEY value) {
        output.type = Output_Key;
        output.value = value;
    };

    switch (button) {
        case NxButton_Minus:
        case NxButton_StickR:
            output.type = Output_ToggleCursor;
            return output;
        case NxButton_Plus:
            key(KEY_ESCAPE);
            return output;
        case NxButton_StickL:
            key(KEY_NUMLOCK);
            return output;
        case NxButton_L:
            key(KEY_8);
            return output;
        case NxButton_R:
            key(KEY_TAB);
            return output;
        default:
            break;
    }

    if (TextMode()) {
        switch (button) {
            case NxButton_A: key(KEY_ENTER); return output;
            case NxButton_B: key(KEY_ESCAPE); return output;
            case NxButton_X: key(KEY_TAB); return output;
            case NxButton_Y: output.type = Output_Keyboard; return output;
            case NxButton_Right: key(KEY_RIGHT); return output;
            case NxButton_Up: key(KEY_UP); return output;
            case NxButton_Left: key(KEY_LEFT); return output;
            case NxButton_Down: key(KEY_DOWN); return output;
            default: break;
        }
    }

    if (s_cursorMode) {
        switch (button) {
            case NxButton_A:
            case NxButton_ZR:
                output.type = Output_Mouse;
                output.value = MOUSE_BUTTON_LEFT;
                return output;
            case NxButton_B:
            case NxButton_ZL:
                output.type = Output_Mouse;
                output.value = MOUSE_BUTTON_RIGHT;
                return output;
            case NxButton_Y:
                key(KEY_ENTER);
                return output;
            case NxButton_Up:
                output.type = Output_Wheel;
                output.value = 1;
                return output;
            case NxButton_Down:
                output.type = Output_Wheel;
                output.value = -1;
                return output;
            default:
                return output;
        }
    }

    switch (button) {
        case NxButton_B: key(KEY_SPACE); break;
        case NxButton_A: key(KEY_1); break;
        case NxButton_X: key(KEY_2); break;
        case NxButton_Y: key(KEY_3); break;
        case NxButton_Right: key(KEY_4); break;
        case NxButton_Up: key(KEY_5); break;
        case NxButton_Left: key(KEY_6); break;
        case NxButton_Down: key(KEY_7); break;
        case NxButton_ZL: key(KEY_LSHIFT); break;
        case NxButton_ZR: key(KEY_LCONTROL); break;
        default: break;
    }

    return output;
}

void RequestKeyboard();
void EndLook();

void PressOutput(const Output& output) {
    switch (output.type) {
        case Output_Key:
            PutKey(static_cast<KEY>(output.value), true);
            break;
        case Output_Mouse:
            PutMouseButton(static_cast<MOUSEBUTTON>(output.value), true);
            break;
        case Output_Wheel:
            PutWheel(output.value);
            break;
        case Output_ToggleCursor:
            EndLook();
            s_cursorMode = !s_cursorMode;
            break;
        case Output_Keyboard:
            RequestKeyboard();
            break;
        default:
            break;
    }
}

void ReleaseOutput(const Output& output) {
    switch (output.type) {
        case Output_Key:
            PutKey(static_cast<KEY>(output.value), false);
            break;
        case Output_Mouse:
            PutMouseButton(static_cast<MOUSEBUTTON>(output.value), false);
            break;
        default:
            break;
    }
}

void SetMoveKey(bool& state, KEY key, float amount) {
    bool down = state ? amount > kMoveRelease : amount > kMovePress;

    if (TextMode()) {
        down = false;
    }

    if (down != state) {
        state = down;
        PutKey(key, down);
    }
}

void BeginLook() {
    s_looking = true;
    s_lookSavedPos = s_mousePos;

    // The look starts from the middle of the screen, where the world is, not from wherever the cursor was (over an
    // action bar, say, where a right click means something else).
    MoveMouseTo(ScreenCenter());
    PutMouseButton(MOUSE_BUTTON_RIGHT, true);
}

void EndLook() {
    if (!s_looking) {
        return;
    }

    s_looking = false;
    PutMouseButton(MOUSE_BUTTON_RIGHT, false);

    if (s_osMouseMode == OS_MOUSE_MODE_RELATIVE) {
        // The game puts the cursor back when it leaves relative mode (OsInputSetMouseMode).
        s_mousePos = s_lookSavedPos;
    } else {
        MoveMouseTo(s_lookSavedPos);
    }
}

void PollRightStick(const NxStick& stick, float seconds) {
    float magnitude = std::sqrt(stick.x * stick.x + stick.y * stick.y);
    bool active = magnitude > kStickDeadzone;

    float scale = 0.0f;

    if (active) {
        // Past the deadzone, response grows with the square of the deflection for fine aiming near the center.
        float amount = (magnitude - kStickDeadzone) / (1.0f - kStickDeadzone);
        scale = amount * amount / magnitude;
    }

    float speed = (s_cursorMode ? kCursorSpeed : kLookSpeed) * static_cast<float>(s_contentHeight) * seconds;
    s_cursorRemainderX += stick.x * scale * speed;
    s_cursorRemainderY -= stick.y * scale * speed;
    int32_t dx = static_cast<int32_t>(s_cursorRemainderX);
    int32_t dy = static_cast<int32_t>(s_cursorRemainderY);
    s_cursorRemainderX -= dx;
    s_cursorRemainderY -= dy;

    if (s_cursorMode || TextMode()) {
        EndLook();

        if (s_cursorMode) {
            MoveMouseBy(dx, dy);
        }

        return;
    }

    uint64_t now = OsGetAsyncTimeMs();

    if (active) {
        if (!s_looking && !(s_osButtonState & (MOUSE_BUTTON_LEFT | MOUSE_BUTTON_RIGHT))) {
            BeginLook();
        }

        s_lookIdleSince = 0;

        if (s_looking) {
            MoveMouseBy(dx, dy);
        }
    } else if (s_looking) {
        if (!s_lookIdleSince) {
            s_lookIdleSince = now;
        } else if (now - s_lookIdleSince >= kLookReleaseMs) {
            EndLook();
        }
    }
}

void PollController(const NxInputState& state, float seconds) {
    for (uint32_t i = 0; i < sizeof(kButtons) / sizeof(kButtons[0]); i++) {
        uint64_t button = kButtons[i];
        bool down = (state.buttons & button) != 0;
        bool was = (s_previous.buttons & button) != 0;

        if (down && !was) {
            s_outputs[i] = Mapping(button);
            PressOutput(s_outputs[i]);
        } else if (!down && was) {
            ReleaseOutput(s_outputs[i]);
            s_outputs[i].type = Output_None;
        }
    }

    const NxStick& left = state.sticks[0];
    SetMoveKey(s_moveForward, KEY_W, left.y);
    SetMoveKey(s_moveBack, KEY_S, -left.y);
    SetMoveKey(s_strafeLeft, KEY_Q, -left.x);
    SetMoveKey(s_strafeRight, KEY_E, left.x);

    PollRightStick(state.sticks[1], seconds);
}

// Software keyboard.

void RequestKeyboard() {
    if (!TextMode() || s_keyboardState != Keyboard_Idle) {
        return;
    }

    if (s_secureFocus) {
        s_keyboardState = Keyboard_Open;
        return;
    }

    // Copy the box's text, to start the keyboard with it once the game has handled the keys.
    s_copyClipboardGeneration = SwitchClipboardGeneration();
    ControlKey(KEY_A);
    ControlKey(KEY_C);
    s_keyboardState = Keyboard_Copying;
}

void AppendUtf16(const char* text) {
    const uint8_t* c = reinterpret_cast<const uint8_t*>(text);

    while (*c) {
        uint32_t codepoint;
        int32_t extra;

        if (*c < 0x80) {
            codepoint = *c;
            extra = 0;
        } else if ((*c & 0xE0) == 0xC0) {
            codepoint = *c & 0x1F;
            extra = 1;
        } else if ((*c & 0xF0) == 0xE0) {
            codepoint = *c & 0x0F;
            extra = 2;
        } else {
            codepoint = *c & 0x07;
            extra = 3;
        }

        c++;

        for (int32_t i = 0; i < extra && (*c & 0xC0) == 0x80; i++, c++) {
            codepoint = (codepoint << 6) | (*c & 0x3F);
        }

        if (codepoint >= 0x10000) {
            codepoint -= 0x10000;
            OsQueuePut(OS_INPUT_CHAR, static_cast<int32_t>(0xD800 + (codepoint >> 10)), 1, 0, 0);
            OsQueuePut(OS_INPUT_CHAR, static_cast<int32_t>(0xDC00 + (codepoint & 0x3FF)), 1, 0, 0);
        } else if (codepoint >= 0x20) {
            OsQueuePut(OS_INPUT_CHAR, static_cast<int32_t>(codepoint), 1, 0, 0);
        }
    }
}

void ShowKeyboard() {
    std::string initial;

    if (!s_secureFocus && SwitchClipboardGeneration() != s_copyClipboardGeneration) {
        if (char* copied = OsClipboardGetString()) {
            initial = copied;
            OsClipboardFreeString(copied);
        }
    }

    // Any held controller keys go up first: the applet takes the controller until it closes.
    for (Output& output : s_outputs) {
        ReleaseOutput(output);
        output.type = Output_None;
    }

    EndLook();

    char text[1024];
    bool accepted = NxInputShowKeyboard(s_secureFocus ? "Password" : nullptr, initial.c_str(), s_secureFocus, text, sizeof(text));
    s_keyboardState = Keyboard_Idle;

    // The buttons that closed the applet are still down; don't treat them as new presses.
    NxInputPoll(&s_previous);

    if (!accepted) {
        return;
    }

    if (s_secureFocus) {
        ControlKey(KEY_A);
        TapKey(KEY_BACKSPACE);
        AppendUtf16(text);
    } else {
        OsClipboardPutString(text);
        ControlKey(KEY_A);
        ControlKey(KEY_V);
    }
}

void PollKeyboardRequest() {
    switch (s_keyboardState) {
        case Keyboard_Copying:
            // The copy keys went out with the last poll and the game has handled them by now.
            s_keyboardState = Keyboard_Open;
            break;
        case Keyboard_Open:
            if (TextMode()) {
                ShowKeyboard();
            } else {
                s_keyboardState = Keyboard_Idle;
            }

            break;
        default:
            break;
    }
}

void Poll() {
    uint64_t now = OsGetAsyncTimeMs();
    float seconds = s_lastPollMs ? static_cast<float>(now - s_lastPollMs) / 1000.0f : 0.0f;
    s_lastPollMs = now;

    // Long frames (loading screens) shouldn't fling the cursor.
    if (seconds > 0.1f) {
        seconds = 0.1f;
    }

    if (!NxInputPumpApplet() && !s_closeSent) {
        s_closeSent = true;
        OsQueuePut(OS_INPUT_CLOSE, 0, 0, 0, 0);
    }

    int32_t active = NxInputHasFocus() ? 1 : 0;

    if (active != s_appActive) {
        s_appActive = active;

        if (!active) {
            EndLook();
            s_osButtonState = 0;
        }

        OsQueuePut(OS_INPUT_FOCUS, active, 0, 0, 0);
        Notify(active ? OsMacWindowEvent_FocusGained : OsMacWindowEvent_FocusLost);
    }

    PollKeyboardRequest();

    NxInputState state;
    NxInputPoll(&state);

    if (s_appActive) {
        PollKeyboard(state);
        PollMouse(state);
        PollTouch(state, now);
        PollController(state, seconds);
    }

    s_previous = state;
}

}

namespace {

// The render size for a requested one: 1920x1080 when asked for and docked, otherwise 1280x720. The system scales it to
// the screen.
void RenderSize(int32_t width, int32_t height, int32_t* renderWidth, int32_t* renderHeight) {
    int32_t screenWidth;
    int32_t screenHeight;
    OsMacGetScreenSize(&screenWidth, &screenHeight);

    if (width == 1920 && height == 1080 && screenWidth >= 1920) {
        *renderWidth = 1920;
        *renderHeight = 1080;
    } else {
        *renderWidth = 1280;
        *renderHeight = 720;
    }
}

}

void* OsMacCreateGameWindow(const char* title, int32_t x, int32_t y, int32_t width, int32_t height) {
    NxInputInitialize();

    RenderSize(width, height, &s_contentWidth, &s_contentHeight);
    s_window = NxInputGetWindow(s_contentWidth, s_contentHeight);
    s_windowSize = 1;
    s_mousePos = ScreenCenter();

    return s_window;
}

void* OsMacGetGameView(void* window) {
    return window;
}

float OsMacGetBackingScale(void* window) {
    return 1.0f;
}

float OsMacGetRenderScale(void* window) {
    return 1.0f;
}

void OsMacDestroyGameWindow(void* window) {
    if (!window) {
        return;
    }

    if (OsGuiGetWindow(0) == window) {
        OsInputSetMouseMode(OS_MOUSE_MODE_NORMAL);
        OsGuiSetGxWindow(nullptr);
    }

    s_window = nullptr;
    s_listener = nullptr;
}

void OsMacSetSystemCursorHidden(void* window, bool hidden) {
}

void OsMacSetGameCursor(void* window, const uint32_t* pixels, int32_t hotspotX, int32_t hotspotY) {
}

void OsMacGetContentSize(void* window, int32_t* width, int32_t* height) {
    *width = s_contentWidth;
    *height = s_contentHeight;
}

void OsMacGetContentScreenOrigin(void* window, int32_t* x, int32_t* y) {
    *x = 0;
    *y = 0;
}

void OsMacSetWindowFullscreen(void* window, bool fullscreen, int32_t width, int32_t height) {
    // Windowed or not, the game fills the screen at one of the two render sizes (the GL surface is created after this,
    // so the size can still change).
    RenderSize(width, height, &width, &height);

    if (width != s_contentWidth || height != s_contentHeight) {
        s_contentWidth = width;
        s_contentHeight = height;
        NxInputGetWindow(width, height);
        s_windowSize = 1;
        s_mousePos = ScreenCenter();
    }
}

void OsMacSetWindowListener(void* window, OsMacWindowListener listener, void* param) {
    s_listener = listener;
    s_listenerParam = param;
}

void OsMacGetScreenSize(int32_t* width, int32_t* height) {
    // Decided once: the window keeps its size if the console is docked or undocked later; the system scales it.
    static int32_t s_width;
    static int32_t s_height;

    if (!s_width) {
        NxInputInitialize();
        bool docked = NxInputIsDocked();
        s_width = docked ? 1920 : 1280;
        s_height = docked ? 1080 : 720;
    }

    *width = s_width;
    *height = s_height;
}

void OsMacInitializeApplication() {
    NxInputInitialize();
}

void OsQueuePut(OSINPUT id, int32_t param0, int32_t param1, int32_t param2, int32_t param3) {
    int32_t next = s_queueTail == kQueueSize - 1 ? 0 : s_queueTail + 1;

    if (next == s_queueHead) {
        s_queueHead = s_queueHead == kQueueSize - 1 ? 0 : s_queueHead + 1;
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

    s_queueHead = s_queueHead == kQueueSize - 1 ? 0 : s_queueHead + 1;

    return 1;
}

void OsInputSetWindowResizeLock(int32_t resizeLock) {
}

float OsInputGetMouseSpeed() {
    return s_mouseSpeed * 0.1f;
}

int32_t OsInputIsKeyDown(KEY key) {
    return key >= 0 && key < KEY_LAST && s_keyDown[key];
}

int32_t OsInputIsMouseButtonDown(MOUSEBUTTON button) {
    return (s_osButtonState & button) != 0;
}

void OsInputInitialize() {
    s_mouseSpeed = 10;
    NxInputInitialize();
}

void OsInputDestroy() {
    OsInputSetMouseMode(OS_MOUSE_MODE_NORMAL);
}

void OsInputSetEventPollProc(void (*pollProc)()) {
    s_eventPollProc = pollProc;
}

int32_t OsInputGet(OSINPUT* id, int32_t* param0, int32_t* param1, int32_t* param2, int32_t* param3) {
    *id = static_cast<OSINPUT>(-1);

    if (s_windowSize && s_window) {
        s_windowSize = 0;

        if (s_contentWidth != s_defaultWindowRect.right || s_contentHeight != s_defaultWindowRect.bottom) {
            s_defaultWindowRect.left = 0;
            s_defaultWindowRect.top = 0;
            s_defaultWindowRect.right = s_contentWidth;
            s_defaultWindowRect.bottom = s_contentHeight;

            *id = OS_INPUT_SIZE;
            *param0 = s_contentWidth;
            *param1 = s_contentHeight;
            *param2 = 0;
            *param3 = 0;

            return 1;
        }
    }

    // The hardware is read once per drain of the queue (once a frame): the game calls this until it returns 0.
    if (s_queueHead == s_queueTail && s_needPoll) {
        s_needPoll = false;
        Poll();
    }

    if (!OsQueueGet(id, param0, param1, param2, param3)) {
        s_needPoll = true;
        return 0;
    }

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

    if (mode == OS_MOUSE_MODE_RELATIVE) {
        s_osMouseMode = mode;
    } else {
        // The cursor comes back where relative mode started, as RestoreMouse does on Windows.
        s_osMouseMode = OS_MOUSE_MODE_NORMAL;
        OsQueuePut(OS_INPUT_MOUSE_MOVE, 0, s_mousePos.x, s_mousePos.y, 0);
    }
}

void OsInputSetMouseSpeed(float speed) {
    float clamped = speed < 0.1f ? 0.1f : (speed >= 2.0f ? 2.0f : speed);
    s_mouseSpeed = static_cast<int32_t>(clamped * 10.0f);
}

void OsInputGetMousePosition(int32_t* x, int32_t* y) {
    if (x) {
        *x = s_mousePos.x;
    }

    if (y) {
        *y = s_mousePos.y;
    }
}

void OsInputSetMousePosition(int32_t x, int32_t y) {
    s_mousePos.x = x;
    s_mousePos.y = y;
    ClampToContent(s_mousePos);
}

int32_t OsGetDefaultWindowRect(tagRECT* rect) {
    if (!rect) {
        SErrSetLastError(STORM_ERROR_INVALID_PARAMETER);
        return 0;
    }

    if (!s_defaultWindowRect.right || !s_defaultWindowRect.bottom) {
        if (!s_window) {
            return 0;
        }

        s_defaultWindowRect.left = 0;
        s_defaultWindowRect.top = 0;
        s_defaultWindowRect.right = s_contentWidth;
        s_defaultWindowRect.bottom = s_contentHeight;
    }

    *rect = s_defaultWindowRect;

    return 1;
}

intptr_t OsWindowProc(void* window, uint32_t message, uintptr_t wparam, intptr_t lparam) {
    // No window messages on the Switch; the Gx device still takes this as its window procedure.
    return 0;
}

void OsInputEnableSecureMode(int32_t enable) {
    bool focus = enable != 0;

    if (focus && !s_secureFocus) {
        s_secureFocus = true;
        RequestKeyboard();
    } else if (!focus) {
        s_secureFocus = false;
    }
}

void SwitchSetTextFocus(bool focus) {
    if (focus && !s_textFocus) {
        s_textFocus = true;
        RequestKeyboard();
    } else if (!focus) {
        s_textFocus = false;
    }
}

void SwitchShowMessage(const char* title, const char* message) {
    NxInputShowMessage(title, message);
}

int32_t SwitchIsModifierDown(int32_t modifier) {
    switch (modifier) {
        case 0:
            return s_keyDown[KEY_LCONTROL] || s_keyDown[KEY_RCONTROL];
        case 1:
            return s_keyDown[KEY_LSHIFT] || s_keyDown[KEY_RSHIFT];
        case 2:
            return s_keyDown[KEY_LALT] || s_keyDown[KEY_RALT];
        default:
            return 0;
    }
}
