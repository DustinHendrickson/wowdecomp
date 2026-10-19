// macOS versions of the CGxDeviceOpenGl members that are Win32/WGL code on Windows (window class and window, pixel
// formats, context, pbuffers, display modes, gamma, the window procedure). Included at the end of
// CGxDeviceOpenGl.cpp, so they share its file-level state (s_deviceChanging).
//
// On macOS the device draws into the Os game window (Os/Mac/OsMacWindow.h): m_hwnd is the NSWindow, m_hdc its view,
// m_hglrc the NSOpenGLContext. The window tells the device about size and focus changes through a listener, where
// Windows sends WM_SIZE / WM_SETFOCUS to WindowProcGl. The pbuffer behind texture render targets is a framebuffer
// object (MacGlCreatePbuffer). Fullscreen is a borderless window over the desktop at its current resolution.

#include <algorithm>
#include "Engine/Source/Gx/CGxDeviceOpenGL/Mac/MacGlContext.h"
#include "Engine/Source/Os/Mac/OsMacWindow.h"

namespace {

bool s_displayGammaChanged;

CRect ContentRect(void* window) {
    int32_t width;
    int32_t height;
    OsMacGetContentSize(window, &width, &height);

    CRect rect;
    rect.minX = 0.0f;
    rect.minY = 0.0f;
    rect.maxX = static_cast<float>(width);
    rect.maxY = static_cast<float>(height);
    return rect;
}

// Fullscreen, and the two borderless "maximized" modes Windows creates as WS_POPUP windows over the screen.
bool IsFullscreen(const CGxFormat& format) {
    return !format.m_window || format.m_maximize == 1 || format.m_maximize == 2;
}

void WindowListener(void* param, OsMacWindowEvent event) {
    CGxDeviceOpenGl* device = static_cast<CGxDeviceOpenGl*>(param);

    switch (event) {
        case OsMacWindowEvent_Resized:
        case OsMacWindowEvent_Restored: {
            MacGlUpdate(device->m_hglrc);
            CRect rect = ContentRect(device->m_hwnd);
            device->DeviceWM(GxWM_Size, reinterpret_cast<uintptr_t>(&rect), 0);
            break;
        }

        case OsMacWindowEvent_Minimized: {
            CRect rect = ContentRect(device->m_hwnd);
            device->DeviceWM(GxWM_Size, reinterpret_cast<uintptr_t>(&rect), 1);
            break;
        }

        case OsMacWindowEvent_FocusGained:
            device->DeviceWM(GxWM_SetFocus, 0, 0);
            break;

        case OsMacWindowEvent_FocusLost:
            device->DeviceWM(GxWM_KillFocus, 0, 0);
            break;
    }
}

}

void CGxDeviceOpenGl::DeviceDestroyPbuffer() {
    if (this->m_pbufferDC && this->m_context) {
        MacGlDestroyPbuffer(this->m_pbufferDC);
    }

    this->m_pbuffer = nullptr;
    this->m_pbufferDC = nullptr;
    this->m_pbufferRC = nullptr;
}

// Render targets take the copy path (m_renderTargetMode 0): draw into the pbuffer, then copy into the texture. Without
// a pbuffer, setting a texture target would make no context current and every GL call after it would be lost.
void CGxDeviceOpenGl::DeviceCreatePbuffer() {
    if (!this->m_context) {
        return;
    }

    // Windows sizes the pbuffer to the window. Targets are drawn into its lower left corner and copied out, so it is at
    // least as large as the biggest target: 2048 square shadow maps at medium shadow quality.
    const CRect& window = this->DeviceDefWindow();
    int32_t width = std::max(static_cast<int32_t>(window.maxX - window.minX), 2048);
    int32_t height = std::max(static_cast<int32_t>(window.maxY - window.minY), 2048);
    HDC pbuffer = MacGlCreatePbuffer(width, height);

    if (!pbuffer) {
        return;
    }

    this->m_pbuffer = reinterpret_cast<HPBUFFERARB>(pbuffer);
    this->m_pbufferDC = pbuffer;
    this->m_pbufferRC = this->m_hglrc;
    this->m_caps.m_rttFormat[GxTex_Argb8888] = 1;

    // Depth targets for shadow maps: copied out of the pbuffer's depth buffer (MacGlCopyTexImage2D makes them sample with
    // depth comparison, for SHADOW2D lookups).
    this->m_caps.m_rttFormat[GxTex_D24X8] = 1;
}

void CGxDeviceOpenGl::DeviceQueryPbuffer() {
    // A framebuffer object is never lost, unlike a WGL pbuffer after a display mode change.
}

void CGxDeviceOpenGl::IDevSetFocus(int32_t focus, const CGxFormat& format) {
    this->m_windowFocus = focus;

    if (focus && this->m_hwnd) {
        OsMacSetWindowFullscreen(this->m_hwnd, IsFullscreen(format), format.m_size.x, format.m_size.y);
    }
}

int32_t CGxDeviceOpenGl::SetFormatMode(CGxFormat& format) {
    // The display mode is never changed: fullscreen covers the desktop at its current resolution.
    format.m_apiSpecificModeID = 0;
    return 1;
}

