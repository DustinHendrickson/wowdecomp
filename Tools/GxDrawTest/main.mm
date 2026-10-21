// GxDrawTest: the OpenGL Gx device on macOS on its own. It creates the device the way the client does
// (CreateGxDevice with OsWindowProc), then clears and draws through the Gx API.
//
// By default it draws a spinning triangle until the window is closed. With --self-test it reads the back buffer back
// with glReadPixels to check the clear color and the triangle, checks the shader targets the device picked and that
// it follows a window resize, and exits non-zero on failure. It also lays out text with an inline texture
// (|Tpath:height:width|t) through the client's font code and checks that the text after the texture is laid out next
// to it.
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
#include "Engine/Source/Gxu/GxuFont.h"
#include "Engine/Source/Gxu/GxuFontMiscClasses.h"
#include "Engine/Source/Gxu/GxuFontString.h"
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

struct TextLayout {
    int32_t quads;
    float minX;
    float maxX;
    uint32_t textures;
};

// Lays a string out the way a FontString does and measures its glyph quads (x in pixels).
static TextLayout LayOut(CGxFont* face, const char* text) {
    TextLayout layout = { 0, 1e9f, -1e9f, 0 };
    CGxString* string = nullptr;
    C3Vector position = { 0.0f, 0.0f, 0.0f };
    CImVector color = { 0xFF, 0xFF, 0xFF, 0xFF };

    if (!GxuFontCreateString(face, text, 0.02f, position, 1.0f, 0.1f, 0.0f, string, GxVJ_Middle, GxHJ_Left, 0, color, 0.0f, 1.0f) || !string) {
        return layout;
    }

    string->CreateGeometry();

    for (TEXTLINETEXTURE* line : string->m_textLines) {
        if (!line) {
            continue;
        }

        for (uint32_t i = 0; i < line->m_verts.Count(); i++) {
            layout.minX = std::fmin(layout.minX, line->m_verts[i].m_vc.x);
            layout.maxX = std::fmax(layout.maxX, line->m_verts[i].m_vc.x);
        }

        layout.quads += static_cast<int32_t>(line->m_verts.Count() / 4);
    }

    const GXUEMBEDDEDTEXTUREINFO* textures = nullptr;
    layout.textures = string->GetEmbeddedTextureInfo(textures);

    GxuFontDestroyString(string);
    return layout;
}

static void FontChecks() {
    GxuFontInitialize();
    CGxFont* face = nullptr;
    CHECK(GxuFontCreateFont("/System/Library/Fonts/Supplemental/Arial.ttf", 0.02f, face, 0) && face);

    if (!face) {
        return;
    }

    TextLayout plain = LayOut(face, "Move");
    TextLayout icon = LayOut(face, "|TInterface\\Icons\\Test:18:18|t Move");
    TextLayout iconOnly = LayOut(face, "|TInterface\\Icons\\Test:18|t Move");
    printf("     \"Move\": %d quads, x %.1f..%.1f\n", plain.quads, plain.minX, plain.maxX);
    printf("     \"|T..:18:18|t Move\": %d quads, x %.1f..%.1f, %u textures\n", icon.quads, icon.minX, icon.maxX, icon.textures);
    printf("     \"|T..:18|t Move\": %d quads, x %.1f..%.1f, %u textures\n", iconOnly.quads, iconOnly.minX, iconOnly.maxX, iconOnly.textures);

    // " Move" after the texture: the space and four letters, laid out past the texture.
    CHECK(plain.quads == 4);
    CHECK(icon.textures == 1 && icon.quads == plain.quads + 1 && icon.minX > plain.minX + 10.0f);
    CHECK(iconOnly.textures == 1 && iconOnly.quads == plain.quads + 1);

    // Measuring: what FontStrings size themselves by. The text after the texture must count.
    const char* iconText = "|TInterface\\Icons\\Test:18:18|t Move";
    float plainExtent = 0.0f;
    float iconExtent = 0.0f;
    float textureExtent = 0.0f;
    GxuFontGetTextExtent(face, "Move", 4, 0.02f, &plainExtent, 0.0f, 1.0f, 0.0f, 0);
    GxuFontGetTextExtent(face, iconText, static_cast<uint32_t>(strlen(iconText)), 0.02f, &iconExtent, 0.0f, 1.0f, 0.0f, 0);
    GxuFontGetTextExtent(face, "|TInterface\\Icons\\Test:18:18|t", 30, 0.02f, &textureExtent, 0.0f, 1.0f, 0.0f, 0);
    printf("     extents: \"Move\" %.4f, texture %.4f, texture + \" Move\" %.4f\n", plainExtent, textureExtent, iconExtent);
    CHECK(iconExtent > plainExtent + textureExtent * 0.5f);

    // The texture counts as one character.
    float fitExtent = 0.0f;
    uint32_t fits = GxuFontGetMaxCharsWithinWidth(face, iconText, 0.02f, iconExtent + 0.001f, static_cast<uint32_t>(strlen(iconText)), &fitExtent, 0.0f, 1.0f, 0.0f, 0);
    CHECK(fits == 6);

    // What a FontString without a set width does: it measures its text, then checks the text fits that width and cuts it
    // with "..." where it doesn't (CSimpleFontString::GetDisplayText).
    const char* hints[] = {
        "Move",
        iconText,
        "|cffffd100Play|r    |TA:18:18|t Move    |TB:18:18|t Look    |TC:18:18|t|TD:18:18|t Actions 1-3",
    };

    // FontString style flags (0x212 by default, 0x400 without color codes), font heights and layout scales a UI uses.
    const uint32_t styles[] = { 0, 0x212, 0x612 };
    const float heights[] = { 0.02f, 0.0156f, 0.0125f };
    const float scales[] = { 1.0f, 0.9f, 0.64f };
    int32_t cut = 0;

    for (const char* hint : hints) {
        for (uint32_t style : styles) {
            for (float fontHeight : heights) {
                for (float scale : scales) {
                    uint32_t bytes = static_cast<uint32_t>(strlen(hint));
                    float width = 0.0f;
                    GxuFontGetTextExtent(face, hint, bytes, fontHeight * scale, &width, 0.0f, scale, 0.0f, style);
                    uint32_t fit = GxuFontGetMaxCharsWithinWidthAndHeight(face, hint, fontHeight * scale, width, fontHeight * scale * 1.5f, bytes, 0.0f, scale, 0.0f, style);

                    if (fit != bytes) {
                        printf("     cut: style %x height %.4f scale %.2f \"%s\": width %.4f, %u of %u bytes fit\n", style, fontHeight, scale, hint, width, fit, bytes);
                        cut++;
                    }
                }
            }
        }
    }

    printf("     %d of %d measured strings cut\n", cut, static_cast<int32_t>(sizeof(hints) / sizeof(hints[0]) * 27));
    CHECK(cut == 0);

    GxuFontDestroyFont(face);
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

    FontChecks();

    GxDevDestroy(device);

    printf("%s (%d failures)\n", s_failures ? "FAILED" : "PASSED", s_failures);
    return s_failures != 0;
}
