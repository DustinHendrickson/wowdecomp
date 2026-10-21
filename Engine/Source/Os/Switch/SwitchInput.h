#ifndef ENGINE_SOURCE_OS_SWITCH_SWITCHINPUT_H
#define ENGINE_SOURCE_OS_SWITCH_SWITCHINPUT_H

// Calls between the Switch Os files (Input.cpp, OsIME.cpp, OsClipboard.cpp, Gui.cpp).

#include <cstdint>

// A text box gained or lost the keyboard focus (OsIMEEnable). Gaining it opens the software keyboard.
void SwitchSetTextFocus(bool focus);

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

// 0 Control, 1 Shift, 2 Alt, as OsGuiIsModifierKeyDown takes them.
int32_t SwitchIsModifierDown(int32_t modifier);

#endif
