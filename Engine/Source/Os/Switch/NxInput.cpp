// libnx side of the Switch Os input layer (NxInput.h).

#include <switch.h>
#include <cstdio>
#include <cstring>
#include "Engine/Source/Os/Switch/NxInput.h"

namespace {

PadState s_pad;
bool s_initialized;

float StickAxis(s32 value) {
    float axis = static_cast<float>(value) / static_cast<float>(JOYSTICK_MAX);
    return axis < -1.0f ? -1.0f : (axis > 1.0f ? 1.0f : axis);
}

}

void NxInputInitialize() {
    if (s_initialized) {
        return;
    }

    s_initialized = true;

    // One player, any controller layout: handheld, Joy-Cons together or alone, the Pro Controller.
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&s_pad);

    hidInitializeTouchScreen();
    hidInitializeKeyboard();
    hidInitializeMouse();

    // Keep receiving focus changes, so the game hears about the HOME menu as a window losing focus.
    appletSetFocusHandlingMode(AppletFocusHandlingMode_SuspendHomeSleepNotify);

    // Closing from the HOME menu becomes a request the game answers by quitting properly (NxInputPumpApplet).
    appletLockExit();
}

// Closing the game from the HOME menu asks it to quit rather than ending the process there and then (appletLockExit in
// NxInputInitialize), so it logs out and saves the UI's saved variables and settings as Exit Game does. Should quitting
// hang, the exit is unlocked after a while and the system ends the process after all.
void ExitWatchdog(void* argument) {
    svcSleepThread(30000000000ULL);
    appletUnlockExit();
}

bool NxInputPumpApplet() {
    static bool s_exitRequested;

    if (appletMainLoop()) {
        return true;
    }

    if (!s_exitRequested) {
        s_exitRequested = true;
        fprintf(stderr, "Exit requested from the HOME menu: quitting\n");

        static Thread s_watchdog;

        if (R_SUCCEEDED(threadCreate(&s_watchdog, ExitWatchdog, nullptr, nullptr, 0x4000, 0x2C, -2))) {
            threadStart(&s_watchdog);
        }
    }

    return false;
}

void NxInputPoll(NxInputState* state) {
    memset(state, 0, sizeof(*state));

    padUpdate(&s_pad);
    state->buttons = padGetButtons(&s_pad);

    for (unsigned i = 0; i < 2; i++) {
        HidAnalogStickState stick = padGetStickPos(&s_pad, i);
        state->sticks[i].x = StickAxis(stick.x);
        state->sticks[i].y = StickAxis(stick.y);
    }

    HidTouchScreenState touch = {};

    if (hidGetTouchScreenStates(&touch, 1)) {
        int32_t count = touch.count < 4 ? touch.count : 4;
        state->touchCount = count;

        for (int32_t i = 0; i < count; i++) {
            state->touches[i].x = static_cast<int32_t>(touch.touches[i].x);
            state->touches[i].y = static_cast<int32_t>(touch.touches[i].y);
        }
    }

    HidKeyboardState keyboard = {};

    if (hidGetKeyboardStates(&keyboard, 1)) {
        memcpy(state->keys, keyboard.keys, sizeof(state->keys));
        state->keyModifiers = static_cast<uint32_t>(keyboard.modifiers);
    }

    HidMouseState mouse = {};

    if (hidGetMouseStates(&mouse, 1) && (mouse.attributes & HidMouseAttribute_IsConnected)) {
        state->mouseConnected = true;
        state->mouseX = mouse.x;
        state->mouseY = mouse.y;
        state->mouseDeltaX = mouse.delta_x;
        state->mouseDeltaY = mouse.delta_y;
        state->mouseWheel = mouse.wheel_delta_y;
        state->mouseButtons = mouse.buttons;
    }
}

bool NxInputHasFocus() {
    return appletGetFocusState() == AppletFocusState_InFocus;
}

bool NxInputIsDocked() {
    return appletGetOperationMode() == AppletOperationMode_Console;
}

bool NxInputShowKeyboard(const char* title, const char* initialText, bool password, char* text, uint32_t size) {
    SwkbdConfig config;

    if (!size || R_FAILED(swkbdCreate(&config, 0))) {
        return false;
    }

    if (password) {
        swkbdConfigMakePresetPassword(&config);
    } else {
        swkbdConfigMakePresetDefault(&config);
    }

    if (title && *title) {
        swkbdConfigSetHeaderText(&config, title);
    }

    if (initialText && *initialText) {
        swkbdConfigSetInitialText(&config, initialText);
    }

    text[0] = '\0';
    Result result = swkbdShow(&config, text, size);
    swkbdClose(&config);

    return R_SUCCEEDED(result);
}

void* NxInputGetWindow(int32_t width, int32_t height) {
    NWindow* window = nwindowGetDefault();
    nwindowSetDimensions(window, static_cast<u32>(width), static_cast<u32>(height));
    return window;
}

void NxInputShowMessage(const char* title, const char* message) {
    ErrorApplicationConfig config;
    char text[2048];

    snprintf(text, sizeof(text), "%s\n\n%s", title ? title : "", message ? message : "");

    if (R_SUCCEEDED(errorApplicationCreate(&config, text, nullptr))) {
        errorApplicationShow(&config);
    }
}