void CGxDeviceOpenGl::IDevRemoveGlContext() {
    this->DeviceDestroyPbuffer();

    if (this->m_hglrc) {
        this->ICursorDestroy();
        this->DeviceDestroyPoolAPI();
        this->ITexForceRecreation();
        this->IShaderForceRecreation();
        this->IQueryRelease();
        UnbindGlExtensions();
        MacGlDestroyContext(this->m_hglrc);
        this->m_hglrc = nullptr;
    }

    if (this->m_hdc) {
        if (s_displayGammaChanged) {
            MacGlRestoreDisplayGamma();
            s_displayGammaChanged = false;
        }

        this->m_hdc = nullptr;
    }

    this->m_context = 0;
}

void CGxDeviceOpenGl::DeviceDestroy() {
    CGxDevice::DeviceDestroy();
    this->DeviceDestroyPbuffer();
    s_deviceChanging = 1;

    if (this->m_hwnd) {
        // DestroyWindow on Windows ends in GxWM_Destroy, which drops the context first.
        this->IDevRemoveGlContext();

        if (this->m_ownhwnd) {
            OsMacSetWindowListener(this->m_hwnd, nullptr, nullptr);
            OsMacDestroyGameWindow(this->m_hwnd);
            this->m_hwnd = nullptr;
        }
    }

    this->m_hwndClass = 0;
    s_deviceChanging = 0;
}

void CGxDeviceOpenGl::DeviceSetGamma(float gamma) {
    CGxDevice::DeviceSetGamma(gamma);

    if (!this->m_format.m_window) {
        MacGlSetDisplayGamma(this->m_gammaRamp);
        s_displayGammaChanged = true;
    }
}

void CGxDeviceOpenGl::DeviceSetGamma(const CGxGammaRamp& ramp) {
    CGxDevice::DeviceSetGamma(ramp);

    if (!this->m_format.m_window) {
        MacGlSetDisplayGamma(this->m_gammaRamp);
        s_displayGammaChanged = true;
    }
}

int32_t CGxDeviceOpenGl::IDevAttachGlContext(const CGxFormat& format) {
    this->m_hdc = OsMacGetGameView(this->m_hwnd);

    if (this->m_hdc && (this->m_hglrc = MacGlCreateContext(this->m_hdc, format)) != nullptr) {
        this->m_context = 1;

        // Windows binds the extensions through a throwaway context first (CreateDummyContext); the real one does here.
        BindGlExtensions();
        BindWglExtensions();
        MacGlSetSwapInterval(this->m_hglrc, format.m_vsync != 0);

        this->ISetCaps(format);
        this->DeviceCreatePbuffer();
        this->ICursorCreate(format);
        this->IStateSetContextDefaults();
        this->IRsSync(1);
        this->IShaderConstantsClear();
        this->m_glVendor = glGetString(GL_VENDOR);
        this->m_glRenderer = glGetString(GL_RENDERER);
        this->m_glVersion = glGetString(GL_VERSION);

        return 1;
    }

    this->IDevRemoveGlContext();
    return 0;
}

int32_t CGxDeviceOpenGl::DeviceWM(EGxWM wm, uintptr_t param1, uintptr_t param2) {
    switch (wm) {
        case GxWM_Size:
            if (param2 == 1 || param2 == 2) {
                this->m_windowVisible = 0;
            } else {
                this->m_windowVisible = 1;

                const CRect& window = this->DeviceDefWindow();
                const CRect* rect = reinterpret_cast<const CRect*>(param1);
                bool grew = window.maxX - window.minX < rect->maxX - rect->minX || window.maxY - window.minY < rect->maxY - rect->minY;
                this->DeviceSetDefWindow(*rect);

                if (grew) {
                    this->DeviceDestroyPbuffer();
                    this->DeviceCreatePbuffer();
                } else {
                    this->DeviceQueryPbuffer();
                }

                this->m_viewportDirty = 1;
            }

            break;

        case GxWM_DisplayChange:
            if (this->m_windowVisible) {
                this->DeviceSetDefWindow(*reinterpret_cast<const CRect*>(param1));
                this->m_viewportDirty = 1;
            }

            break;

        case GxWM_Destroy:
            this->IDevRemoveGlContext();
            this->m_windowFocus = 0;
            break;

        case GxWM_SetFocus:
            if (!s_deviceChanging) {
                this->m_windowFocus = 1;
            }

            break;

        case GxWM_KillFocus:
            // Windows minimizes a fullscreen window and restores the desktop mode here; the borderless window simply
            // goes behind whatever takes focus.
            if (!s_deviceChanging) {
                this->m_windowFocus = 0;
            }

            break;

        default:
            break;
    }

    return 0;
}

