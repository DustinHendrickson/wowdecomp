#ifndef ENGINE_SOURCE_OS_SWITCH_NXINPUT_H
#define ENGINE_SOURCE_OS_SWITCH_NXINPUT_H

// The Switch hardware behind the Os input layer (NxInput.cpp): controllers, the touch screen, USB keyboards and mice,
// the applet state (HOME menu, docking) and the system's software keyboard. Plain types only, so Input.cpp can use them
// next to the game's headers without <switch.h>.

#include <cstdint>

// Controller buttons, as libnx's HidNpadButton bits. Joy-Cons held together, a single Joy-Con held sideways, the Pro
// Controller and handheld mode all report through these.
enum NxButton : uint64_t {
    NxButton_A = 1ull << 0,
    NxButton_B = 1ull << 1,
    NxButton_X = 1ull << 2,
    NxButton_Y = 1ull << 3,
    NxButton_StickL = 1ull << 4,
    NxButton_StickR = 1ull << 5,
    NxButton_L = 1ull << 6,
    NxButton_R = 1ull << 7,
    NxButton_ZL = 1ull << 8,
    NxButton_ZR = 1ull << 9,
    NxButton_Plus = 1ull << 10,
    NxButton_Minus = 1ull << 11,
    NxButton_Left = 1ull << 12,
    NxButton_Up = 1ull << 13,
    NxButton_Right = 1ull << 14,
    NxButton_Down = 1ull << 15,
};

struct NxStick {
    // -1 .. 1, up and right positive.
    float x;
    float y;
};

struct NxTouch {
    // Screen pixels of the 1280x720 panel.
    int32_t x;
    int32_t y;
};

// USB keyboard modifiers, as libnx's HidKeyboardModifier bits.
enum NxKeyboardModifier : uint32_t {
    NxKeyboardModifier_CapsLock = 1u << 8,
};

struct NxInputState {
    uint64_t buttons;
    NxStick sticks[2];

    int32_t touchCount;
    NxTouch touches[4];

    // USB keyboard: one bit per HID usage (HidKeyboardKey), and the modifier bits.
    uint64_t keys[4];
    uint32_t keyModifiers;

    // USB mouse: whether one is attached, its position in screen pixels, movement since the last poll, wheel notches
    // and HidMouseButton bits.
    bool mouseConnected;
    int32_t mouseX;
    int32_t mouseY;
    int32_t mouseDeltaX;
    int32_t mouseDeltaY;
    int32_t mouseWheel;
    uint32_t mouseButtons;
};

void NxInputInitialize();

// Runs the applet's message loop (HOME menu, sleep, docking). Returns false once the system asks the game to exit.
bool NxInputPumpApplet();

void NxInputPoll(NxInputState* state);

// Whether the game has the screen: false while the HOME menu or an overlay is up.
bool NxInputHasFocus();

// Docked on a TV (the 1080p screen) rather than handheld.
bool NxInputIsDocked();

// The system's software keyboard, modal. initialText is UTF-8. Returns false when the player cancels; otherwise the
// UTF-8 result is in text.
bool NxInputShowKeyboard(const char* title, const char* initialText, bool password, char* text, uint32_t size);

// libnx's default NWindow (the screen), sized to width x height: the EGL native window the GL device draws to.
void* NxInputGetWindow(int32_t width, int32_t height);

// Raises the CPU and GPU clocks to the highest Nintendo uses for games, for the docked or handheld GPU, and puts the
// original ones back at exit (NxClocks.cpp).
void NxClocksApply(bool docked);

// The error applet with a message, modal.
void NxInputShowMessage(const char* title, const char* message);

#endif
