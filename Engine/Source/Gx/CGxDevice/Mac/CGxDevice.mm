// macOS implementation of the adapter queries in Gx/CGxDevice/Win64/CGxDevice.cpp, and the Direct3D device factories,
// which return nullptr: OpenGL is the only API on macOS (see GxApiSupported).

#import <AppKit/AppKit.h>
#include <cstdint>
#include <cstdlib>
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Os/Mac/OsMacWindow.h"

namespace {

int CGxMonitorModeSort(const void* a, const void* b) {
    const CGxMonitorMode* modeA = static_cast<const CGxMonitorMode*>(a);
    const CGxMonitorMode* modeB = static_cast<const CGxMonitorMode*>(b);

    int32_t area = modeA->m_size.x * modeA->m_size.y - modeB->m_size.x * modeB->m_size.y;

    if (area) {
        return area;
    }

    if (modeA->m_size.x != modeB->m_size.x) {
        return modeA->m_size.x - modeB->m_size.x;
    }

    return static_cast<int32_t>(modeA->m_refreshRate) - static_cast<int32_t>(modeB->m_refreshRate);
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

    // Render pixels, matching the window sizes the Os layer reports: the desktop's points times the render scale.
    float scale = OsMacGetRenderScale(nullptr);
    mode.m_size.x = static_cast<int32_t>(CGDisplayModeGetWidth(current) * scale);
    mode.m_size.y = static_cast<int32_t>(CGDisplayModeGetHeight(current) * scale);
    mode.m_refreshRate = RefreshRate(current);
    mode.m_bpp = 32;
    CGDisplayModeRelease(current);

    return 1;
}

int32_t CGxDevice::AdapterMonitorModes(TSGrowableArray<CGxMonitorMode>& modes) {
    // macOS never switches display modes for the game: fullscreen is a borderless window covering the desktop, and a
    // window is sized in render pixels. So the modes the game chooses from (gxResolution, the video options) are window
    // sizes in render pixels that fit the desktop: the usual 4:3 and widescreen sizes plus the desktop itself, at each
    // refresh rate the display offers. The display's own mode list is in pixels and lacks sizes like 1024x768 that the default
    // settings and the resolution check expect.
    static const C2iVector s_sizes[] = {
        { 640, 480 }, { 800, 600 }, { 1024, 768 }, { 1152, 864 }, { 1280, 720 }, { 1280, 800 }, { 1280, 960 },
        { 1280, 1024 }, { 1366, 768 }, { 1440, 900 }, { 1600, 900 }, { 1600, 1200 }, { 1680, 1050 }, { 1920, 1080 },
        { 1920, 1200 }, { 2560, 1440 }, { 2560, 1600 }, { 2880, 1800 }, { 3840, 2160 },
    };

    modes.SetCount(0);

    CGxMonitorMode desktop;

    if (!CGxDevice::AdapterDesktopMode(desktop)) {
        return 0;
    }

    uint32_t rates[16];
    uint32_t numRates = 0;
    CFArrayRef list = CGDisplayCopyAllDisplayModes(CGMainDisplayID(), nullptr);

    if (list) {
        for (CFIndex i = 0; i < CFArrayGetCount(list); i++) {
            uint32_t rate = RefreshRate((CGDisplayModeRef)CFArrayGetValueAtIndex(list, i));
            uint32_t j = 0;

            while (j < numRates && rates[j] != rate) {
                j++;
            }

            if (j == numRates && numRates < sizeof(rates) / sizeof(rates[0])) {
                rates[numRates++] = rate;
            }
        }

        CFRelease(list);
    }

    if (!numRates) {
        rates[numRates++] = desktop.m_refreshRate;
    }

    auto add = [&](int32_t width, int32_t height) {
        for (uint32_t i = 0; i < modes.Count(); i++) {
            if (modes[i].m_size.x == width && modes[i].m_size.y == height) {
                return;
            }
        }

        for (uint32_t i = 0; i < numRates; i++) {
            CGxMonitorMode* mode = modes.New();
            mode->m_size.x = width;
            mode->m_size.y = height;
            mode->m_bpp = 32;
            mode->m_refreshRate = rates[i];
        }
    };

    for (const C2iVector& size : s_sizes) {
        if (size.x <= desktop.m_size.x && size.y <= desktop.m_size.y) {
            add(size.x, size.y);
        }
    }

    add(desktop.m_size.x, desktop.m_size.y);

    // With Retina rendering, the desktop in points too: one render pixel per point, for a lighter window.
    float scale = OsMacGetRenderScale(nullptr);

    if (scale > 1.0f) {
        add(static_cast<int32_t>(desktop.m_size.x / scale), static_cast<int32_t>(desktop.m_size.y / scale));
    }

    qsort(modes.Ptr(), modes.Count(), sizeof(CGxMonitorMode), CGxMonitorModeSort);

    return modes.Count() != 0;
}
