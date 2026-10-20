// WGL stand-ins for the shared OpenGL device code on macOS (see GlMac.h).

#import <AppKit/AppKit.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <vector>
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

PFNGLCOMPRESSEDTEXIMAGE2DARBPROC s_realCompressedTexImage2D;
PFNGLCOMPRESSEDTEXSUBIMAGE2DARBPROC s_realCompressedTexSubImage2D;

void DebugCompressedTexImage2D(GLenum target, GLint level, GLenum format, GLsizei width, GLsizei height, GLint border, GLsizei size, const void* data) {
    s_realCompressedTexImage2D(target, level, format, width, height, border, size, data);
    GLenum error = glGetError();

    if (error != GL_NO_ERROR) {
        fprintf(stderr, "GL error 0x%x from glCompressedTexImage2D (level %d, %dx%d, format 0x%x, size %d, data %p)\n", error, level, width, height, format, size, data);
    }
}

void DebugCompressedTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLsizei size, const void* data) {
    s_realCompressedTexSubImage2D(target, level, x, y, width, height, format, size, data);
    GLenum error = glGetError();

    if (error != GL_NO_ERROR) {
        fprintf(stderr, "GL error 0x%x from glCompressedTexSubImage2D (level %d, %d,%d %dx%d, format 0x%x, size %d)\n", error, level, x, y, width, height, format, size);
    }
}

// An incomplete texture (a mipmapped filter with levels missing or the wrong size) samples as if texturing were off:
// untextured geometry, white in the UI. Each texture bound for a draw is checked once.
void CheckBoundTexturesComplete() {
    static std::set<GLuint> s_checked;
    static int s_reports;

    if (s_reports >= 40) {
        return;
    }

    GLint active = GL_TEXTURE0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);

    for (GLenum unit = 0; unit < 4; unit++) {
        glActiveTexture(GL_TEXTURE0 + unit);

        GLint name = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &name);

        if (!name || !s_checked.insert(static_cast<GLuint>(name)).second) {
            continue;
        }

        GLint minFilter = 0;
        GLint maxLevel = 0;
        GLint width = 0;
        GLint height = 0;
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &minFilter);
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, &maxLevel);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);

        char problem[256] = "";

        if (!width || !height) {
            snprintf(problem, sizeof(problem), "level 0 undefined");
        } else if (minFilter != GL_NEAREST && minFilter != GL_LINEAR) {
            for (GLint level = 1; level <= maxLevel; level++) {
                GLint expectedWidth = std::max(width >> level, 1);
                GLint expectedHeight = std::max(height >> level, 1);
                GLint levelWidth = 0;
                GLint levelHeight = 0;
                glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_WIDTH, &levelWidth);
                glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_HEIGHT, &levelHeight);

                if (levelWidth != expectedWidth || levelHeight != expectedHeight) {
                    snprintf(problem, sizeof(problem), "level %d is %dx%d, expected %dx%d (max level %d)", level, levelWidth, levelHeight, expectedWidth, expectedHeight, maxLevel);
                    break;
                }

                if (expectedWidth == 1 && expectedHeight == 1) {
                    break;
                }
            }
        }

        if (problem[0]) {
            s_reports++;
            GLint format = 0;
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &format);
            fprintf(stderr, "GL incomplete texture %d on unit %u: %dx%d format 0x%x min filter 0x%x: %s\n", name, unit, width, height, format, minFilter, problem);
        }
    }

    glActiveTexture(active);
}

void DebugDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void* indices) {
    CheckBoundTexturesComplete();

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

    // Reading indices back stalls on the GPU, so only the first draws are checked.
    static int s_rangeChecks;

    if (s_reports < 60 && s_rangeChecks++ < 3000 && type == GL_UNSIGNED_SHORT && count > 0) {
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
            { "glCompressedTexImage2DARB", reinterpret_cast<void**>(&s_realCompressedTexImage2D), reinterpret_cast<void*>(DebugCompressedTexImage2D) },
            { "glCompressedTexSubImage2DARB", reinterpret_cast<void**>(&s_realCompressedTexSubImage2D), reinterpret_cast<void*>(DebugCompressedTexSubImage2D) },
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

#undef glCopyTexImage2D

void MacGlCopyTexImage2D(GLenum target, GLint level, GLenum internalFormat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border) {
    glCopyTexImage2D(target, level, internalFormat, x, y, width, height, border);

    if (internalFormat == GL_DEPTH_COMPONENT || internalFormat == GL_DEPTH_COMPONENT16 || internalFormat == GL_DEPTH_COMPONENT24 || internalFormat == GL_DEPTH_COMPONENT32) {
        glTexParameteri(target, GL_TEXTURE_COMPARE_MODE_ARB, GL_COMPARE_R_TO_TEXTURE_ARB);
        glTexParameteri(target, GL_TEXTURE_COMPARE_FUNC_ARB, GL_LEQUAL);
        glTexParameteri(target, GL_DEPTH_TEXTURE_MODE_ARB, GL_LUMINANCE);

        static int s_reports;

        if (getenv("WOWDECOMP_GLDEBUG") && s_reports < 10) {
            s_reports++;
            GLint framebuffer = 0;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &framebuffer);
            fprintf(stderr, "GL depth texture copied %dx%d from framebuffer %d at %d,%d, error 0x%x\n", width, height, framebuffer, x, y, glGetError());
        }
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

namespace {

// Debugging aid: WOWDECOMP_FRAMEDUMP="8,14" writes the frame being presented that many seconds after the first one to
// FrameDump_<seconds>.png in the working directory (the game folder). Unlike the game's own screenshots it works in any
// state, movies included.
void DumpScheduledFrame(NSOpenGLContext* context) {
    static std::vector<double> s_times;
    static bool s_parsed;
    static CFAbsoluteTime s_start;

    if (!s_parsed) {
        s_parsed = true;
        s_start = CFAbsoluteTimeGetCurrent();

        for (const char* times = getenv("WOWDECOMP_FRAMEDUMP"); times && *times;) {
            char* end;
            double seconds = strtod(times, &end);

            if (end == times) {
                break;
            }

            s_times.push_back(seconds);
            times = *end == ',' ? end + 1 : end;
        }
    }

    if (s_times.empty() || CFAbsoluteTimeGetCurrent() - s_start < s_times.front()) {
        return;
    }

    double seconds = s_times.front();
    s_times.erase(s_times.begin());

    NSView* view = context.view;
    NSRect bounds = view.wantsBestResolutionOpenGLSurface ? [view convertRectToBacking:view.bounds] : view.bounds;
    GLsizei width = static_cast<GLsizei>(bounds.size.width);
    GLsizei height = static_cast<GLsizei>(bounds.size.height);

    GLint framebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &framebuffer);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
    glReadBuffer(GL_BACK);

    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, framebuffer);

    @autoreleasepool {
        NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr pixelsWide:width pixelsHigh:height bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:width * 4 bitsPerPixel:32];

        // GL rows run bottom up; the frame's alpha isn't meaningful, so it is made opaque.
        for (GLsizei y = 0; y < height; y++) {
            uint8_t* row = rep.bitmapData + static_cast<size_t>(y) * width * 4;
            memcpy(row, pixels.data() + static_cast<size_t>(height - 1 - y) * width * 4, static_cast<size_t>(width) * 4);

            for (GLsizei x = 0; x < width; x++) {
                row[x * 4 + 3] = 0xFF;
            }
        }

        NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        NSString* name = [NSString stringWithFormat:@"FrameDump_%g.png", seconds];
        [png writeToFile:name atomically:YES];
    }
}

}

BOOL wglSwapLayerBuffers(HDC dc, UINT planes) {
    NSOpenGLContext* context = [NSOpenGLContext currentContext];
    DumpScheduledFrame(context);
    [context flushBuffer];
    return YES;
}
