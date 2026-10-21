#include <cstdint>
#include <cstdio>
#include <cstring>
#include "WoW/Source/Ui/Switch/SwitchControlsScript.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Os/Switch/SwitchInput.h"
#include "Engine/Source/lua-5.1/src/lua.h"

namespace {

int32_t Script_SwitchControls_GetMode(lua_State* L) {
    SwitchControllerState state = SwitchGetControllerState();
    lua_pushstring(L, state.cursorMode ? "cursor" : "play");
    lua_pushboolean(L, state.textFocus);
    lua_pushboolean(L, state.pointerActive);
    return 3;
}

int32_t Script_SwitchControls_SetMode(lua_State* L) {
    const char* mode = lua_tostring(L, 1);

    if (mode && (!strcmp(mode, "cursor") || !strcmp(mode, "play"))) {
        SwitchSetCursorMode(!strcmp(mode, "cursor"));
    }

    return 0;
}

// A line in Logs/Console.txt, for diagnosing the add-on on the console.
int32_t Script_SwitchControls_Log(lua_State* L) {
    const char* text = lua_tostring(L, 1);

    if (text) {
        fprintf(stderr, "SwitchControls: %s\n", text);
    }

    return 0;
}

int32_t Script_SwitchControls_SetOption(lua_State* L) {
    const char* name = lua_tostring(L, 1);

    if (name) {
        SwitchSetOption(name, lua_toboolean(L, 2) != 0);
    }

    return 0;
}

int32_t Script_SwitchControls_IsDocked(lua_State* L) {
    lua_pushboolean(L, SwitchIsDocked());
    return 1;
}

const struct {
    const char* m_name;
    lua_CFunction m_method;
} s_functions[] = {
    { "SwitchControls_GetMode", &Script_SwitchControls_GetMode },
    { "SwitchControls_SetMode", &Script_SwitchControls_SetMode },
    { "SwitchControls_IsDocked", &Script_SwitchControls_IsDocked },
    { "SwitchControls_Log", &Script_SwitchControls_Log },
    { "SwitchControls_SetOption", &Script_SwitchControls_SetOption },
};

}

void SwitchControlsRegisterScriptFunctions() {
    for (const auto& function : s_functions) {
        FrameScript_RegisterFunction(function.m_name, function.m_method);
    }
}

void SwitchControlsUnregisterScriptFunctions() {
    for (const auto& function : s_functions) {
        FrameScript_UnregisterFunction(function.m_name);
    }
}
