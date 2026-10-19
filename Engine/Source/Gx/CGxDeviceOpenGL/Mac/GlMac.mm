// WGL stand-ins for the shared OpenGL device code on macOS (see GlMac.h).

#import <AppKit/AppKit.h>
#include <dlfcn.h>
#include "Engine/Source/Gx/CGxDeviceOpenGL/GlExt.h"

void* wglGetProcAddress(const char* name) {
    static void* s_framework = dlopen("/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_LAZY | RTLD_LOCAL);
    return s_framework && name ? dlsym(s_framework, name) : nullptr;
}

HDC wglGetCurrentDC() {
    NSOpenGLContext* context = [NSOpenGLContext currentContext];
    return context ? (__bridge HDC)context.view : nullptr;
}

HGLRC wglGetCurrentContext() {
    return (__bridge HGLRC)[NSOpenGLContext currentContext];
}

BOOL wglMakeCurrent(HDC dc, HGLRC context) {
    if (context) {
        [(__bridge NSOpenGLContext*)context makeCurrentContext];
    } else {
        [NSOpenGLContext clearCurrentContext];
    }

    return YES;
}

BOOL wglSwapLayerBuffers(HDC dc, UINT planes) {
    [[NSOpenGLContext currentContext] flushBuffer];
    return YES;
}
