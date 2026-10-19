// macOS implementation of the Storm registry (Win64/SReg.cpp) on CFPreferences.
//
// Each registry value becomes one preference named "<key path>/<value>", with '\' turned into '/', for example
// "Software/Blizzard Entertainment/World of Warcraft/Client/..." in the "org.wowdecomp.client" domain, so this client
// never touches the preferences of a Blizzard install. REG_SZ values are strings, REG_DWORD values numbers. HKEY_CURRENT_USER
// and HKEY_LOCAL_MACHINE both map to the current user: a game doesn't get to write system-wide preferences on macOS.

#include <CoreFoundation/CoreFoundation.h>
#include <cstdint>
#include <cstdlib>
#include <string>
#include "Engine/Source/Storm/H/storm.h"

namespace {

const CFStringRef kDomain = CFSTR("org.wowdecomp.client");

// Win32 status codes the callers may see through SErrGetLastError.
const uint32_t kErrorFileNotFound = 2;
const uint32_t kErrorInvalidParameter = 87;

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

CFStringRef MakeString(const std::string& text) {
    return CFStringCreateWithCString(kCFAllocatorDefault, text.c_str(), kCFStringEncodingUTF8);
}

CFPropertyListRef LoadEntry(const char* keyname, const char* valuename, uint32_t flags) {
    // Both hives are the same store here, so excluding only one of them still finds the value.
    if ((flags & SREG_EXCLUDE_CURRENT_USER) && (flags & SREG_EXCLUDE_LOCAL_MACHINE)) {
        return nullptr;
    }

    CFStringRef name = MakeString(FullValueName(keyname, valuename, flags));

    if (!name) {
        return nullptr;
    }

    CFPropertyListRef value = CFPreferencesCopyAppValue(name, kDomain);
    CFRelease(name);

    return value;
}

int32_t SaveEntry(const char* keyname, const char* valuename, uint32_t flags, CFPropertyListRef value) {
    CFStringRef name = MakeString(FullValueName(keyname, valuename, flags));

    if (!name || !value) {
        if (name) {
            CFRelease(name);
        }

        SErrSetLastError(kErrorInvalidParameter);
        return 0;
    }

    CFPreferencesSetAppValue(name, value, kDomain);
    CFRelease(name);

    if ((flags & SREG_FLUSH_KEY) && !CFPreferencesAppSynchronize(kDomain)) {
        return 0;
    }

    return 1;
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

    CFPropertyListRef value = LoadEntry(keyname, valuename, flags);

    if (!value) {
        SErrSetLastError(kErrorFileNotFound);
        return 0;
    }

    int32_t result = 1;

    if (CFGetTypeID(value) == CFStringGetTypeID()) {
        if (!CFStringGetCString(static_cast<CFStringRef>(value), buffer, buffersize, kCFStringEncodingUTF8)) {
            buffer[0] = 0;
        }
    } else if (CFGetTypeID(value) == CFNumberGetTypeID()) {
        int64_t number = 0;
        CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberSInt64Type, &number);
        SStrPrintf(buffer, buffersize, "%u", static_cast<uint32_t>(number));
    } else {
        result = 0;
    }

    CFRelease(value);

    return result;
}

int32_t STORMAPI SRegLoadValue(const char* keyname, const char* valuename, uint32_t flags, uint32_t* value) {
    if (!keyname || !*keyname || !valuename || !*valuename || !value) {
        SErrSetLastError(kErrorInvalidParameter);
        return 0;
    }

    CFPropertyListRef stored = LoadEntry(keyname, valuename, flags);

    if (!stored) {
        SErrSetLastError(kErrorFileNotFound);
        return 0;
    }

    int32_t result = 1;

    if (CFGetTypeID(stored) == CFNumberGetTypeID()) {
        int64_t number = 0;
        CFNumberGetValue(static_cast<CFNumberRef>(stored), kCFNumberSInt64Type, &number);
        *value = static_cast<uint32_t>(number);
    } else if (CFGetTypeID(stored) == CFStringGetTypeID()) {
        char buffer[256];

        if (CFStringGetCString(static_cast<CFStringRef>(stored), buffer, sizeof(buffer), kCFStringEncodingUTF8)) {
            *value = static_cast<uint32_t>(strtoul(buffer, nullptr, 0));
        } else {
            result = 0;
        }
    } else {
        result = 0;
    }

    CFRelease(stored);

    return result;
}

int32_t STORMAPI SRegSaveString(const char* keyname, const char* valuename, uint32_t flags, const char* string) {
    if (!keyname || !*keyname || !valuename || !*valuename || !string) {
        SErrSetLastError(kErrorInvalidParameter);
        return 0;
    }

    CFStringRef value = CFStringCreateWithCString(kCFAllocatorDefault, string, kCFStringEncodingUTF8);
    int32_t result = SaveEntry(keyname, valuename, flags, value);

    if (value) {
        CFRelease(value);
    }

    return result;
}

int32_t STORMAPI SRegSaveValue(const char* keyname, const char* valuename, uint32_t flags, uint32_t value) {
    if (!keyname || !*keyname || !valuename || !*valuename) {
        SErrSetLastError(kErrorInvalidParameter);
        return 0;
    }

    int64_t number = value;
    CFNumberRef stored = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt64Type, &number);
    int32_t result = SaveEntry(keyname, valuename, flags, stored);

    if (stored) {
        CFRelease(stored);
    }

    return result;
}
