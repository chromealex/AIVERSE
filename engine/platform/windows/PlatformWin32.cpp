// Windows: Win32 window + WGL OpenGL 3.3 core (GL backend). A D3D11 backend can replace WGL later
// without touching anything above the RHI.
#include "../PlatformCommon.h"
#include "../../gfx/Gfx.h"
#include "../../gfx/gl/GLApi.h"

#include <shellapi.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#if defined(_MSC_VER)
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#endif

namespace aiverse::platform {
using detail::state;

namespace {
HWND g_hwnd = nullptr;
bool g_pointerLocked = false;
HDC g_hdc = nullptr;
HGLRC g_rc = nullptr;
bool g_surrogatePending = false;
wchar_t g_highSurrogate = 0;

std::wstring widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(size_t(n > 0 ? n - 1 : 0), L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}
std::string narrow(const wchar_t* w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n > 0 ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

uint8_t currentMods() {
    uint8_t m = 0;
    if (GetKeyState(VK_SHIFT) & 0x8000) m |= kModShift;
    if (GetKeyState(VK_CONTROL) & 0x8000) m |= kModCtrl;
    if (GetKeyState(VK_MENU) & 0x8000) m |= kModAlt;
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) m |= kModSuper;
    return m;
}

Key mapVK(WPARAM vk, LPARAM lp) {
    if (vk >= 'A' && vk <= 'Z') return Key(int(Key::A) + int(vk - 'A'));
    if (vk >= '0' && vk <= '9') return Key(int(Key::Num0) + int(vk - '0'));
    if (vk >= VK_F1 && vk <= VK_F12) return Key(int(Key::F1) + int(vk - VK_F1));
    const bool ext = (lp >> 24) & 1;
    switch (vk) {
        case VK_ESCAPE: return Key::Escape; case VK_RETURN: return Key::Enter; case VK_TAB: return Key::Tab;
        case VK_BACK: return Key::Backspace; case VK_DELETE: return Key::Delete; case VK_INSERT: return Key::Insert;
        case VK_SPACE: return Key::Space; case VK_LEFT: return Key::Left; case VK_RIGHT: return Key::Right;
        case VK_UP: return Key::Up; case VK_DOWN: return Key::Down; case VK_HOME: return Key::Home; case VK_END: return Key::End;
        case VK_PRIOR: return Key::PageUp; case VK_NEXT: return Key::PageDown;
        case VK_SHIFT: return MapVirtualKeyW(UINT((lp >> 16) & 0xff), MAPVK_VSC_TO_VK_EX) == VK_RSHIFT ? Key::RightShift : Key::LeftShift;
        case VK_CONTROL: return ext ? Key::RightCtrl : Key::LeftCtrl;
        case VK_MENU: return ext ? Key::RightAlt : Key::LeftAlt;
        case VK_LWIN: return Key::LeftSuper; case VK_RWIN: return Key::RightSuper;
        case VK_OEM_MINUS: return Key::Minus; case VK_OEM_PLUS: return Key::Equal; case VK_OEM_COMMA: return Key::Comma;
        case VK_OEM_PERIOD: return Key::Period; case VK_OEM_2: return Key::Slash; case VK_OEM_1: return Key::Semicolon;
        default: return Key::Unknown;
    }
}

void updateSize() {
    auto& S = state();
    RECT r; GetClientRect(g_hwnd, &r);
    UINT dpi = 96;
    if (auto fn = reinterpret_cast<UINT(WINAPI*)(HWND)>(GetProcAddress(GetModuleHandleW(L"user32"), "GetDpiForWindow"))) dpi = fn(g_hwnd);
    S.scale = S.cfg.highDpi ? float(dpi) / 96.0f : 1.0f;
    S.fbW = r.right - r.left; S.fbH = r.bottom - r.top;
    S.winW = float(S.fbW) / S.scale; S.winH = float(S.fbH) / S.scale;
}

// Window style from the config: no resize border / maximize box for a fixed-size window.
DWORD windowStyle() {
    DWORD st = WS_OVERLAPPEDWINDOW;
    if (!state().cfg.resizable) st &= ~DWORD(WS_THICKFRAME | WS_MAXIMIZEBOX);
    return st;
}
// Frame size (window rect minus client rect) for the current style.
SIZE frameExtra(HWND h) {
    RECT r{0, 0, 100, 100};
    AdjustWindowRectEx(&r, DWORD(GetWindowLongPtrW(h, GWL_STYLE)), FALSE, DWORD(GetWindowLongPtrW(h, GWL_EXSTYLE)));
    return SIZE{(r.right - r.left) - 100, (r.bottom - r.top) - 100};
}

LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto& S = state();
    Event e;
    auto mouse = [&](EventType t, int button) {
        e.type = t; e.button = button;
        e.x = float(short(LOWORD(lp))) / S.scale; e.y = float(short(HIWORD(lp))) / S.scale;
        e.mods = currentMods();
        detail::dispatch(e);
    };
    switch (msg) {
    case WM_CLOSE: e.type = EventType::QuitRequested; detail::dispatch(e); S.quit = true; return 0;
    case WM_SIZE: updateSize(); e.type = EventType::Resized; detail::dispatch(e); return 0;
    case WM_GETMINMAXINFO: {   // game.json window.minWidth / minHeight (logical points -> pixels + frame)
        const AppConfig& c = S.cfg;
        if ((c.minWidth > 0 || c.minHeight > 0) && !S.fullscreen) {
            auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
            const SIZE fr = frameExtra(h);
            const float k = S.scale > 0 ? S.scale : 1.0f;
            if (c.minWidth > 0) mm->ptMinTrackSize.x = LONG(float(c.minWidth) * k) + fr.cx;
            if (c.minHeight > 0) mm->ptMinTrackSize.y = LONG(float(c.minHeight) * k) + fr.cy;
            return 0;
        }
        break;
    }
    case WM_SIZING: {   // game.json window.keepAspect: the dragged edge decides, the other side follows
        const AppConfig& c = S.cfg;
        if (!c.keepAspect || c.width <= 0 || c.height <= 0) break;
        auto* r = reinterpret_cast<RECT*>(lp);
        const SIZE fr = frameExtra(h);
        const float aspect = float(c.width) / float(c.height);
        LONG cw = (r->right - r->left) - fr.cx, ch = (r->bottom - r->top) - fr.cy;
        if (wp == WMSZ_TOP || wp == WMSZ_BOTTOM) cw = LONG(float(ch) * aspect);
        else ch = LONG(float(cw) / aspect);
        if (wp == WMSZ_TOP || wp == WMSZ_TOPLEFT || wp == WMSZ_TOPRIGHT) r->top = r->bottom - ch - fr.cy; else r->bottom = r->top + ch + fr.cy;
        if (wp == WMSZ_LEFT || wp == WMSZ_TOPLEFT || wp == WMSZ_BOTTOMLEFT) r->left = r->right - cw - fr.cx; else r->right = r->left + cw + fr.cx;
        return TRUE;
    }
    case WM_DPICHANGED: {
        RECT* r = reinterpret_cast<RECT*>(lp);
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        updateSize(); return 0;
    }
    case WM_MOUSEMOVE: if (!g_pointerLocked) mouse(EventType::MouseMove, 0); return 0;
    case WM_INPUT: {
        RAWINPUT input{}; UINT size = sizeof(input);
        if (g_pointerLocked && GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER)) == size && input.header.dwType == RIM_TYPEMOUSE) {
            Event motion; motion.type = EventType::MouseMove;
            motion.deltaX = float(input.data.mouse.lLastX); motion.deltaY = float(input.data.mouse.lLastY);
            detail::dispatch(motion);
        }
        break;
    }
    case WM_LBUTTONDOWN: SetCapture(h); mouse(EventType::MouseDown, 0); return 0;
    case WM_LBUTTONUP: ReleaseCapture(); mouse(EventType::MouseUp, 0); return 0;
    case WM_RBUTTONDOWN: SetCapture(h); mouse(EventType::MouseDown, 1); return 0;
    case WM_RBUTTONUP: ReleaseCapture(); mouse(EventType::MouseUp, 1); return 0;
    case WM_MBUTTONDOWN: SetCapture(h); mouse(EventType::MouseDown, 2); return 0;
    case WM_MBUTTONUP: ReleaseCapture(); mouse(EventType::MouseUp, 2); return 0;
    case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL: {
        POINT p{short(LOWORD(lp)), short(HIWORD(lp))}; ScreenToClient(h, &p);
        e.type = EventType::MouseWheel; e.x = float(p.x) / S.scale; e.y = float(p.y) / S.scale;
        float d = float(GET_WHEEL_DELTA_WPARAM(wp)) / float(WHEEL_DELTA);
        if (msg == WM_MOUSEWHEEL) e.wheelY = d; else e.wheelX = -d;
        e.mods = currentMods();
        detail::dispatch(e); return 0;
    }
    case WM_KEYDOWN: case WM_SYSKEYDOWN: case WM_KEYUP: case WM_SYSKEYUP:
        e.type = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) ? EventType::KeyDown : EventType::KeyUp;
        e.key = mapVK(wp, lp); e.mods = currentMods(); e.repeat = (lp >> 30) & 1;
        detail::dispatch(e);
        // F11 and Alt+Enter: the usual fullscreen toggles on Windows
        if (e.type == EventType::KeyDown && !e.repeat && S.cfg.allowFullscreen && (wp == VK_F11 || (msg == WM_SYSKEYDOWN && wp == VK_RETURN)))
            detail::applyFullscreen(!state().fullscreen);
        if (msg == WM_SYSKEYDOWN && wp == VK_F4) break; // keep Alt+F4
        return 0;
    case WM_CHAR: {
        wchar_t c = wchar_t(wp);
        uint32_t cp = c;
        if (c >= 0xD800 && c <= 0xDBFF) { g_highSurrogate = c; g_surrogatePending = true; return 0; }
        if (c >= 0xDC00 && c <= 0xDFFF && g_surrogatePending) { cp = 0x10000 + ((uint32_t(g_highSurrogate) - 0xD800) << 10) + (c - 0xDC00); g_surrogatePending = false; }
        if (cp >= 32 && cp != 127) { e.type = EventType::Char; e.codepoint = cp; detail::dispatch(e); }
        return 0;
    }
    case WM_SETFOCUS: e.type = EventType::FocusGained; detail::dispatch(e); return 0;
    case WM_KILLFOCUS: setPointerLock(false); e.type = EventType::FocusLost; detail::dispatch(e); return 0;
    default: break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

