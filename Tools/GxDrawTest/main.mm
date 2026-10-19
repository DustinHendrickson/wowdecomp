// GxDrawTest: the OpenGL Gx device on macOS on its own. It creates the device the way the client does
// (CreateGxDevice with OsWindowProc), then clears and draws through the Gx API.
//
// By default it draws a spinning triangle until the window is closed. With --self-test it reads the back buffer back
// with glReadPixels to check the clear color and the triangle, checks the shader targets the device picked and that
// it follows a window resize, and exits non-zero on failure.
#import <AppKit/AppKit.h>
#define GL_SILENCE_DEPRECATION 1
#import <OpenGL/gl.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxFormat.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Gxu/Gxu.h"
#include "Engine/Source/Os/Gui.h"
#include "Engine/Source/Os/Input.h"
#include "Engine/Source/Os/Mac/OsMacWindow.h"
#include "Engine/Source/Os/Time.h"
#include "Engine/Source/Tempest/C2Vector.h"
#include "Engine/Source/Tempest/C3Vector.h"
#include "Engine/Source/Tempest/C44Matrix.h"
#include "Engine/Source/Tempest/CImVector.h"

static int s_failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); s_failures++; } else { printf("ok   %s\n", #cond); } } while (0)

static void Pixel(int32_t x, int32_t y, uint8_t rgba[4]) {
    glReadBuffer(GL_BACK);
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

static bool Near(const uint8_t rgba[4], int r, int g, int b) {
    return std::abs(rgba[0] - r) <= 2 && std::abs(rgba[1] - g) <= 2 && std::abs(rgba[2] - b) <= 2;
}

static void DrawFrame(float time, bool solid) {
    GxXformSetViewport(0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f);

    CImVector clear(0x80, 0x40, 0x20, 0xFF);
    GxSceneClear(GxBufferBit_Color | GxBufferBit_Depth, clear);

    C44Matrix view;
    C44Matrix projection;
    GxuXformCreateOrtho(0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 500.0f, projection);
    g_theGxDevicePtr->XformSetView(view);
    g_theGxDevicePtr->XformSetProjection(projection);

    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_Fog, 0);
    GxRsSet(GxRs_DepthTest, 0);
    GxRsSet(GxRs_Culling, 0);
    GxRsSet(GxRs_BlendingMode, 0);

    float spin = solid ? 0.0f : time;
    C3Vector positions[3];

    for (int i = 0; i < 3; i++) {
        float angle = spin + i * 2.0943951f + 1.5707963f;
        positions[i] = C3Vector(0.5f + 0.3f * std::cos(angle), 0.5f + 0.3f * std::sin(angle), 0.0f);
    }

    CImVector colors[3] = {
        solid ? CImVector(0x00, 0xFF, 0xFF, 0xFF) : CImVector(0x00, 0x00, 0xFF, 0xFF),
        solid ? CImVector(0x00, 0xFF, 0xFF, 0xFF) : CImVector(0x00, 0xFF, 0x00, 0xFF),
        solid ? CImVector(0x00, 0xFF, 0xFF, 0xFF) : CImVector(0xFF, 0x00, 0x00, 0xFF),
    };

    static const uint16_t indices[3] = { 0, 1, 2 };
    GxPrimLockVertexPtrs(3, positions, sizeof(C3Vector), nullptr, 0, colors, sizeof(CImVector), nullptr, 0, nullptr, 0, nullptr, 0);
    GxDrawLockedElements(GxPrim_Triangles, 3, indices);
    GxPrimUnlockVertexPtrs();
}

int main(int argc, char** argv) {
    bool interactive = !(argc > 1 && !strcmp(argv[1], "--self-test"));

    OsTimeStartup(OsTimeManager::BestAvailable);
    OsInputInitialize();

    CGxFormat format;
    memset(&format, 0, sizeof(format));
    format.m_hwTnL = true;
    format.m_window = 1;
    format.m_size.x = 1024;
    format.m_size.y = 768;
    format.m_colorFormat = CGxFormat::Fmt_Argb8888;
    format.m_depthFormat = CGxFormat::Fmt_Ds248;
    format.m_multisampleCount = 1;
    format.m_backBufferCount = 1;
    format.m_vsync = 1;

    // -1 lets the device pick the best shader target, as the game's default format does.
    for (int32_t& target : format.m_fixedFunction) {
        target = -1;
    }

    TSGrowableArray<CGxMonitorMode> modes;
    CGxMonitorMode desktop;
    CHECK(GxAdapterDesktopMode(desktop) && desktop.m_size.x > 0);
    CHECK(GxAdapterMonitorModes(modes) && modes.Count() > 0);
    printf("     desktop %dx%d @ %u Hz, %u modes\n", desktop.m_size.x, desktop.m_size.y, desktop.m_refreshRate, modes.Count());

    CGxDevice* device = CreateGxDevice(GxApi_OpenGl, OsWindowProc, format);
    CHECK(device != nullptr);

    if (!device) {
        return 1;
    }

    OsGuiSetGxWindow(device->DeviceWindow());
    CHECK(device->DeviceWindow() != nullptr);
    CHECK(GxHasContext());
    CHECK(GxIsWindowVisible());
    printf("     GL_VENDOR   %s\n     GL_RENDERER %s\n     GL_VERSION  %s\n", glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));

    const CGxCaps& caps = device->m_caps;
    printf("     caps: %d TMUs, vertex shader target %s, pixel shader target %s, max anisotropy %u\n", caps.m_numTmus, CGxDevice::s_vertexShaderTargetNames[caps.m_shaderTargets[GxSh_Vertex]], CGxDevice::s_pixelShaderTargetNames[caps.m_shaderTargets[GxSh_Pixel]], caps.m_maxTexAnisotropy);
    CHECK(!strcmp(CGxDevice::s_vertexShaderTargetNames[caps.m_shaderTargets[GxSh_Vertex]], "arbvp1"));
    CHECK(!strcmp(CGxDevice::s_pixelShaderTargetNames[caps.m_shaderTargets[GxSh_Pixel]], "arbfp1"));

    const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    CHECK(extensions && strstr(extensions, "GL_ARB_vertex_program") && strstr(extensions, "GL_ARB_fragment_program"));
    CHECK(extensions && strstr(extensions, "GL_EXT_framebuffer_object") && strstr(extensions, "GL_ARB_vertex_buffer_object"));

    int32_t width;
    int32_t height;
    OsMacGetContentSize(device->DeviceWindow(), &width, &height);

    // Frames until the window is on screen, then a checked one.
    for (int frame = 0; frame < 30; frame++) {
        OSINPUT id;
        int32_t p[4];

        while (OsInputGet(&id, &p[0], &p[1], &p[2], &p[3])) {
        }

        DrawFrame(0.0f, true);

        if (frame == 29) {
            uint8_t center[4];
            uint8_t corner[4];
            Pixel(width / 2, height / 2, center);
            Pixel(4, 4, corner);
            printf("     center rgba %u %u %u %u, corner rgba %u %u %u %u\n", center[0], center[1], center[2], center[3], corner[0], corner[1], corner[2], corner[3]);
            CHECK(Near(center, 0xFF, 0xFF, 0x00));
            CHECK(Near(corner, 0x20, 0x40, 0x80));
            CHECK(glGetError() == GL_NO_ERROR);
        }

        GxScenePresent();
    }

    if (interactive) {
        printf("Drawing; close the window to quit.\n");
        float start = OsGetAsyncTimeSec();

        for (bool running = true; running;) {
            OSINPUT id;
            int32_t p[4];

            while (OsInputGet(&id, &p[0], &p[1], &p[2], &p[3])) {
                if (id == OS_INPUT_CLOSE) {
                    running = false;
                }
            }

            DrawFrame(OsGetAsyncTimeSec() - start, false);
            GxScenePresent();
        }
    }

    // Resize: the device's default window follows the window.
    [(__bridge NSWindow*)device->DeviceWindow() setContentSize:NSMakeSize(800, 600)];
    OSINPUT id;
    int32_t p[4];

    while (OsInputGet(&id, &p[0], &p[1], &p[2], &p[3])) {
    }

    const CRect& window = device->DeviceDefWindow();
    printf("     after resize: def window %.0fx%.0f\n", window.maxX - window.minX, window.maxY - window.minY);
    // Render pixels: 800x600 points times the render scale.
    float scale = OsMacGetRenderScale(device->DeviceWindow());
    CHECK(window.maxX - window.minX == 800 * scale && window.maxY - window.minY == 600 * scale);
    DrawFrame(0.0f, true);
    uint8_t center[4];
    Pixel(static_cast<int32_t>(400 * scale), static_cast<int32_t>(300 * scale), center);
    CHECK(Near(center, 0xFF, 0xFF, 0x00));
    GxScenePresent();

    GxDevDestroy(device);

    printf("%s (%d failures)\n", s_failures ? "FAILED" : "PASSED", s_failures);
    return s_failures != 0;
}
