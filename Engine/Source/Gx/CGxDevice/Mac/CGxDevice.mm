// macOS implementation of the adapter queries in Gx/CGxDevice/Win64/CGxDevice.cpp, and the Direct3D device factories,
// which return nullptr: OpenGL is the only API on macOS (see GxApiSupported).

#import <AppKit/AppKit.h>
#include <cstdint>
#include <cstdlib>
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/Gx.h"

namespace {

int CGxMonitorModeSort(const void* a, const void* b) {
    const CGxMonitorMode* modeA = static_cast<const CGxMonitorMode*>(a);
    const CGxMonitorMode* modeB = static_cast<const CGxMonitorMode*>(b);

    return modeA->m_size.x * modeA->m_size.y - modeB->m_size.x * modeB->m_size.y;
}

uint32_t RefreshRate(CGDisplayModeRef mode) {
    double rate = CGDisplayModeGetRefreshRate(mode);

    // Built-in and ProMotion panels report 0; treat them as 60 Hz like an unspecified Windows mode.
    return rate > 0.0 ? static_cast<uint32_t>(rate + 0.5) : 60;
}

}

CGxDevice* CreateCGxDeviceD3d() {
    return nullptr;
}

CGxDevice* CreateCGxDeviceD3d9Ex() {
    return nullptr;
}

int32_t CGxDevice::AdapterFormats(EGxApi api, TSGrowableArray<CGxFormat>& formats) {
    formats.SetCount(0);
    formats.m_chunk = 64;
    formats.Reserve(256, 0);

    if (api == GxApi_OpenGl) {
        CGxDevice::OpenGlAdapterFormats(formats);
    }

    return formats.Count() != 0;
}

int32_t CGxDevice::DeviceAdapterID(uint16_t& vendorID, uint16_t& deviceID, uint32_t& driverVersionHi, uint32_t& driverVersionLo) {
    // Apple GPUs have no PCI vendor/device IDs to report; callers fall back to their defaults, as on Windows when the
    // adapter can't be identified.
    vendorID = 0xFFFF;
    deviceID = 0xFFFF;
    driverVersionHi = 0;
    driverVersionLo = 0;

    CGxDevice::Log("CGxDevice::DeviceAdapterID(): RET: %d, VID: %x, DID: %x, DVER: %x.%x", 0, vendorID, deviceID, driverVersionHi, driverVersionLo);

    return 0;
}

int32_t CGxDevice::DeviceAdapterInfer(uint16_t& deviceID) {
    // Class 3 is hardware T&L with three or more texture units and pixel shader 2.0, which every Apple GPU exceeds.
    deviceID = 3;
    CGxDevice::Log("CGxDevice::DeviceAdapterInfer(): RET: %d, DID: %x", 1, deviceID);

    return 1;
}

int32_t CGxDevice::AdapterDesktopMode(CGxMonitorMode& mode) {
    CGDisplayModeRef current = CGDisplayCopyDisplayMode(CGMainDisplayID());

    if (!current) {
        return 0;
    }

    // Points, matching the window sizes the Os layer reports.
    mode.m_size.x = static_cast<int32_t>(CGDisplayModeGetWidth(current));
    mode.m_size.y = static_cast<int32_t>(CGDisplayModeGetHeight(current));
    mode.m_refreshRate = RefreshRate(current);
    mode.m_bpp = 32;
    CGDisplayModeRelease(current);

    return 1;
}

int32_t CGxDevice::AdapterMonitorModes(TSGrowableArray<CGxMonitorMode>& modes) {
    modes.SetCount(0);

    CFArrayRef list = CGDisplayCopyAllDisplayModes(CGMainDisplayID(), nullptr);

    if (!list) {
        return 0;
    }

    for (CFIndex i = 0; i < CFArrayGetCount(list); i++) {
        CGDisplayModeRef displayMode = (CGDisplayModeRef)CFArrayGetValueAtIndex(list, i);
        int32_t width = static_cast<int32_t>(CGDisplayModeGetWidth(displayMode));
        int32_t height = static_cast<int32_t>(CGDisplayModeGetHeight(displayMode));

        if (width < 640 || height < 480) {
            continue;
        }

        CGxMonitorMode* mode = modes.New();
        mode->m_size.x = width;
        mode->m_size.y = height;
        mode->m_bpp = 32;
        mode->m_refreshRate = RefreshRate(displayMode);
    }

    CFRelease(list);
    qsort(modes.Ptr(), modes.Count(), sizeof(CGxMonitorMode), CGxMonitorModeSort);

    return modes.Count() != 0;
}
