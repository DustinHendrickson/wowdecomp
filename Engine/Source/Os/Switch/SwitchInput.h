#ifndef ENGINE_SOURCE_OS_SWITCH_SWITCHINPUT_H
#define ENGINE_SOURCE_OS_SWITCH_SWITCHINPUT_H

// Calls between the Switch Os files (Input.cpp, OsIME.cpp, OsClipboard.cpp, Gui.cpp).

#include <cstdint>

// A text box gained or lost the keyboard focus (OsIMEEnable). Gaining it opens the software keyboard.
void SwitchSetTextFocus(bool focus);

// No text box has the focus after all (one was destroyed while focused, so never reported losing it): the controller
// leaves text mode, whose face buttons edit text and whose sticks send nothing.
void SwitchClearTextFocus();

// Changes every time the clipboard is written, so Input.cpp can tell whether a copy happened.
uint32_t SwitchClipboardGeneration();

void SwitchShowMessage(const char* title, const char* message);

// Development settings from Debug.txt in the game folder, one "name=value" per line; nullptr when unset. The Switch has
// no environment for the WOWDECOMP_* variables the macOS build reads. Known names:
//   screenshots=15,25   press Print Screen that many seconds after the first input poll (WOWDECOMP_SCREENSHOTS)
const char* SwitchDebugSetting(const char* name);

// The controller mode and what the cursor shows for, for the SwitchControls add-on (WoW/Source/Ui/Switch).
struct SwitchControllerState {
    bool cursorMode;
    bool textFocus;
    bool pointerActive;
};

SwitchControllerState SwitchGetControllerState();
void SwitchSetCursorMode(bool cursorMode);
bool SwitchIsDocked();

// Controller options from the SwitchControls add-on, kept in Registry.txt: "invertLook" (the right stick's up and down
// in play mode), and the add-on's own ("hints"). SwitchGetOption is false for an option never set.
bool SwitchGetOption(const char* name, bool* value);
void SwitchSetOption(const char* name, bool value);

// The swap interval to present frames with, given the one the game asks for (its vertical sync setting): every other
// refresh (30 fps) undocked with the battery saver on.
int32_t SwitchFrameInterval(int32_t requested);

// The render size the game's resolution gives has changed with docking (1920x1080 renders at 1280x720 handheld): the
// device needs restarting (the SwitchControls add-on calls RestartGx).
bool SwitchDisplayChanged();

// Moves the pointer in cursor mode, to a point given as fractions of the screen from its bottom left (UI coordinates
// over the screen's size): the SwitchControls add-on's D-pad navigation.
void SwitchSetPointer(float x, float y);

// The Key Bindings window is waiting for a key: controller buttons send keys rather than clicks.
void SwitchSetKeyCapture(bool capture);

// 0 Control, 1 Shift, 2 Alt, as OsGuiIsModifierKeyDown takes them.
int32_t SwitchIsModifierDown(int32_t modifier);

#endif
