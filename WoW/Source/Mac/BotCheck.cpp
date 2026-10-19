// macOS version of Win64/BotCheck.cpp. The checks are no-ops in the decompiled client (AdditionalCheck1Scan finds
// nothing), so this keeps the same behavior without the x86-only parts: the SEH guard and the test for a jmp (0xE9)
// patched over OsValidateFunctionPointer.

#include <cstdint>
#include "Engine/Source/Base/CDataStore.h"
#include "Engine/Source/Os/OsLibrary.h"
#include "Engine/Source/Os/System.h"
#include "WoW/Source/Client.h"
#include "WoW/Source/Net/Types.h"

void AdditionalCheck1() {
    if (!s_botCheckEnabled || s_botCheckDelay <= 0 || s_botCheckDelay > 420) {
        return;
    }

    if (InterlockedCompareExchange(&s_additionalCheckArmed, 0, 1) != 1) {
        return;
    }

    AdditionalCheckInterface iface;
    iface.m_memAlloc = Client_MemAlloc;
    iface.m_memFree = Client_MemFree;
    iface.m_loadLibrary = OsLoadLibrary;
    iface.m_getLibraryEntryPoint = OsGetLibraryEntryPoint;
    iface.m_freeLibrary = OsFreeLibrary;

    if (AdditionalCheck1Scan(&iface, 0)) {
        SendBotDetected2(CMSG_BOT_DETECTED2, 0x60231D69);
    }
}

void InitTokenSeed(const uint8_t* key) {
}

int32_t Packet_SMSG_CHECK_FOR_BOTS(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    uint8_t key[16];
    msg->GetArray(key, sizeof(key));
    return 1;
}

int32_t GatherAdditionalChecks(uint8_t* check1, uint8_t* check2, uint8_t* check3) {
    *check1 = 0;
    *check2 = 0;
    *check3 = 0;
    return 0;
}

uint8_t AdditionalCheck1Scan(const AdditionalCheckInterface* iface, int32_t unused_a2) {
    return 0;
}

void LoadEncryptedModule(const uint8_t* key, uint32_t length, void (*callback)()) {
}
