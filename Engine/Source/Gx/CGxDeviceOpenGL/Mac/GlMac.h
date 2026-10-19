#ifndef ENGINE_SOURCE_GX_CGXDEVICEOPENGL_MAC_GLMAC_H
#define ENGINE_SOURCE_GX_CGXDEVICEOPENGL_MAC_GLMAC_H

// macOS side of GlExt.h, included after Apple's <OpenGL/gl.h> and the PFN typedefs.
//
// GlExt keeps every extension entry point in a function-pointer variable named after the GL function and fills them in
// BindGlExtensions through wglGetProcAddress. Apple's headers declare those same names as functions, so here each
// variable is renamed (glActiveTextureARB becomes GxGl_glActiveTextureARB) and wglGetProcAddress looks the real entry
// point up in OpenGL.framework. GlExt.cpp and the device then build unchanged: they call through the pointers, and the
// ones Apple doesn't provide (NV register combiners, NV vertex programs) stay null, as with a Windows driver that
// lacks the extension.
//
// The WGL calls the shared device code makes act on NSOpenGLContext (GlMac.mm): an HGLRC is an NSOpenGLContext and an
// HDC is the NSView it draws to.

#include <cstdint>

#define glActiveTextureARB GxGl_glActiveTextureARB
#define glBeginQueryARB GxGl_glBeginQueryARB
#define glBindBufferARB GxGl_glBindBufferARB
#define glBindFramebufferEXT GxGl_glBindFramebufferEXT
#define glBindProgramARB GxGl_glBindProgramARB
#define glBindProgramNV GxGl_glBindProgramNV
#define glBindRenderbufferEXT GxGl_glBindRenderbufferEXT
#define glBlendColorEXT GxGl_glBlendColorEXT
#define glBufferDataARB GxGl_glBufferDataARB
#define glBufferSubDataARB GxGl_glBufferSubDataARB
#define glCheckFramebufferStatusEXT GxGl_glCheckFramebufferStatusEXT
#define glClientActiveTextureARB GxGl_glClientActiveTextureARB
#define glCombinerInputNV GxGl_glCombinerInputNV
#define glCombinerOutputNV GxGl_glCombinerOutputNV
#define glCombinerParameterfvNV GxGl_glCombinerParameterfvNV
#define glCombinerParameteriNV GxGl_glCombinerParameteriNV
#define glCombinerStageParameterfvNV GxGl_glCombinerStageParameterfvNV
#define glCompressedTexImage2DARB GxGl_glCompressedTexImage2DARB
#define glCompressedTexSubImage2DARB GxGl_glCompressedTexSubImage2DARB
#define glDeleteBuffersARB GxGl_glDeleteBuffersARB
#define glDeleteFramebuffersEXT GxGl_glDeleteFramebuffersEXT
#define glDeleteProgramsARB GxGl_glDeleteProgramsARB
#define glDeleteProgramsNV GxGl_glDeleteProgramsNV
#define glDeleteQueriesARB GxGl_glDeleteQueriesARB
#define glDeleteRenderbuffersEXT GxGl_glDeleteRenderbuffersEXT
#define glDisableVertexAttribArrayARB GxGl_glDisableVertexAttribArrayARB
#define glDrawRangeElementsEXT GxGl_glDrawRangeElementsEXT
#define glEnableVertexAttribArrayARB GxGl_glEnableVertexAttribArrayARB
#define glEndQueryARB GxGl_glEndQueryARB
#define glFinalCombinerInputNV GxGl_glFinalCombinerInputNV
#define glFramebufferRenderbufferEXT GxGl_glFramebufferRenderbufferEXT
#define glFramebufferTexture1DEXT GxGl_glFramebufferTexture1DEXT
#define glFramebufferTexture2DEXT GxGl_glFramebufferTexture2DEXT
#define glFramebufferTexture3DEXT GxGl_glFramebufferTexture3DEXT
#define glGenBuffersARB GxGl_glGenBuffersARB
#define glGenFramebuffersEXT GxGl_glGenFramebuffersEXT
#define glGenProgramsARB GxGl_glGenProgramsARB
#define glGenProgramsNV GxGl_glGenProgramsNV
#define glGenQueriesARB GxGl_glGenQueriesARB
#define glGenRenderbuffersEXT GxGl_glGenRenderbuffersEXT
#define glGetProgramivARB GxGl_glGetProgramivARB
#define glGetQueryObjectivARB GxGl_glGetQueryObjectivARB
#define glGetQueryObjectuivARB GxGl_glGetQueryObjectuivARB
#define glGetQueryivARB GxGl_glGetQueryivARB
#define glIsProgramARB GxGl_glIsProgramARB
#define glLoadProgramNV GxGl_glLoadProgramNV
#define glLockArraysEXT GxGl_glLockArraysEXT
#define glMapBufferARB GxGl_glMapBufferARB
#define glPointParameterfARB GxGl_glPointParameterfARB
#define glPointParameterfvARB GxGl_glPointParameterfvARB
#define glProgramEnvParameter4fvARB GxGl_glProgramEnvParameter4fvARB
#define glProgramEnvParameters4fvEXT GxGl_glProgramEnvParameters4fvEXT
#define glProgramLocalParameter4fvARB GxGl_glProgramLocalParameter4fvARB
#define glProgramLocalParameters4fvEXT GxGl_glProgramLocalParameters4fvEXT
#define glProgramParameters4fvNV GxGl_glProgramParameters4fvNV
#define glProgramStringARB GxGl_glProgramStringARB
#define glRenderbufferStorageEXT GxGl_glRenderbufferStorageEXT
#define glUnlockArraysEXT GxGl_glUnlockArraysEXT
#define glUnmapBufferARB GxGl_glUnmapBufferARB
#define glVertexAttribPointerARB GxGl_glVertexAttribPointerARB

typedef uint16_t ATOM;
typedef intptr_t LRESULT;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM;

#define WGL_SWAP_MAIN_PLANE 0x00000001

// dlsym on OpenGL.framework; nullptr for anything it doesn't export.
void* wglGetProcAddress(const char* name);

// The Mac stand-in for a WGL pbuffer: a framebuffer object with color and depth/stencil renderbuffers in the device's
// own context. On Windows the device also draws into its pbuffer through the main context (m_pbufferRC is that
// context), then copies the pixels into the target texture. wglMakeCurrent with the returned handle as the HDC binds
// the framebuffer object; with any other HDC it binds the window's. nullptr without EXT_framebuffer_object.
// glCopyTexImage2D, plus what the FBO render-target path does when it creates a depth texture: depth comparison, so
// shadow maps copied out of the pbuffer sample through SHADOW2D lookups as on Windows' D3D path.
void MacGlCopyTexImage2D(GLenum target, GLint level, GLenum internalFormat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border);
#define glCopyTexImage2D MacGlCopyTexImage2D

HDC MacGlCreatePbuffer(int32_t width, int32_t height);
void MacGlDestroyPbuffer(HDC pbuffer);

HDC wglGetCurrentDC();
HGLRC wglGetCurrentContext();
BOOL wglMakeCurrent(HDC dc, HGLRC context);
BOOL wglSwapLayerBuffers(HDC dc, UINT planes);

#endif
