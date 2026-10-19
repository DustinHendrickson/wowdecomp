#ifndef ENGINE_SOURCE_GX_CGXDEVICEOPENGL_MAC_MACGLCONTEXT_H
#define ENGINE_SOURCE_GX_CGXDEVICEOPENGL_MAC_MACGLCONTEXT_H

// NSOpenGL and CoreGraphics for the macOS OpenGL device (MacGlContext.mm), as plain C++ so the device code that calls
// it stays C++.

#include <cstdint>

class CGxFormat;
class CGxGammaRamp;

// Creates an NSOpenGLContext for the view with the format's color, depth, stencil and sample counts, on the legacy
// (2.1) profile the device's fixed-function and ARB program paths need, attaches it to the view and makes it current.
// Returns the retained context, or nullptr when no matching pixel format exists.
void* MacGlCreateContext(void* view, const CGxFormat& format);
void MacGlDestroyContext(void* context);
void MacGlSetSwapInterval(void* context, int32_t interval);
// After the view's size changes.
void MacGlUpdate(void* context);

// Main display gamma, as 16-bit ramps.
bool MacGlGetDisplayGamma(CGxGammaRamp& ramp);
void MacGlSetDisplayGamma(const CGxGammaRamp& ramp);
void MacGlRestoreDisplayGamma();

#endif
