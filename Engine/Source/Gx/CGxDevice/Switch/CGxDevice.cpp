// Nintendo Switch implementation of the adapter queries in Gx/CGxDevice/Win64/CGxDevice.cpp, and the Direct3D device
// factories, which return nullptr: OpenGL (Mesa on the Tegra X1) is the only API.
//
// The screen is the game's window (Os/Switch/Input.cpp): 1280x720 handheld, 1920x1080 docked. Those are the monitor
// modes, at 60 Hz, and the larger one only while docked.

#include <cstdint>
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Os/Mac/OsMacWindow.h"

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
    // NVIDIA's PCI vendor ID; the Tegra X1's GPU has no PCI device ID, so callers use their defaults for it.
    vendorID = 0x10DE;
    deviceID = 0xFFFF;
    driverVersionHi = 0;
    driverVersionLo = 0;

    CGxDevice::Log("CGxDevice::DeviceAdapterID(): RET: %d, VID: %x, DID: %x, DVER: %x.%x", 0, vendorID, deviceID, driverVersionHi, driverVersionLo);

    return 0;
}

int32_t CGxDevice::DeviceAdapterInfer(uint16_t& deviceID) {
    // Class 3: hardware T&L, three or more texture units and pixel shader 2.0. The Maxwell GPU is well past that.
    deviceID = 3;
    CGxDevice::Log("CGxDevice::DeviceAdapterInfer(): RET: %d, DID: %x", 1, deviceID);

    return 1;
}

int32_t CGxDevice::AdapterDesktopMode(CGxMonitorMode& mode) {
    int32_t width;
    int32_t height;
    OsMacGetScreenSize(&width, &height);

    mode.m_size.x = width;
    mode.m_size.y = height;
    mode.m_refreshRate = 60;
    mode.m_bpp = 32;

    return 1;
}

int32_t CGxDevice::AdapterMonitorModes(TSGrowableArray<CGxMonitorMode>& modes) {
    static const C2iVector s_sizes[] = { { 1280, 720 }, { 1920, 1080 } };

    modes.SetCount(0);

    CGxMonitorMode desktop;
    CGxDevice::AdapterDesktopMode(desktop);

    for (const C2iVector& size : s_sizes) {
        if (size.x <= desktop.m_size.x && size.y <= desktop.m_size.y) {
            CGxMonitorMode* mode = modes.New();
            mode->m_size = size;
            mode->m_bpp = 32;
            mode->m_refreshRate = 60;
        }
    }

    return modes.Count() != 0;
}
