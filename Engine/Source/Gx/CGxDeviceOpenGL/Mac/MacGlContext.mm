// NSOpenGL and CoreGraphics side of the macOS OpenGL device (see MacGlContext.h).

#import <AppKit/AppKit.h>
#include <cstdint>
#include "Engine/Source/Gx/CGxDeviceOpenGL/Mac/MacGlContext.h"
#include "Engine/Source/Gx/CGxFormat.h"
#include "Engine/Source/Gx/CGxGammaRamp.h"
#include "Engine/Source/Os/Mac/OsMacWindow.h"

void* MacGlCreateContext(void* view, const CGxFormat& format) {
    @autoreleasepool {
        NSView* object = (__bridge NSView*)view;

        // The drawable is in render pixels, the units the Os layer reports sizes and mouse positions in: backing pixels
        // with Retina rendering, points without it.
        object.wantsBestResolutionOpenGLSurface = OsMacGetRenderScale((__bridge void*)object.window) > 1.0f;

        uint32_t depth = 24;
        uint32_t stencil = 8;

        switch (format.m_depthFormat) {
            case CGxFormat::Fmt_Ds160:
                depth = 16;
                stencil = 0;
                break;
            case CGxFormat::Fmt_Ds24X:
                stencil = 0;
                break;
            case CGxFormat::Fmt_Ds320:
                depth = 32;
                stencil = 0;
                break;
            default:
                break;
        }

        uint32_t samples = format.m_multisampleCount > 1 ? format.m_multisampleCount : 0;

        NSOpenGLPixelFormatAttribute attributes[32];
        int32_t count = 0;
        attributes[count++] = NSOpenGLPFAOpenGLProfile;
        attributes[count++] = NSOpenGLProfileVersionLegacy;
        attributes[count++] = NSOpenGLPFADoubleBuffer;
        attributes[count++] = NSOpenGLPFAAccelerated;
        attributes[count++] = NSOpenGLPFAColorSize;
        attributes[count++] = 24;
        attributes[count++] = NSOpenGLPFAAlphaSize;
        attributes[count++] = format.m_colorFormat == CGxFormat::Fmt_Argb8888 ? 8 : 0;
        attributes[count++] = NSOpenGLPFADepthSize;
        attributes[count++] = depth;
        attributes[count++] = NSOpenGLPFAStencilSize;
        attributes[count++] = stencil;

        if (samples) {
            attributes[count++] = NSOpenGLPFAMultisample;
            attributes[count++] = NSOpenGLPFASampleBuffers;
            attributes[count++] = 1;
            attributes[count++] = NSOpenGLPFASamples;
            attributes[count++] = samples;
        }

        attributes[count++] = 0;

        NSOpenGLPixelFormat* pixelFormat = [[NSOpenGLPixelFormat alloc] initWithAttributes:attributes];

        if (!pixelFormat) {
            return nullptr;
        }

        NSOpenGLContext* context = [[NSOpenGLContext alloc] initWithFormat:pixelFormat shareContext:nil];

        if (!context) {
            return nullptr;
        }

        context.view = object;
        [context makeCurrentContext];
        [context update];

        return (__bridge_retained void*)context;
    }
}

void MacGlDestroyContext(void* context) {
    if (!context) {
        return;
    }

    NSOpenGLContext* object = (__bridge_transfer NSOpenGLContext*)context;

    if ([NSOpenGLContext currentContext] == object) {
        [NSOpenGLContext clearCurrentContext];
    }

    [object clearDrawable];
}

void MacGlSetSwapInterval(void* context, int32_t interval) {
    GLint value = interval;
    [(__bridge NSOpenGLContext*)context setValues:&value forParameter:NSOpenGLContextParameterSwapInterval];
}

void MacGlUpdate(void* context) {
    if (context) {
        [(__bridge NSOpenGLContext*)context update];
    }
}

bool MacGlGetDisplayGamma(CGxGammaRamp& ramp) {
    CGGammaValue red[256];
    CGGammaValue green[256];
    CGGammaValue blue[256];
    uint32_t count = 0;

    if (CGGetDisplayTransferByTable(CGMainDisplayID(), 256, red, green, blue, &count) != kCGErrorSuccess || count != 256) {
        return false;
    }

    for (uint32_t i = 0; i < 256; i++) {
        ramp.m_red[i] = static_cast<uint16_t>(red[i] * 65535.0f + 0.5f);
        ramp.m_green[i] = static_cast<uint16_t>(green[i] * 65535.0f + 0.5f);
        ramp.m_blue[i] = static_cast<uint16_t>(blue[i] * 65535.0f + 0.5f);
    }

    return true;
}

void MacGlSetDisplayGamma(const CGxGammaRamp& ramp) {
    CGGammaValue red[256];
    CGGammaValue green[256];
    CGGammaValue blue[256];

    for (uint32_t i = 0; i < 256; i++) {
        red[i] = ramp.m_red[i] / 65535.0f;
        green[i] = ramp.m_green[i] / 65535.0f;
        blue[i] = ramp.m_blue[i] / 65535.0f;
    }

    CGSetDisplayTransferByTable(CGMainDisplayID(), 256, red, green, blue);
}

void MacGlRestoreDisplayGamma() {
    CGDisplayRestoreColorSyncSettings();
}
