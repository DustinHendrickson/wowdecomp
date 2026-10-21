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
//   on = SwitchControls_GetOption(name)                   controller options ("invertLook", "hints"), saved in
//   SwitchControls_SetOption(name, on)                    Registry.txt as soon as they change; nil if never set
//   SwitchControls_SetKeyCapture(on)                      the Key Bindings window is waiting for a key
//   SwitchControls_ClearTextFocus()                       no text box has the focus: leave text mode
//   SwitchControls_SetPointer(x, y)                       pointer to (x, y), fractions of the screen from bottom left
//   changed = SwitchControls_DisplayChanged()             docking changed the render size: the device needs a restart
//   interacted = SwitchControls_Interact()                right-click the nearest lootable body, NPC or usable object

void SwitchControlsRegisterScriptFunctions();

// After GlueXML loads: greys out the login screens' video options the Switch has no use for.
void SwitchControlsSetUpGlue();
void SwitchControlsUnregisterScriptFunctions();

#endif
