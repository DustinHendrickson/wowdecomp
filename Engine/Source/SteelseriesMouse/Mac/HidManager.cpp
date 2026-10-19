// macOS HidManager (Win64 talks to the SteelSeries WoW mouse through the Windows HID API). No devices are found, so
// WowMouse reports the mouse as not connected, as on a Windows machine without one.

#include <cstdint>
#include <new>
#include "Engine/Source/SteelseriesMouse/HidManager.h"
#include "Engine/Source/Storm/H/storm.h"

HidManager::~HidManager() {
}

namespace {

class NullHidManager : public HidManager {
    public:
    ~NullHidManager() override {
    }

    bool EnumerateDevices() override {
        return false;
    }

    void SetWindow(void* window) override {
    }

    uint32_t GetDeviceCount() override {
        return 0;
    }

    bool WriteWait(const void* data, uint32_t size) override {
        return false;
    }

    bool Write(const void* data, uint32_t size) override {
        return false;
    }

    bool SetFeature(void* data, uint32_t size) override {
        return false;
    }
};

}

HidManager* HidManager::Create() {
    return new (SMemAlloc(sizeof(NullHidManager), __FILE__, __LINE__, 0)) NullHidManager();
}
