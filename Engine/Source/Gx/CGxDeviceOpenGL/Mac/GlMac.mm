// WGL stand-ins for the shared OpenGL device code on macOS (see GlMac.h).

#import <AppKit/AppKit.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <pthread.h>
#include <OpenGL/OpenGL.h>
#include "Engine/Source/Gx/CGxDeviceOpenGL/GlExt.h"

// Debugging aid: with WOWDECOMP_GLDEBUG set, ARB programs the driver rejects are printed with the driver's message and
// the offending line (the device only marks them invalid), and draws are checked for GL errors and for index ranges
// that don't cover the indices drawn.
namespace {

PFNGLPROGRAMSTRINGARBPROC s_realProgramString;
PFNGLDRAWRANGEELEMENTSEXTPROC s_realDrawRangeElements;

PFNGLBINDBUFFERARBPROC s_realBindBuffer;
PFNGLBUFFERDATAARBPROC s_realBufferData;
PFNGLBUFFERSUBDATAARBPROC s_realBufferSubData;
PFNGLMAPBUFFERARBPROC s_realMapBuffer;
PFNGLDELETEBUFFERSARBPROC s_realDeleteBuffers;

// GL calls only reach the context current on the calling thread; from another thread they are lost.
void CheckThread(const char* function, GLenum target, GLuint name) {
    static int s_reports;

    if ((!pthread_main_np() || !CGLGetCurrentContext()) && s_reports < 40) {
        s_reports++;
        fprintf(stderr, "GL %s(0x%x, %u) on %s thread %p, context %p\n", function, target, name, pthread_main_np() ? "main" : "other", pthread_self(), CGLGetCurrentContext());
    }
}

void DebugBindBuffer(GLenum target, GLuint buffer) {
    CheckThread("glBindBuffer", target, buffer);
    s_realBindBuffer(target, buffer);
}

void DebugBufferData(GLenum target, GLsizeiptrARB size, const void* data, GLenum usage) {
    CheckThread("glBufferData", target, static_cast<GLuint>(size));
    s_realBufferData(target, size, data, usage);
}

void DebugBufferSubData(GLenum target, GLintptrARB offset, GLsizeiptrARB size, const void* data) {
    CheckThread("glBufferSubData", target, static_cast<GLuint>(size));
    s_realBufferSubData(target, offset, size, data);
}

void* DebugMapBuffer(GLenum target, GLenum access) {
    CheckThread("glMapBuffer", target, 0);
    return s_realMapBuffer(target, access);
}

void DebugDeleteBuffers(GLsizei count, const GLuint* buffers) {
    CheckThread("glDeleteBuffers", 0, count ? buffers[0] : 0);

    GLint element = 0;
    GLint array = 0;
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING_ARB, &element);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING_ARB, &array);

    for (GLsizei i = 0; i < count; i++) {
        if (buffers[i] && (static_cast<GLint>(buffers[i]) == element || static_cast<GLint>(buffers[i]) == array)) {
            fprintf(stderr, "GL glDeleteBuffers deletes bound buffer %u (element %d, array %d)\n", buffers[i], element, array);
        }
    }

    s_realDeleteBuffers(count, buffers);
}

void DebugProgramString(GLenum target, GLenum format, GLsizei length, const void* string) {
    s_realProgramString(target, format, length, string);

    GLint position = -1;
    glGetIntegerv(GL_PROGRAM_ERROR_POSITION_ARB, &position);

    if (position < 0 || position > length) {
        return;
    }

    const char* text = static_cast<const char*>(string);
    const char* line = text + position;

    while (line > text && line[-1] != '\n') {
        line--;
    }

    const char* end = text + position;

    while (end < text + length && *end != '\n') {
        end++;
    }

    fprintf(stderr, "GL %s program rejected at %d: %s\n    %.*s\n", target == GL_VERTEX_PROGRAM_ARB ? "vertex" : "fragment", position, glGetString(GL_PROGRAM_ERROR_STRING_ARB), static_cast<int>(end - line), line);
}

void DebugDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void* indices) {
    static int s_reports;

    GLenum pending = glGetError();

    if (pending != GL_NO_ERROR && s_reports < 60) {
        s_reports++;
        fprintf(stderr, "GL error 0x%x pending before a draw\n", pending);
    }

    GLint buffer = 0;
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING_ARB, &buffer);

    if (!buffer && reinterpret_cast<uintptr_t>(indices) < 0x100000) {
        if (s_reports < 60) {
            s_reports++;
            fprintf(stderr, "GL draw with no element buffer bound and index pointer %p (count %d, vertex program %d)\n", indices, count, glIsEnabled(GL_VERTEX_PROGRAM_ARB));
        }

        return;
    }

    if (s_reports < 60 && type == GL_UNSIGNED_SHORT && count > 0) {
        uint16_t* data = static_cast<uint16_t*>(malloc(count * sizeof(uint16_t)));

        if (buffer) {
            glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER_ARB, reinterpret_cast<intptr_t>(indices), count * sizeof(uint16_t), data);
        } else {
            memcpy(data, indices, count * sizeof(uint16_t));
        }

        GLuint low = 0xFFFF;
        GLuint high = 0;

        for (GLsizei i = 0; i < count; i++) {
            low = data[i] < low ? data[i] : low;
            high = data[i] > high ? data[i] : high;
        }

        free(data);

        if (low < start || high > end) {
            s_reports++;
            fprintf(stderr, "GL draw range %u-%u doesn't cover indices %u-%u (count %d, vertex program %d)\n", start, end, low, high, count, glIsEnabled(GL_VERTEX_PROGRAM_ARB));
        }
    }

    s_realDrawRangeElements(mode, start, end, count, type, indices);

    GLenum error = glGetError();

    if (error != GL_NO_ERROR && s_reports < 60) {
        s_reports++;
        GLint program = 0;
        glGetProgramivARB(GL_VERTEX_PROGRAM_ARB, GL_PROGRAM_BINDING_ARB, &program);
        fprintf(stderr, "GL error 0x%x from glDrawRangeElements (mode %u, count %d, vertex program %d bound %d, fragment program %d)\n", error, mode, count, glIsEnabled(GL_VERTEX_PROGRAM_ARB), program, glIsEnabled(GL_FRAGMENT_PROGRAM_ARB));
    }
}

}

void* wglGetProcAddress(const char* name) {
    static void* s_framework = dlopen("/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_LAZY | RTLD_LOCAL);
    static bool s_debug = getenv("WOWDECOMP_GLDEBUG") != nullptr;

    void* function = s_framework && name ? dlsym(s_framework, name) : nullptr;

    if (s_debug && function) {
        if (!strcmp(name, "glProgramStringARB")) {
            s_realProgramString = reinterpret_cast<PFNGLPROGRAMSTRINGARBPROC>(function);
            return reinterpret_cast<void*>(DebugProgramString);
        }

        struct {
            const char* name;
            void** real;
            void* wrapper;
        } wrappers[] = {
            { "glBindBufferARB", reinterpret_cast<void**>(&s_realBindBuffer), reinterpret_cast<void*>(DebugBindBuffer) },
            { "glBufferDataARB", reinterpret_cast<void**>(&s_realBufferData), reinterpret_cast<void*>(DebugBufferData) },
            { "glBufferSubDataARB", reinterpret_cast<void**>(&s_realBufferSubData), reinterpret_cast<void*>(DebugBufferSubData) },
            { "glMapBufferARB", reinterpret_cast<void**>(&s_realMapBuffer), reinterpret_cast<void*>(DebugMapBuffer) },
            { "glDeleteBuffersARB", reinterpret_cast<void**>(&s_realDeleteBuffers), reinterpret_cast<void*>(DebugDeleteBuffers) },
        };

        for (auto& wrapper : wrappers) {
            if (!strcmp(name, wrapper.name)) {
                *wrapper.real = function;
                return wrapper.wrapper;
            }
        }

        if (!strcmp(name, "glDrawRangeElementsEXT")) {
            s_realDrawRangeElements = reinterpret_cast<PFNGLDRAWRANGEELEMENTSEXTPROC>(function);
            return reinterpret_cast<void*>(DebugDrawRangeElements);
        }
    }

    return function;
}

HDC wglGetCurrentDC() {
    NSOpenGLContext* context = [NSOpenGLContext currentContext];
    return context ? (__bridge HDC)context.view : nullptr;
}

HGLRC wglGetCurrentContext() {
    return (__bridge HGLRC)[NSOpenGLContext currentContext];
}

namespace {

struct MacPbuffer {
    GLuint m_framebuffer;
    GLuint m_color;
    GLuint m_depthStencil;
};

MacPbuffer* s_pbuffer;

void DeletePbuffer(MacPbuffer* pbuffer) {
    glDeleteFramebuffersEXT(1, &pbuffer->m_framebuffer);
    glDeleteRenderbuffersEXT(1, &pbuffer->m_color);
    glDeleteRenderbuffersEXT(1, &pbuffer->m_depthStencil);
    delete pbuffer;
}

}

HDC MacGlCreatePbuffer(int32_t width, int32_t height) {
    if (!glExtFramebufferObject || width <= 0 || height <= 0) {
        return nullptr;
    }

    MacPbuffer* pbuffer = new MacPbuffer();

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
    MacPbuffer* pbuffer = reinterpret_cast<MacPbuffer*>(handle);

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
        [NSOpenGLContext clearCurrentContext];
        return YES;
    }

    [(__bridge NSOpenGLContext*)context makeCurrentContext];

    if (glExtFramebufferObject && glBindFramebufferEXT) {
        MacPbuffer* pbuffer = s_pbuffer && dc == reinterpret_cast<HDC>(s_pbuffer) ? s_pbuffer : nullptr;
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, pbuffer ? pbuffer->m_framebuffer : 0);
    }

    return YES;
}

BOOL wglSwapLayerBuffers(HDC dc, UINT planes) {
    [[NSOpenGLContext currentContext] flushBuffer];
    return YES;
}
