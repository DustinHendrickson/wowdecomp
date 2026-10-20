// Nintendo Switch implementation of the Storm registry (Win64/SReg.cpp).
//
// Values live in "Registry.txt" in the game folder, one "<key path>/<value>=<data>" line each, with '\' turned into
// '/' as in the macOS preference names. Numbers are written as "#<decimal>", strings as "$<text>". Both hives are the
// same store, as on macOS. The file is read on first use and rewritten whenever a value changes.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include "Engine/Source/Storm/H/storm.h"

namespace {

const char kFileName[] = "Registry.txt";

// Win32 status codes the callers may see through SErrGetLastError.
const uint32_t kErrorFileNotFound = 2;
const uint32_t kErrorInvalidParameter = 87;

std::mutex s_lock;
std::map<std::string, std::string>* s_values;

std::string FullValueName(const char* keyname, const char* valuename, uint32_t flags) {
    std::string name;

    if (!(flags & SREG_ABSOLUTE)) {
        name = (flags & SREG_BATTLE_NET) ? "Software\\Battle.net\\" : "Software\\Blizzard Entertainment\\";
    }

    name += keyname;

    if (!name.empty() && name.back() != '\\') {
        name += '\\';
    }

    name += valuename;

    for (char& c : name) {
        if (c == '\\') {
            c = '/';
        }
    }

    return name;
}

// Called with s_lock held.
std::map<std::string, std::string>& Values() {
    if (s_values) {
        return *s_values;
    }

    s_values = new std::map<std::string, std::string>();

    if (FILE* file = fopen(kFileName, "r")) {
        char line[2048];

        while (fgets(line, sizeof(line), file)) {
            std::string text = line;

            while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
                text.pop_back();
            }

            size_t equals = text.find('=');

            if (equals != std::string::npos) {
                (*s_values)[text.substr(0, equals)] = text.substr(equals + 1);
            }
        }

        fclose(file);
    }

    return *s_values;
}

// Called with s_lock held.
int32_t Flush() {
    FILE* file = fopen(kFileName, "w");

    if (!file) {
        return 0;
    }

    for (const auto& entry : Values()) {
        fprintf(file, "%s=%s\n", entry.first.c_str(), entry.second.c_str());
    }

    fclose(file);
    return 1;
}

bool LoadEntry(const char* keyname, const char* valuename, uint32_t flags, std::string* value) {
    if ((flags & SREG_EXCLUDE_CURRENT_USER) && (flags & SREG_EXCLUDE_LOCAL_MACHINE)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(s_lock);
    auto& values = Values();
    auto it = values.find(FullValueName(keyname, valuename, flags));

    if (it == values.end()) {
        return false;
    }

    *value = it->second;
    return true;
}

int32_t SaveEntry(const char* keyname, const char* valuename, uint32_t flags, const std::string& value) {
    // Lines hold one value each.
    if (value.find('\n') != std::string::npos) {
        SErrSetLastError(kErrorInvalidParameter);
        return 0;
    }

    std::lock_guard<std::mutex> lock(s_lock);
    Values()[FullValueName(keyname, valuename, flags)] = value;

    return Flush();
}

}

int32_t STORMAPI SRegGetBaseKey(uint32_t flags, char* buffer, uint32_t bufferchars) {
    if (flags & SREG_BATTLE_NET) {
        SStrCopy(buffer, "Software\\Battle.net\\", bufferchars);
    } else {
        SStrCopy(buffer, "Software\\Blizzard Entertainment\\", bufferchars);
    }

    return 1;
}

int32_t STORMAPI SRegLoadString(const char* keyname, const char* valuename, uint32_t flags, char* buffer, uint32_t buffersize) {
    if (!keyname || !*keyname || !valuename || !*valuename || !buffer || !buffersize) {
        SErrSetLastError(kErrorInvalidParameter);
        return 0;
    }

    std::string value;

    if (!LoadEntry(keyname, valuename, flags, &value)) {
        SErrSetLastError(kErrorFileNotFound);
        return 0;
    }

    if (!value.empty() && value[0] == '#') {
        SStrPrintf(buffer, buffersize, "%u", static_cast<uint32_t>(strtoull(value.c_str() + 1, nullptr, 10)));
    } else {
        SStrCopy(buffer, value.empty() ? "" : value.c_str() + 1, buffersize);
    }

    return 1;
}

int32_t STORMAPI SRegLoadValue(const char* keyname, const char* valuename, uint32_t flags, uint32_t* value) {
    if (!keyname || !*keyname || !valuename || !*valuename || !value) {
        SErrSetLastError(kErrorInvalidParameter);
        return 0;
    }

    std::string stored;

    if (!LoadEntry(keyname, valuename, flags, &stored)) {
        SErrSetLastError(kErrorFileNotFound);
        return 0;
    }

    if (stored.empty()) {
        return 0;
    }

    *value = static_cast<uint32_t>(stored[0] == '#' ? strtoull(stored.c_str() + 1, nullptr, 10) : strtoul(stored.c_str() + 1, nullptr, 0));

    return 1;
}

int32_t STORMAPI SRegSaveString(const char* keyname, const char* valuename, uint32_t flags, const char* string) {
    if (!keyname || !*keyname || !valuename || !*valuename || !string) {
        SErrSetLastError(kErrorInvalidParameter);
        return 0;
    }

    return SaveEntry(keyname, valuename, flags, std::string("$") + string);
}

int32_t STORMAPI SRegSaveValue(const char* keyname, const char* valuename, uint32_t flags, uint32_t value) {
    if (!keyname || !*keyname || !valuename || !*valuename) {
        SErrSetLastError(kErrorInvalidParameter);
        return 0;
    }

    char text[16];
    snprintf(text, sizeof(text), "#%u", value);

    return SaveEntry(keyname, valuename, flags, text);
}
