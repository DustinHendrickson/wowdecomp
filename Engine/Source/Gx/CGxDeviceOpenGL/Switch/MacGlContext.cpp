// Nintendo Switch side of the OpenGL device's context (Mac/MacGlContext.h), on Mesa's EGL. The context is a desktop
// OpenGL compatibility context, which keeps the fixed-function pipeline and ARB vertex and fragment programs the
// device uses (Mesa's nouveau driver on the Tegra X1's Maxwell GPU). It draws to the window surface of libnx's default
// NWindow, which the Os layer gives as the game view. There's no display gamma to change.

#include <switch.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <cstdint>
#include <cstdio>
#include "Engine/Source/Gx/CGxDeviceOpenGL/Mac/MacGlContext.h"
#include "Engine/Source/Gx/CGxDeviceOpenGL/Switch/GlSwitch.h"
#include "Engine/Source/Gx/CGxFormat.h"
#include "Engine/Source/Gx/CGxGammaRamp.h"
#include "Engine/Source/Os/Switch/SwitchInput.h"

namespace {

struct SwitchGlContext {
    EGLDisplay m_display;
    EGLSurface m_surface;
    EGLContext m_context;
    int32_t m_requestedInterval;   // the game's vertical sync
    int32_t m_interval;            // as applied, after the battery saver (SwitchFrameInterval)
};

SwitchGlContext* s_current;

}

void* MacGlCreateContext(void* view, const CGxFormat& format) {
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);

    if (display == EGL_NO_DISPLAY || !eglInitialize(display, nullptr, nullptr)) {
        fprintf(stderr, "EGL: no display (0x%x)\n", eglGetError());
        return nullptr;
    }

    if (!eglBindAPI(EGL_OPENGL_API)) {
        fprintf(stderr, "EGL: no desktop OpenGL (0x%x)\n", eglGetError());
        eglTerminate(display);
        return nullptr;
    }

    EGLint depth = 24;
    EGLint stencil = 8;

    switch (format.m_depthFormat) {
        case CGxFormat::Fmt_Ds160:
            depth = 16;
            stencil = 0;
            break;
        case CGxFormat::Fmt_Ds24X:
            stencil = 0;
            break;
        default:
            break;
    }

    EGLint samples = format.m_multisampleCount > 1 ? static_cast<EGLint>(format.m_multisampleCount) : 0;

    const EGLint configAttributes[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, format.m_colorFormat == CGxFormat::Fmt_Argb8888 ? 8 : 0,
        EGL_DEPTH_SIZE, depth,
        EGL_STENCIL_SIZE, stencil,
        EGL_SAMPLE_BUFFERS, samples ? 1 : 0,
        EGL_SAMPLES, samples,
        EGL_NONE,
    };

    EGLConfig config;
    EGLint count = 0;

    if (!eglChooseConfig(display, configAttributes, &config, 1, &count) || !count) {
        fprintf(stderr, "EGL: no config for depth %d stencil %d samples %d (0x%x)\n", depth, stencil, samples, eglGetError());
        eglTerminate(display);
        return nullptr;
    }

    EGLSurface surface = eglCreateWindowSurface(display, config, reinterpret_cast<EGLNativeWindowType>(view), nullptr);

    if (surface == EGL_NO_SURFACE) {
        fprintf(stderr, "EGL: no window surface (0x%x)\n", eglGetError());
        eglTerminate(display);
        return nullptr;
    }

    // No version asked for: Mesa then creates its newest compatibility-profile context.
    const EGLint contextAttributes[] = {
        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR,
        EGL_NONE,
    };

    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttributes);

    if (context == EGL_NO_CONTEXT || !eglMakeCurrent(display, surface, surface, context)) {
        fprintf(stderr, "EGL: no compatibility context (0x%x)\n", eglGetError());

        if (context != EGL_NO_CONTEXT) {
            eglDestroyContext(display, context);
        }

        eglDestroySurface(display, surface);
        eglTerminate(display);
        return nullptr;
    }

    SwitchGlLoadEntryPoints();

    SwitchGlContext* result = new SwitchGlContext();
    result->m_display = display;
    result->m_surface = surface;
    result->m_context = context;
    result->m_requestedInterval = 1;
    result->m_interval = -1;
    s_current = result;

    return result;
}

void MacGlDestroyContext(void* handle) {
    SwitchGlContext* context = static_cast<SwitchGlContext*>(handle);

    if (!context) {
        return;
    }

    eglMakeCurrent(context->m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(context->m_display, context->m_context);
    eglDestroySurface(context->m_display, context->m_surface);
    eglTerminate(context->m_display);

    if (s_current == context) {
        s_current = nullptr;
    }

    delete context;
}

void MacGlSetSwapInterval(void* handle, int32_t interval) {
    SwitchGlContext* context = static_cast<SwitchGlContext*>(handle);

    if (context) {
        context->m_requestedInterval = interval;
        context->m_interval = SwitchFrameInterval(interval);
        eglSwapInterval(context->m_display, context->m_interval);
    }
}

void MacGlUpdate(void* context) {
    // The surface is the whole screen and never changes size.
}

bool MacGlGetDisplayGamma(CGxGammaRamp& ramp) {
    return false;
}

void MacGlSetDisplayGamma(const CGxGammaRamp& ramp) {
}

void MacGlRestoreDisplayGamma() {
}

bool SwitchGlMakeCurrent(void* handle) {
    SwitchGlContext* context = static_cast<SwitchGlContext*>(handle);

    if (!context) {
        if (s_current) {
            eglMakeCurrent(s_current->m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        }

        s_current = nullptr;
        return true;
    }

    s_current = context;
    return eglMakeCurrent(context->m_display, context->m_surface, context->m_surface, context->m_context);
}

void* SwitchGlCurrentContext() {
    return s_current;
}

void SwitchGlSwapBuffers() {
    if (!s_current) {
        return;
    }

    // The battery saver follows docking and the option as they change.
    int32_t interval = SwitchFrameInterval(s_current->m_requestedInterval);

    if (interval != s_current->m_interval) {
        s_current->m_interval = interval;
        eglSwapInterval(s_current->m_display, interval);
    }

    eglSwapBuffers(s_current->m_display, s_current->m_surface);
}

void* SwitchGlGetProcAddress(const char* name) {
    return reinterpret_cast<void*>(eglGetProcAddress(name));
}