bool createGLContext() {
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof pfd; pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 32; pfd.cDepthBits = 24; pfd.cStencilBits = 8;
    int pf = ChoosePixelFormat(g_hdc, &pfd);
    if (!pf || !SetPixelFormat(g_hdc, pf, &pfd)) return false;
    HGLRC legacy = wglCreateContext(g_hdc);
    wglMakeCurrent(g_hdc, legacy);
    using CreateCtxFn = HGLRC(WINAPI*)(HDC, HGLRC, const int*);
    auto createCtx = reinterpret_cast<CreateCtxFn>(wglGetProcAddress("wglCreateContextAttribsARB"));
    if (!createCtx) return false;
    const int attrs[] = {0x2091 /*MAJOR*/, 3, 0x2092 /*MINOR*/, 3, 0x9126 /*PROFILE_MASK*/, 0x1 /*CORE*/, 0};
    g_rc = createCtx(g_hdc, nullptr, attrs);
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(legacy);
    if (!g_rc) return false;
    wglMakeCurrent(g_hdc, g_rc);
    using SwapFn = BOOL(WINAPI*)(int);
    if (auto swap = reinterpret_cast<SwapFn>(wglGetProcAddress("wglSwapIntervalEXT"))) swap(1);
    return gfx::loadGLFunctions();
}
} // namespace