int32_t CGxDeviceOpenGl::DeviceCreate(void* window, const CGxFormat& format) {
    s_deviceChanging = 1;
    this->m_ownhwnd = 0;

    if (MacGlGetDisplayGamma(this->m_systemGammaRamp) && &this->m_gammaRamp != &this->m_systemGammaRamp) {
        this->m_gammaRamp = this->m_systemGammaRamp;
    }

    this->m_hwnd = static_cast<HWND>(window);
    OsMacSetWindowListener(this->m_hwnd, WindowListener, this);

    CRect rect = ContentRect(this->m_hwnd);
    this->DeviceWM(GxWM_Size, reinterpret_cast<uintptr_t>(&rect), 0);

    if (this->IDevAttachGlContext(format)) {
        s_deviceChanging = 0;

        if (CGxDevice::DeviceCreate(window, format)) {
            return 1;
        }
    }

    this->DeviceDestroy();
    s_deviceChanging = 0;
    return 0;
}

int32_t CGxDeviceOpenGl::DeviceSetFormat(const CGxFormat& format) {
    CGxDevice::Log("CGxDeviceOpenGl::DeviceSetFormat():");
    this->Log(format);

    s_deviceChanging = 1;
    this->IDevRemoveGlContext();

    CGxFormat newFormat = format;

    if (this->SetFormatMode(newFormat)) {
        // Windows destroys and recreates its window for every format; the Mac window stays and is reconfigured.
        if (!this->m_hwnd) {
            int32_t x = newFormat.m_window && (newFormat.m_pos.x || newFormat.m_pos.y) ? newFormat.m_pos.x : -1;
            int32_t y = newFormat.m_window && (newFormat.m_pos.x || newFormat.m_pos.y) ? newFormat.m_pos.y : -1;
            this->m_hwnd = static_cast<HWND>(OsMacCreateGameWindow("World of Warcraft", x, y, newFormat.m_size.x, newFormat.m_size.y));
        }

        if (this->m_hwnd) {
            OsMacSetWindowListener(this->m_hwnd, WindowListener, this);
            this->IDevSetFocus(1, newFormat);

            // The window is sized now; Windows learns this from the WM_SIZE that creating the window sends.
            CRect rect = ContentRect(this->m_hwnd);
            this->DeviceWM(GxWM_Size, reinterpret_cast<uintptr_t>(&rect), 0);

            if (IsFullscreen(newFormat)) {
                newFormat.m_size.x = static_cast<int32_t>(rect.maxX);
                newFormat.m_size.y = static_cast<int32_t>(rect.maxY);
            }

            if (this->IDevAttachGlContext(newFormat)) {
                s_deviceChanging = 0;

                if (CGxDevice::DeviceSetFormat(newFormat)) {
                    this->DeviceSetGamma(this->m_gammaRamp);
                    return 1;
                }
            }
        }
    }

    s_deviceChanging = 0;
    return 0;
}

void CGxDeviceOpenGl::CapsWindowSizeInScreenCoords(CRect& rect) {
    const CRect& window = this->DeviceCurWindow();

    if (this->IDevIsWindowed() && this->m_hwnd) {
        int32_t x;
        int32_t y;
        OsMacGetContentScreenOrigin(this->m_hwnd, &x, &y);

        rect.minX = static_cast<float>(x);
        rect.minY = static_cast<float>(y);
        rect.maxX = static_cast<float>(x) + window.maxX;
        rect.maxY = static_cast<float>(y) + window.maxY;
    } else {
        rect.minY = window.minY;
        rect.minX = window.minX;
        rect.maxY = window.maxY;
        rect.maxX = window.maxX;
    }
}

int32_t CGxDeviceOpenGl::DeviceCreate(GxWindowProc windowProc, const CGxFormat& format) {
    this->m_ownhwnd = 1;

    if (MacGlGetDisplayGamma(this->m_systemGammaRamp) && &this->m_gammaRamp != &this->m_systemGammaRamp) {
        this->m_gammaRamp = this->m_systemGammaRamp;
    }

    // There is no window class to register; the Os layer owns the window type.
    this->m_hwndClass = 1;

    if (CGxDevice::DeviceCreate(windowProc, format)) {
        return 1;
    }

    this->DeviceDestroy();
    return 0;
}

int32_t CGxDevice::OpenGlAdapterFormats(TSGrowableArray<CGxFormat>& formats) {
    // Apple's GL drivers take any of these; the list stands in for the pixel formats Windows enumerates.
    static const CGxFormat::Format colorFormats[] = { CGxFormat::Fmt_Argb8888, CGxFormat::Fmt_ArgbX888 };
    static const CGxFormat::Format depthFormats[] = { CGxFormat::Fmt_Ds248, CGxFormat::Fmt_Ds24X, CGxFormat::Fmt_Ds160 };
    static const uint32_t sampleCounts[] = { 1, 2, 4 };

    for (CGxFormat::Format color : colorFormats) {
        for (CGxFormat::Format depth : depthFormats) {
            for (uint32_t samples : sampleCounts) {
                CGxFormat format;
                memset(&format, 0, sizeof(format));
                format.m_colorFormat = color;
                format.m_depthFormat = depth;
                format.m_multisampleCount = samples;

                if (!CGxDevice::FormatExists(formats, format)) {
                    *formats.New() = format;
                }
            }
        }
    }

    return formats.Count() != 0;
}
