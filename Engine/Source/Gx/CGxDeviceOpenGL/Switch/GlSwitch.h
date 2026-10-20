#ifndef ENGINE_SOURCE_GX_CGXDEVICEOPENGL_SWITCH_GLSWITCH_H
#define ENGINE_SOURCE_GX_CGXDEVICEOPENGL_SWITCH_GLSWITCH_H

// EGL calls behind the Switch's WGL stand-ins (GlSwitch.cpp), kept apart from the device code so that <EGL/egl.h> and
// <switch.h> stay out of it. The context handle is the HGLRC the device holds (MacGlCreateContext).

bool SwitchGlMakeCurrent(void* context);
void* SwitchGlCurrentContext();
void SwitchGlSwapBuffers();
void* SwitchGlGetProcAddress(const char* name);

// Fills the GL 1.x entry points (GlSwitchEntryPoints.cpp) from the current context's driver.
void SwitchGlLoadEntryPoints();

#endif
