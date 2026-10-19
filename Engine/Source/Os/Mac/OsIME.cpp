// macOS version of Os/Win64/OsIME.cpp (Windows IMM). Text arrives through the game view's NSTextInputClient (Input.mm),
// which commits dead-key and simple composed input as characters. Composition and candidate windows for CJK input
// methods aren't routed to the game UI yet, so these report "no composition".

#include <cstdint>
#include "Engine/Source/Os/OsIME.h"

int32_t OsIMEGetLanguageMode() {
    return 0;
}

int32_t OsIMEGetCompositionString(char* buffer, uint32_t bufferSize) {
    if (buffer && bufferSize) {
        buffer[0] = '\0';
    }

    return 0;
}

int32_t OsIMEGetCompositionResult(char* buffer, uint32_t bufferSize) {
    if (buffer && bufferSize) {
        buffer[0] = '\0';
    }

    return 0;
}

int32_t OsIMEGetClauseInfo(uint32_t& clauseStart, uint32_t& clauseEnd, uint32_t& cursor) {
    clauseStart = 0;
    clauseEnd = 0;
    cursor = 0;

    return 0;
}

void OsIMEEnable(int32_t enable) {
}

void OsIMESync() {
}

void OsIMESetLanguageMode(int32_t native) {
}

void OsIMEInitialize() {
}

void OsIMEDestroy() {
}

int32_t OsIMEGetCandidates(uint32_t listMask, uint32_t& pageStart, uint32_t& pageSize, uint32_t& count, uint32_t& selection, TSGrowableArray<OsIMECandidate>& candidates) {
    pageStart = 0;
    pageSize = 0;
    count = 0;
    selection = 0;

    return 0;
}