const char* name() { return "windows"; }
void setTitle(const std::string& t) { if (g_hwnd) SetWindowTextW(g_hwnd, widen(t).c_str()); }

namespace detail {
// Borderless window over the whole monitor; the previous placement is restored on the way back.
void applyFullscreen(bool on) {
    static WINDOWPLACEMENT saved = [] { WINDOWPLACEMENT w{}; w.length = sizeof(w); return w; }();
    static LONG_PTR savedStyle = 0;
    if (!g_hwnd || on == state().fullscreen) return;
    LONG_PTR style = GetWindowLongPtrW(g_hwnd, GWL_STYLE);
    if (on) {
        MONITORINFO mi{}; mi.cbSize = sizeof(mi);
        if (!GetWindowPlacement(g_hwnd, &saved) || !GetMonitorInfoW(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) return;
        savedStyle = style;
        SetWindowLongPtrW(g_hwnd, GWL_STYLE, style & ~LONG_PTR(WS_OVERLAPPEDWINDOW));
        SetWindowPos(g_hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    } else {
        SetWindowLongPtrW(g_hwnd, GWL_STYLE, savedStyle ? savedStyle : (style | LONG_PTR(windowStyle())));   // the style before fullscreen (fixed size stays fixed)
        SetWindowPlacement(g_hwnd, &saved);
        SetWindowPos(g_hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    state().fullscreen = on;
}
} // namespace detail

bool openUrl(const std::string& url) {
    if (!detail::urlAllowed(url)) return false;
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}
std::string clipboardText() {
    std::string out;
    if (!OpenClipboard(g_hwnd)) return out;
    if (HANDLE hd = GetClipboardData(CF_UNICODETEXT)) {
        if (auto* w = static_cast<const wchar_t*>(GlobalLock(hd))) { out = narrow(w); GlobalUnlock(hd); }
    }
    CloseClipboard();
    return out;
}
void setClipboardText(const std::string& s) {
    if (!OpenClipboard(g_hwnd)) return;
    EmptyClipboard();
    std::wstring w = widen(s);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
    if (mem) {
        memcpy(GlobalLock(mem), w.c_str(), (w.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(mem);
        SetClipboardData(CF_UNICODETEXT, mem);
    }
    CloseClipboard();
}

int run(const AppConfig& cfgIn, AppCallbacks cb) {
    auto& S = state();
    S.cfg = cfgIn; S.cb = std::move(cb);
    const AppConfig& cfg = S.cfg;   // configure (below) may change it

    wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::string exeDir = narrow(exe);
    exeDir = exeDir.substr(0, exeDir.find_last_of("\\/"));
    auto getenvW = [](const wchar_t* n) -> std::string {
        wchar_t buf[2048];
        DWORD len = GetEnvironmentVariableW(n, buf, 2048);
        return len > 0 && len < 2048 ? narrow(buf) : std::string();
    };
    if (std::string env = getenvW(L"AIVERSE_CONTENT"); !env.empty()) S.contentRoot = env;
    else if (GetFileAttributesW(widen(exeDir + "\\content").c_str()) != INVALID_FILE_ATTRIBUTES) S.contentRoot = exeDir + "\\content";
    else S.contentRoot = "content";
    if (std::string ad = getenvW(L"APPDATA"); !ad.empty()) S.userDir = ad + "\\" + cfg.title;
    if (S.cb.configure) S.cb.configure(S.cfg);   // game data: window size and mode

    if (auto fn = reinterpret_cast<BOOL(WINAPI*)(HANDLE)>(GetProcAddress(GetModuleHandleW(L"user32"), "SetProcessDpiAwarenessContext")))
        fn(reinterpret_cast<HANDLE>(-4)); // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2

    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc; wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW; wc.lpfnWndProc = wndProc;
    wc.hInstance = inst; wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512) /* IDC_ARROW */); wc.lpszClassName = L"AIVERSEWindow";
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    // painted until the first frame is presented (games: the splash background, see AppConfig::clearColor)
    wc.hbrBackground = CreateSolidBrush(RGB(int(cfg.clearColor[0] * 255), int(cfg.clearColor[1] * 255), int(cfg.clearColor[2] * 255)));
    RegisterClassExW(&wc);
    RECT r{0, 0, cfg.width, cfg.height};
    const DWORD style = windowStyle();
    AdjustWindowRect(&r, style, FALSE);
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, widen(cfg.title).c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT,
                             r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, nullptr);
    g_hdc = GetDC(g_hwnd);
    if (!createGLContext()) { MessageBoxW(nullptr, L"OpenGL 3.3 is required", L"AIVERSE", MB_ICONERROR); return 1; }
    ShowWindow(g_hwnd, cfg.windowMode == AppConfig::WindowMode::Maximized && cfg.resizable ? SW_SHOWMAXIMIZED : SW_SHOW);
    if (cfg.windowMode == AppConfig::WindowMode::Fullscreen) detail::applyFullscreen(true);
    updateSize();

    auto device = gfx::createGLDevice(gfx::Backend::GL33);
    if (S.cb.init) S.cb.init(*device);
    // CI hooks (same as Linux): AIVERSE_SCREENSHOT=out.ppm AIVERSE_EXIT_AFTER_FRAMES=N
    const detail::CaptureRequest capture = detail::captureRequestFromEnv();
    detail::FrameClock clock(capture.active());
    long frameNo = 0;
    while (!S.quit) {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            if (m.message == WM_QUIT) S.quit = true;
            TranslateMessage(&m); DispatchMessageW(&m);
        }
        if (S.quit) break;
        if (IsIconic(g_hwnd) || S.fbW <= 0 || S.fbH <= 0) { Sleep(16); clock.reset(); continue; }
        double dt = clock.tick(std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count());
        device->beginFrame(S.fbW, S.fbH);
        if (S.cb.frame) S.cb.frame(*device, dt);
        device->endFrame();
        if (capture.active() && ++frameNo >= capture.frames) {
            if (!capture.path.empty()) {
                std::vector<unsigned char> px(size_t(S.fbW) * size_t(S.fbH) * 4);
                glReadPixels(0, 0, S.fbW, S.fbH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
                detail::writePPM(capture.path, S.fbW, S.fbH, px.data());
            }
            S.quit = true;
        }
        SwapBuffers(g_hdc);
    }
    if (S.cb.shutdown) S.cb.shutdown();
    device.reset();
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(g_rc);
    ReleaseDC(g_hwnd, g_hdc);
    DestroyWindow(g_hwnd);
    return 0;
}

bool pointerLocked() { return g_pointerLocked; }
bool setPointerLock(bool enabled) {
    if (g_pointerLocked == enabled) return true;
    RAWINPUTDEVICE device{0x01, 0x02, enabled ? DWORD(0) : DWORD(RIDEV_REMOVE), enabled ? g_hwnd : nullptr};
    if (!RegisterRawInputDevices(&device, 1, sizeof(device))) return false;
    if (enabled) {
        RECT rect; GetClientRect(g_hwnd, &rect);
        POINT points[2]={{rect.left,rect.top},{rect.right,rect.bottom}};
        ClientToScreen(g_hwnd,&points[0]); ClientToScreen(g_hwnd,&points[1]);
        rect={points[0].x,points[0].y,points[1].x,points[1].y}; ClipCursor(&rect); ShowCursor(FALSE);
    } else { ClipCursor(nullptr); ShowCursor(TRUE); }
    g_pointerLocked=enabled; return true;
}
} // namespace aiverse::platform

int main(int argc, char** argv) { return aiverseMain(argc, argv); }
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return aiverseMain(__argc, __argv); }
