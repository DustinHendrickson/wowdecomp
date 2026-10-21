#ifndef WOW_SOURCE_UI_SWITCH_SWITCHCONTROLSSCRIPT_H
#define WOW_SOURCE_UI_SWITCH_SWITCHCONTROLSSCRIPT_H

// Lua functions for the SwitchControls add-on (the controller hints and automatic cursor mode on the Switch):
//
//   mode, textFocus, pointer = SwitchControls_GetMode()   mode is "play" or "cursor"; textFocus is true while a text box
//                                                         has the keyboard focus; pointer is true while the touch screen
//                                                         or a USB mouse is in use
//   SwitchControls_SetMode("play" | "cursor")
//   docked = SwitchControls_IsDocked()
//   SwitchControls_Log(text)                              a line in Logs/Console.txt
//   SwitchControls_SetOption("invertLook", on)            controller options
//   interacted = SwitchControls_Interact()                right-click the nearest lootable body, NPC or usable object

void SwitchControlsRegisterScriptFunctions();
void SwitchControlsUnregisterScriptFunctions();

#endif
