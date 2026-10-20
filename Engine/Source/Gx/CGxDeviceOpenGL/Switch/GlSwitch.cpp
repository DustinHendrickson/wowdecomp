// WGL stand-ins for the shared OpenGL device code on the Switch (see Mac/GlMac.h): the C++ twin of Mac/GlMac.mm on
// EGL. An HGLRC is the context from MacGlCreateContext and an HDC the NWindow it draws to, or the framebuffer object
// that stands in for the device's pbuffer.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "Engine/Source/Gx/CGxDeviceOpenGL/GlExt.h"
#include "Engine/Source/Gx/CGxDeviceOpenGL/Switch/GlSwitch.h"

namespace {

HDC s_currentDC;

struct SwitchPbuffer {
    GLuint m_framebuffer;
    GLuint m_color;
    GLuint m_depthStencil;
};

SwitchPbuffer* s_pbuffer;

void DeletePbuffer(SwitchPbuffer* pbuffer) {
    glDeleteFramebuffersEXT(1, &pbuffer->m_framebuffer);
    glDeleteRenderbuffersEXT(1, &pbuffer->m_color);
    glDeleteRenderbuffersEXT(1, &pbuffer->m_depthStencil);
    delete pbuffer;
}

}

void* wglGetProcAddress(const char* name) {
    return name ? SwitchGlGetProcAddress(name) : nullptr;
}

HDC wglGetCurrentDC() {
    return SwitchGlCurrentContext() ? s_currentDC : nullptr;
}

HGLRC wglGetCurrentContext() {
    return SwitchGlCurrentContext();
}

#undef glCopyTexImage2D

void MacGlCopyTexImage2D(GLenum target, GLint level, GLenum internalFormat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border) {
    glCopyTexImage2D(target, level, internalFormat, x, y, width, height, border);

    if (internalFormat == GL_DEPTH_COMPONENT || internalFormat == GL_DEPTH_COMPONENT16 || internalFormat == GL_DEPTH_COMPONENT24 || internalFormat == GL_DEPTH_COMPONENT32) {
        glTexParameteri(target, GL_TEXTURE_COMPARE_MODE_ARB, GL_COMPARE_R_TO_TEXTURE_ARB);
        glTexParameteri(target, GL_TEXTURE_COMPARE_FUNC_ARB, GL_LEQUAL);
        glTexParameteri(target, GL_DEPTH_TEXTURE_MODE_ARB, GL_LUMINANCE);
    }
}

HDC MacGlCreatePbuffer(int32_t width, int32_t height) {
    if (!glExtFramebufferObject || width <= 0 || height <= 0) {
        return nullptr;
    }

    SwitchPbuffer* pbuffer = new SwitchPbuffer();

    glGenRenderbuffersEXT(1, &pbuffer->m_color);
    glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, pbuffer->m_color);
    glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_RGBA8, width, height);

    glGenRenderbuffersEXT(1, &pbuffer->m_depthStencil);
    glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, pbuffer->m_depthStencil);
    glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_DEPTH24_STENCIL8_EXT, width, height);
    glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, 0);

    GLint previous = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &previous);

    glGenFramebuffersEXT(1, &pbuffer->m_framebuffer);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, pbuffer->m_framebuffer);
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_RENDERBUFFER_EXT, pbuffer->m_color);
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, pbuffer->m_depthStencil);
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_STENCIL_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, pbuffer->m_depthStencil);
    GLenum status = glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, previous);

    if (status != GL_FRAMEBUFFER_COMPLETE_EXT) {
        DeletePbuffer(pbuffer);
        return nullptr;
    }

    s_pbuffer = pbuffer;
    return reinterpret_cast<HDC>(pbuffer);
}

void MacGlDestroyPbuffer(HDC handle) {
    SwitchPbuffer* pbuffer = reinterpret_cast<SwitchPbuffer*>(handle);

    if (!pbuffer) {
        return;
    }

    GLint current = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &current);

    if (static_cast<GLuint>(current) == pbuffer->m_framebuffer) {
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
    }

    if (s_pbuffer == pbuffer) {
        s_pbuffer = nullptr;
    }

    DeletePbuffer(pbuffer);
}

BOOL wglMakeCurrent(HDC dc, HGLRC context) {
    if (!context) {
        SwitchGlMakeCurrent(nullptr);
        s_currentDC = nullptr;
        return 1;
    }

    // There is one context and one surface: switching between the window and the pbuffer only rebinds the framebuffer.
    if (SwitchGlCurrentContext() != context && !SwitchGlMakeCurrent(context)) {
        return 0;
    }

    s_currentDC = dc;

    if (glExtFramebufferObject && glBindFramebufferEXT) {
        SwitchPbuffer* pbuffer = s_pbuffer && dc == reinterpret_cast<HDC>(s_pbuffer) ? s_pbuffer : nullptr;
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, pbuffer ? pbuffer->m_framebuffer : 0);
    }

    return 1;
}

BOOL wglSwapLayerBuffers(HDC dc, UINT planes) {
    SwitchGlSwapBuffers();
    return 1;
}
