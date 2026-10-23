// Android implementation of the adapter queries in Gx/CGxDevice/Win64/CGxDevice.cpp, and the Direct3D device
// factories, which return nullptr: OpenGL ES (the CGxDeviceGLES device, in the OpenGL slot) is the only API.
//
// The screen is the activity's window (Os/Android). Phone screens are large (2400x1080 and up) for a GPU that also
// drives them, so besides the native size the modes offer smaller ones of the same shape, which Android's hardware
// scaler stretches to the screen at no cost: the player trades sharpness for frame rate in the video options.

#include <cstdint>
#include <cstring>
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Os/Android/AndroidSystem.h"

namespace {

// Heights of the smaller modes, each kept when it is below the screen's.
const int32_t kScaledHeights[] = { 1080, 900, 720, 540 };

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

    if (api != GxApi_OpenGl) {
        return 0;
    }

    // What EGL configs on OpenGL ES 3 devices offer: 8-bit color with or without alpha, 24-bit depth with or without
    // stencil or 16-bit depth, and up to 4x multisampling.
    static const CGxFormat::Format colorFormats[] = { CGxFormat::Fmt_Argb8888, CGxFormat::Fmt_ArgbX888 };
    static const CGxFormat::Format depthFormats[] = { CGxFormat::Fmt_Ds248, CGxFormat::Fmt_Ds24X, CGxFormat::Fmt_Ds160 };
    static const uint32_t sampleCounts[] = { 1, 2, 4 };

    for (CGxFormat::Format color : colorFormats) {
        for (CGxFormat::Format depth : depthFormats) {
            for (uint32_t samples : sampleCounts) {
                CGxFormat format;
                memset(&format, 0, sizeof(format));
                format.m_colorFormat = color;
                format.m_depthFormat = depth;
                format.m_multisampleCount = samples;

                if (!CGxDevice::FormatExists(formats, format)) {
                    *formats.New() = format;
                }
            }
        }
    }

    return formats.Count() != 0;
}

int32_t CGxDevice::DeviceAdapterID(uint16_t& vendorID, uint16_t& deviceID, uint32_t& driverVersionHi, uint32_t& driverVersionLo) {
    // Phone GPUs have no PCI IDs; callers use their defaults.
    vendorID = 0xFFFF;
    deviceID = 0xFFFF;
    driverVersionHi = 0;
    driverVersionLo = 0;

    CGxDevice::Log("CGxDevice::DeviceAdapterID(): RET: %d, VID: %x, DID: %x, DVER: %x.%x", 0, vendorID, deviceID, driverVersionHi, driverVersionLo);

    return 0;
}

int32_t CGxDevice::DeviceAdapterInfer(uint16_t& deviceID) {
    // Class 3: hardware T&L, three or more texture units and pixel shader 2.0, which any OpenGL ES 3 GPU is well past.
    deviceID = 3;
    CGxDevice::Log("CGxDevice::DeviceAdapterInfer(): RET: %d, DID: %x", 1, deviceID);

    return 1;
}

int32_t CGxDevice::AdapterDesktopMode(CGxMonitorMode& mode) {
    int32_t width;
    int32_t height;
    AndroidGetScreenSize(&width, &height);

    mode.m_size.x = width;
    mode.m_size.y = height;
    mode.m_refreshRate = 60;
    mode.m_bpp = 32;

    return 1;
}

int32_t CGxDevice::AdapterMonitorModes(TSGrowableArray<CGxMonitorMode>& modes) {
    CGxMonitorMode desktop;
    CGxDevice::AdapterDesktopMode(desktop);

    modes.SetCount(0);
    *modes.New() = desktop;

    for (int32_t height : kScaledHeights) {
        if (height < desktop.m_size.y) {
            CGxMonitorMode* mode = modes.New();
            mode->m_size.x = (desktop.m_size.x * height / desktop.m_size.y) & ~1;
            mode->m_size.y = height;
            mode->m_bpp = 32;
            mode->m_refreshRate = 60;
        }
    }

    return modes.Count() != 0;
}
