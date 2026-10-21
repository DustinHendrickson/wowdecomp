#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include "WoW/Source/Ui/Switch/SwitchControlsScript.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Os/Switch/SwitchInput.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "WoW/Source/Object/ObjectClient/GameObject_C.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "WoW/Source/Object/ObjectClient/Unit_C.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/Ui/GameUI.h"

namespace {

// Interact with the nearest thing a right-click would: a lootable or skinnable body, an NPC with something to offer
// (gossip, quests, a vendor, a trainer, a flight master, ...), or a usable object (herbs, ore, chests, mailboxes,
// quest objects). Things in front of the player win over closer things behind.

const float kUnitReach = 6.0f;
const float kBehindPenalty = 10.0f;
const float kFrontCone = 0.7853982f;    // 45 degrees either side of facing

struct InteractSearch {
    CGPlayer_C* player;
    C3Vector position;
    float facing;
    WOWGUID best;
    float bestScore;
    bool bestIsUnit;
};

// Distance, penalized when the object is outside the cone in front of the player.
float Score(const InteractSearch& search, const C3Vector& position, float* distance) {
    float dx = position.x - search.position.x;
    float dy = position.y - search.position.y;
    float dz = position.z - search.position.z;
    *distance = std::sqrt(dx * dx + dy * dy + dz * dz);

    C3Vector from = search.position;
    C3Vector to = position;
    float delta = std::fmod(CalculateFacingTo(&from, &to) - search.facing + 12.566371f, 6.2831855f);
    bool inFront = delta < kFrontCone || delta > 6.2831855f - kFrontCone;

    return inFront ? *distance : *distance + kBehindPenalty;
}

int32_t InteractProc(WOWGUID guid, void* param) {
    InteractSearch& search = *static_cast<InteractSearch*>(param);
    CGObject_C* object = ClntObjMgrObjectPtr(guid, TYPEMASK_OBJECT, __FILE__, __LINE__);

    if (!object || object == search.player) {
        return 1;
    }

    float distance;
    float score;
    bool isUnit = false;

    if (object->m_obj->m_type & TYPEMASK_UNIT) {
        CGUnit_C* unit = static_cast<CGUnit_C*>(object);
        bool dead = unit->IsDeadOrGhost() || unit->m_unit->m_unitFieldHealth <= 0;

        if (dead) {
            // Lootable (UNIT_DYNFLAG_LOOTABLE) or skinnable (UNIT_FLAG_SKINNABLE).
            if (!(unit->m_unit->m_unitDynamicFlags & 0x1) && !(unit->m_unit->m_unitFieldFlags & 0x4000000)) {
                return 1;
            }
        } else if (!unit->m_unit->m_unitNpcFlags || search.player->CanAttack(unit)) {
            return 1;
        }

        score = Score(search, unit->GetPosition(), &distance);

        if (distance > kUnitReach) {
            return 1;
        }

        isUnit = true;
    } else if (object->m_obj->m_type & TYPEMASK_GAMEOBJECT) {
        CGGameObject_C* gameObject = static_cast<CGGameObject_C*>(object);

        if (!gameObject->CanUse()) {
            return 1;
        }

        score = Score(search, gameObject->GetPosition(), &distance);

        if (distance > gameObject->m_typeData->GetInteractDistance() + 1.0f) {
            return 1;
        }
    } else {
        return 1;
    }

    if (score < search.bestScore) {
        search.best = guid;
        search.bestScore = score;
        search.bestIsUnit = isUnit;
    }

    return 1;
}

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

int32_t Script_SwitchControls_GetOption(lua_State* L) {
    const char* name = lua_tostring(L, 1);
    bool value;

    if (!name || !SwitchGetOption(name, &value)) {
        lua_pushnil(L);
    } else {
        lua_pushboolean(L, value);
    }

    return 1;
}

int32_t Script_SwitchControls_ClearTextFocus(lua_State* L) {
    SwitchClearTextFocus();
    return 0;
}

int32_t Script_SwitchControls_SetKeyCapture(lua_State* L) {
    SwitchSetKeyCapture(lua_toboolean(L, 1) != 0);
    return 0;
}

// interacted = SwitchControls_Interact(): what the controller's interact button does. The add-on's button calls it from a
// key binding; the game's own InteractUnit refuses add-on code, so this runs the right-click as if from the game.
int32_t Script_SwitchControls_Interact(lua_State* L) {
    InteractSearch search;
    search.player = ClntObjMgrGetActivePlayerObj();
    search.best.m_guid = 0;
    search.bestScore = 1e9f;
    search.bestIsUnit = false;

    if (!search.player || search.player->IsDeadOrGhost()) {
        lua_pushboolean(L, 0);
        return 1;
    }

    search.position = search.player->GetPosition();
    search.facing = search.player->GetFacing();
    ClntObjMgrEnumVisibleObjects(InteractProc, &search);

    if (!search.best) {
        lua_pushboolean(L, 0);
        return 1;
    }

    const char* taint = lua_tainted;
    lua_tainted = nullptr;

    if (search.bestIsUnit) {
        CGGameUI::Target(search.best);
    }

    int32_t interacted = CGGameUI::OnSpriteRightClick(search.best);
    lua_tainted = taint;

    lua_pushboolean(L, interacted != 0);
    return 1;
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
    { "SwitchControls_GetOption", &Script_SwitchControls_GetOption },
    { "SwitchControls_SetOption", &Script_SwitchControls_SetOption },
    { "SwitchControls_SetKeyCapture", &Script_SwitchControls_SetKeyCapture },
    { "SwitchControls_ClearTextFocus", &Script_SwitchControls_ClearTextFocus },
    { "SwitchControls_Interact", &Script_SwitchControls_Interact },
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
