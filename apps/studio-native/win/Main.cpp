// AIVERSE Studio, Windows shell: Win32 window + WebView2 UI + native Vulkan Scene / Game views (child windows
// over the DOM viewport rects, like the macOS Metal views). Everything that is not Win32 lives in
// apps/studio-native/StudioHost (shared with mac/Main.mm). The web UI is the same build as on macOS: a script
// injected at document creation gives it the window.webkit.messageHandlers.aiverse bridge it already uses.
// Engine headers first: <windows.h> defines near / far / small / interface as macros.
#include "apps/studio-native/StudioHost.h"
#include "apps/studio-native/StudioGameInput.h"
#include "apps/editor/core/BuildTool.h"
#include "engine/gfx/vulkan/Vulkan.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include "WebView2.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef AIVERSE_STUDIO_SOURCE_DIR
#define AIVERSE_STUDIO_SOURCE_DIR "."
#endif

using aiverse::Json;
namespace studio = aiverse::studio;
namespace fs = std::filesystem;

namespace {
// ---------------------------------------------------------------- small helpers
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}
std::string narrow(const wchar_t* w, int len = -1) {
    if (!w) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, len, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, len, s.data(), n, nullptr, nullptr);
    if (len < 0 && !s.empty() && s.back() == '\0') s.pop_back();
    return s;
}
// Strings WebView2 hands out are CoTaskMemAlloc'ed.
std::string takeString(LPWSTR w) { std::string s = narrow(w); CoTaskMemFree(w); return s; }
fs::path exeDir() {
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buf, DWORD(std::size(buf)));
    return fs::path(std::wstring(buf, n)).parent_path();
}
fs::path knownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    fs::path out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) out = fs::path(p);
    CoTaskMemFree(p);
    return out;
}
fs::path studioDataDir() {
    fs::path dir = knownFolder(FOLDERID_LocalAppData);
    if (dir.empty()) dir = fs::temp_directory_path();
    dir /= L"AIVERSE";
    dir /= L"Studio";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}
void logLine(const std::string& line) {
    OutputDebugStringW(widen(line + "\n").c_str());
    std::fprintf(stderr, "%s\n", line.c_str());
    static std::mutex m;
    std::lock_guard<std::mutex> lock(m);
    std::ofstream(studioDataDir() / L"studio.log", std::ios::app) << line << "\n";
}

// Minimal owning COM pointer.
template <class T> struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() { reset(); }
    void reset(T* v = nullptr) { if (p) p->Release(); p = v; }
    T** put() { reset(); return &p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};
// A WebView2 completion / event handler from a lambda. Invoke's signature comes from the template arguments,
// so it overrides the interface's pure virtual (no WRL: builds with MSVC and MinGW alike).
template <class I, class... Args> class Callback final : public I {
public:
    Callback(const IID& iid, std::function<HRESULT(Args...)> fn) : iid_(iid), fn_(std::move(fn)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, iid_)) { *out = static_cast<I*>(this); AddRef(); return S_OK; }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { const ULONG r = --refs_; if (!r) delete this; return r; }
    HRESULT STDMETHODCALLTYPE Invoke(Args... args) override { return fn_ ? fn_(args...) : S_OK; }
private:
    std::atomic<ULONG> refs_{1};
    const IID& iid_;
    std::function<HRESULT(Args...)> fn_;
};
template <class I, class... Args, class F> I* makeCallback(const IID& iid, F&& fn) {
    return new Callback<I, Args...>(iid, std::function<HRESULT(Args...)>(std::forward<F>(fn)));
}
// Same key rules as the page (floating.ts): a window id and the panel ids it is born with.
bool floatKeyOk(const std::string& s) {
    if (s.empty() || s.size() > 40) return false;
    for (unsigned char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    return true;
}
bool floatPanelOk(const std::string& s) {
    if (s.empty() || s.size() > 32 || s[0] < 'a' || s[0] > 'z') return false;
    for (unsigned char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}
struct FloatPane {
    std::string key;
    HWND hwnd = nullptr;
    Com<ICoreWebView2Controller> controller;
    Com<ICoreWebView2> web;
    POINT press{};
    bool overMain = false;
    bool moved = false;
};

// WS_SYSMENU / WS_MINIMIZEBOX / WS_MAXIMIZEBOX make Windows paint a caption even when WS_CAPTION
// is left out. Keep those styles (snap, Alt+Space, the taskbar) and give the client the whole window.
void extendClientOverCaption(HWND hwnd) {
    const MARGINS shadow{0, 0, 0, 1};
    DwmExtendFrameIntoClientArea(hwnd, &shadow);
    const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

constexpr UINT kRunTask = WM_APP + 1; // lParam: std::function<void()>* to run on the UI thread
constexpr UINT_PTR kTickTimer = 1;
constexpr wchar_t kMainClass[] = L"AiverseStudioWindow";
constexpr wchar_t kFloatClass[] = L"AiverseStudioFloat";
constexpr wchar_t kSurfaceClass[] = L"AiverseStudioSurface";
constexpr wchar_t kVirtualHost[] = L"studio.aiverse";
constexpr char kTrustedOrigin[] = "https://studio.aiverse/";
constexpr char kDevOrigin[] = "http://127.0.0.1:4173/";
// The web UI talks to window.webkit.messageHandlers.aiverse (WKWebView); WebView2 has chrome.webview.
constexpr wchar_t kBridgeShim[] =
    L"(function(){if(!window.chrome||!window.chrome.webview)return;"
    L"var h={postMessage:function(m){window.chrome.webview.postMessage(m);}};"
    L"window.webkit={messageHandlers:{aiverse:h}};"
    L"document.documentElement&&document.documentElement.classList.add('studio-windows');})();";

class WinShell;
WinShell* gShell = nullptr;

class WinShell final : public studio::StudioHostPlatform {
public:
    WinShell(std::string project, bool dev) : project_(std::move(project)), dev_(dev) {}

    int run() {
        HINSTANCE instance = GetModuleHandleW(nullptr);
        if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {
            fatal("COM could not start");
            return 1;
        }
        WNDCLASSEXW wc{sizeof(wc)};
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = mainProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(RGB(14, 17, 23));
        // The resource holds 16–256 with alpha. LoadIcon keeps one 32px frame and drops that alpha.
        wc.hIcon = reinterpret_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, 256, 256, LR_DEFAULTCOLOR));
        wc.hIconSm = reinterpret_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
        wc.lpszClassName = kMainClass;
        RegisterClassExW(&wc);
        WNDCLASSEXW fc{sizeof(fc)};
        fc.style = CS_HREDRAW | CS_VREDRAW;
        fc.lpfnWndProc = floatProc;
        fc.hInstance = instance;
        fc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        fc.hbrBackground = wc.hbrBackground;
        fc.hIcon = wc.hIcon;
        fc.hIconSm = wc.hIconSm;
        fc.lpszClassName = kFloatClass;
        RegisterClassExW(&fc);
        WNDCLASSEXW sc{sizeof(sc)};
        sc.style = CS_OWNDC | CS_DBLCLKS;
        sc.lpfnWndProc = surfaceProc;
        sc.hInstance = instance;
        sc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        sc.lpszClassName = kSurfaceClass;
        RegisterClassExW(&sc);

        const UINT dpi = GetDpiForSystem();
        // The HTML title bar is the chrome. The client fills the frame; WM_NCCALCSIZE drops the system caption.
        constexpr DWORD kFrame = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
        const int width = MulDiv(1440, int(dpi), 96);
        const int height = MulDiv(900, int(dpi), 96);
        main_ = CreateWindowExW(0, kMainClass, L"AIVERSE", kFrame, CW_USEDEFAULT, CW_USEDEFAULT,
                                width, height, nullptr, nullptr, instance, nullptr);
        if (!main_) { fatal("The main window could not be created"); return 1; }
        extendClientOverCaption(main_);
        for (int i = 0; i < 2; ++i) {
            surfaces_[i] = CreateWindowExW(0, kSurfaceClass, nullptr, WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0, 16, 16,
                                           main_, nullptr, instance, nullptr);
            SetWindowLongPtrW(surfaces_[i], GWLP_USERDATA, LONG_PTR(i));
        }

        const std::string root = AIVERSE_STUDIO_SOURCE_DIR;
        core_ = std::make_unique<studio::StudioHost>(*this, root, false);
        const std::string exe = exeDir().string();
#ifdef AIVERSE_STUDIO_BUILD_TOOL
        core_->session().setBuildTool(AIVERSE_STUDIO_BUILD_TOOL);
#else
        core_->session().setBuildTool(aiverse::editor::findBuildTool(exe, root));
#endif
#ifdef AIVERSE_STUDIO_MCP_TOOL
        core_->session().setMcpTool(AIVERSE_STUDIO_MCP_TOOL);
#else
        core_->session().setMcpTool(exe + "\\aiversemcp.exe");
#endif
        if (project_.empty()) project_ = lastProject();
        else rememberLastProject(project_);
        core_->setPendingProject(project_);

        ShowWindow(main_, SW_SHOWDEFAULT);
        UpdateWindow(main_);
        if (!startWebView()) return 1;
        SetTimer(main_, kTickTimer, 15, nullptr); // ~60 Hz; WM_TIMER also runs inside modal move/resize loops

        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        CoUninitialize();
        return int(msg.wParam);
    }

    // ------------------------------------------------------------ StudioHostPlatform
    void evaluateScript(const std::string& js, bool currentOnly) override {
        const std::wstring wide = widen(js);
        auto run = [&](ICoreWebView2* web) { if (web) web->ExecuteScript(wide.c_str(), nullptr); };
        if (currentOnly) { run(currentWeb_ ? currentWeb_ : web_.p); return; }
        run(web_.p);
        for (const auto& pane : floats_) run(pane->web.p);
    }
    std::unique_ptr<aiverse::gfx::Device> createDevice(studio::StudioSurface surface) override {
        aiverse::gfx::VulkanWindow window;
        window.kind = aiverse::gfx::VulkanWindow::Kind::Win32;
        window.display = GetModuleHandleW(nullptr);
        window.window = uint64_t(uintptr_t(surfaces_[index(surface)]));
        std::string error;
        auto device = aiverse::gfx::createVulkanDevice(window, &error);
        if (!device) throw std::runtime_error("Vulkan is required for the Studio views: " + (error.empty() ? std::string("no device") : error));
        return device;
    }
    bool surfaceDrawable(studio::StudioSurface surface) override {
        HWND child = surfaces_[index(surface)];
        HWND host = GetParent(child);
        return IsWindowVisible(child) && host && IsWindowVisible(host) && !IsIconic(host);
    }
    bool gameFocused() override {
        HWND host = GetParent(surfaces_[1]);
        return host && GetForegroundWindow() == host && GetFocus() == surfaces_[1];
    }
    // The DOM rect is in CSS pixels (= DIPs, the web view is not zoomed); the child window takes physical pixels.
    bool placeSurface(studio::StudioSurface surface, const studio::ViewportRect& rect, studio::NativeViewportPlacement& out) override {
        HWND child = surfaces_[index(surface)];
        HWND host = windowForWeb(currentWeb_);
        if (!host || !IsWindow(host)) host = main_;
        // A hide from a window that no longer hosts the view is stale (the other window just showed it).
        if (GetParent(child) != host) {
            if (!rect.visible) return false;
            ShowWindow(child, SW_HIDE);
            SetParent(child, host);
        }
        RECT client{};
        GetClientRect(host, &client);
        const double scale = double(GetDpiForWindow(host)) / 96.0;
        const double hostW = double(client.right) / scale, hostH = double(client.bottom) / scale;
        out = studio::viewportPlacement(rect, hostW, hostH, scale);
        if (!out.visible) {
            if (IsWindowVisible(child)) ShowWindow(child, SW_HIDE);
            return true;
        }
        const int left = int(std::lround(out.x * scale));
        const int top = int(std::lround((hostH - out.y - out.height) * scale));
        RECT now{};
        GetWindowRect(child, &now);
        MapWindowPoints(HWND_DESKTOP, host, reinterpret_cast<POINT*>(&now), 2);
        const bool moved = now.left != left || now.top != top || now.right - now.left != out.pixelsW || now.bottom - now.top != out.pixelsH;
        // Above the WebView2 window (a sibling child): the native view covers its DOM rect.
        SetWindowPos(child, HWND_TOP, left, top, out.pixelsW, out.pixelsH,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW | (moved ? 0 : SWP_NOMOVE | SWP_NOSIZE));
        return true;
    }
    double now() override {
        static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
        LARGE_INTEGER t;
        QueryPerformanceCounter(&t);
        return double(t.QuadPart) / double(freq.QuadPart);
    }
    void runInBackground(std::function<void()> work, std::function<void()> then) override {
        HWND target = main_;
        std::thread([work = std::move(work), then = std::move(then), target]() mutable {
            work();
            postTask(target, std::move(then));
        }).detach();
    }
    bool confirmDiscardChanges(const std::string& title, const std::string& message, const std::string& discardLabel) override {
        const std::wstring text = widen(message + "\n\nChoose Yes to " + discardLabel + ".");
        return MessageBoxW(main_, text.c_str(), widen(title).c_str(), MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
    }
    // Native dialogs run after the current bridge message returns (no re-entrant web messages during them).
    void chooseProject(std::function<void(std::string)> done) override {
        later([this, done = std::move(done)] { done(pickFolder(L"Open Project", {})); });
    }
    void chooseFolder(const std::string& message, const std::string& startDir, std::function<void(std::string)> done) override {
        later([this, message, startDir, done = std::move(done)] { done(pickFolder(widen(message), startDir)); });
    }
    void chooseSaveFile(const std::string& startDir, const std::string& defaultName, std::function<void(std::string)> done) override {
        later([this, startDir, defaultName, done = std::move(done)] {
            std::string result;
            Com<IFileSaveDialog> dialog;
            if (SUCCEEDED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileSaveDialog, reinterpret_cast<void**>(dialog.put())))) {
                const COMDLG_FILTERSPEC filter[] = {{L"Map (*.json)", L"*.json"}};
                dialog->SetFileTypes(1, filter);
                dialog->SetDefaultExtension(L"json");
                dialog->SetFileName(widen(defaultName).c_str());
                setFolder(dialog.p, startDir);
                if (SUCCEEDED(dialog->Show(main_))) result = resultPath(dialog.p);
            }
            done(result);
        });
    }
    void chooseImportSources(const std::string& startDir, std::function<void(std::vector<fs::path>)> done) override {
        later([this, startDir, done = std::move(done)] {
            std::vector<fs::path> sources;
            Com<IFileOpenDialog> dialog;
            if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog, reinterpret_cast<void**>(dialog.put())))) {
                DWORD options = 0;
                dialog->GetOptions(&options);
                dialog->SetOptions(options | FOS_ALLOWMULTISELECT | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
                dialog->SetTitle(L"Import into content");
                setFolder(dialog.p, startDir);
                Com<IShellItemArray> items;
                if (SUCCEEDED(dialog->Show(main_)) && SUCCEEDED(dialog->GetResults(items.put()))) {
                    DWORD count = 0;
                    items->GetCount(&count);
                    for (DWORD i = 0; i < count && i < 65; ++i) {
                        Com<IShellItem> item;
                        PWSTR path = nullptr;
                        if (SUCCEEDED(items->GetItemAt(i, item.put())) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
                            sources.emplace_back(narrow(path));
                        CoTaskMemFree(path);
                    }
                }
            }
            done(std::move(sources));
        });
    }
    void revealInFileManager(const fs::path& path) override {
        const std::wstring args = L"/select,\"" + path.wstring() + L"\"";
        ShellExecuteW(main_, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    }
    fs::path cacheDir() override {
        fs::path dir = knownFolder(FOLDERID_LocalAppData);
        return dir.empty() ? fs::temp_directory_path() : dir;
    }
    void rememberLastProject(const std::string& dir) override {
        HKEY key = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\AIVERSE\\Studio", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return;
        const std::wstring value = widen(dir);
        RegSetValueExW(key, L"LastProject", 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), DWORD((value.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
    }
    Json windowRequest(const std::string& method, const Json& params) override {
        Json result = Json::object();
        // A caption-less frame ignores WM_NCLBUTTONDOWN HTCAPTION. SC_MOVE / SC_SIZE still run the system loop while the button is down.
        const bool buttonDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        if (method == "studio.windowDrag") {
            if (!buttonDown) return result;
            ReleaseCapture();
            SendMessageW(main_, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
            return result;
        }
        if (method == "studio.windowResize") {
            // SC_SIZE runs a modal loop inside this web callback, so the page cannot move the
            // Scene child until the button comes up. Follow the cursor from the UI timer instead.
            if (!buttonDown || IsZoomed(main_) || windowForWeb(currentWeb_) != main_) return result;
            int edge = 0;
            const std::string which = params.getStr("edge");
            if (which.find("left") != std::string::npos) edge |= 1;
            if (which.find("right") != std::string::npos) edge |= 2;
            if (which.find("top") != std::string::npos) edge |= 4;
            if (which.find("bottom") != std::string::npos) edge |= 8;
            if (!edge) return result;
            size_.on = true;
            size_.edge = edge;
            GetCursorPos(&size_.origin);
            GetWindowRect(main_, &size_.start);
            return result;
        }
        HWND target = windowForWeb(currentWeb_);
        if (method == "studio.windowZoom") { ShowWindow(target, IsZoomed(target) ? SW_RESTORE : SW_MAXIMIZE); return result; }
        if (method == "studio.windowMinimize") { ShowWindow(target, SW_MINIMIZE); return result; }
        if (method == "studio.windowClose") {
            const std::string key = params.getStr("window");
            if (!key.empty()) {
                FloatPane* pane = findFloat(key);
                result.set("closed", Json::fromBool(pane && pane->hwnd));
                if (pane && pane->hwnd) PostMessageW(pane->hwnd, WM_CLOSE, 0, 0);
                return result;
            }
            PostMessageW(main_, WM_CLOSE, 0, 0);
            result.set("closed", Json::fromBool(true));
            return result;
        }
        if (method == "studio.windowOpen") return openFloating(params);
        if (method == "studio.windowTitle") {
            if (target == main_ || !IsWindow(target)) throw std::runtime_error("Invalid window title");
            const std::string title = params.getStr("title");
            if (title.size() > 64) throw std::runtime_error("Invalid window title");
            SetWindowTextW(target, title.empty() ? L"AIVERSE" : widen(title).c_str());
            result.set("ok", Json::fromBool(true));
            return result;
        }
        throw std::runtime_error("Unknown window request");
    }
    bool hasFloatingWindows() override { return !floats_.empty(); }
    void log(const std::string& message) override { logLine(message); }

    // ------------------------------------------------------------ window procedures
    static LRESULT CALLBACK mainProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        WinShell* s = gShell;
        if (!s) return DefWindowProcW(h, msg, wp, lp);
        switch (msg) {
        case WM_TIMER:
            if (wp == kTickTimer) {
                if (s->size_.on) s->stepSize();
                if (s->core_) s->core_->tick();
            }
            return 0;
        case kRunTask: {
            std::unique_ptr<std::function<void()>> task(reinterpret_cast<std::function<void()>*>(lp));
            if (task && *task) (*task)();
            return 0;
        }
        case WM_SIZE:
            s->resizeWeb();
            return 0;
        case WM_NCCALCSIZE:
            if (wp) {
                // The proposed window rect becomes the client, so Windows does not reserve a caption.
                if (IsZoomed(h)) {
                    HMONITOR mon = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
                    MONITORINFO info{sizeof(info)};
                    if (GetMonitorInfoW(mon, &info)) reinterpret_cast<NCCALCSIZE_PARAMS*>(lp)->rgrc[0] = info.rcWork;
                }
                return 0;
            }
            break;
        case WM_NCPAINT:
            return 0;
        case WM_NCACTIVATE:
            // -1 keeps the active state without asking Windows to repaint a caption.
            return DefWindowProcW(h, msg, wp, -1);
        case WM_NCHITTEST: {
            if (IsZoomed(h)) return HTCLIENT;
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            RECT rc{};
            GetWindowRect(h, &rc);
            const UINT frameDpi = GetDpiForWindow(h);
            const int border = GetSystemMetricsForDpi(SM_CXFRAME, frameDpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, frameDpi);
            const bool left = pt.x < rc.left + border;
            const bool right = pt.x >= rc.right - border;
            const bool top = pt.y < rc.top + border;
            const bool bottom = pt.y >= rc.bottom - border;
            if (top && left) return HTTOPLEFT;
            if (top && right) return HTTOPRIGHT;
            if (bottom && left) return HTBOTTOMLEFT;
            if (bottom && right) return HTBOTTOMRIGHT;
            if (left) return HTLEFT;
            if (right) return HTRIGHT;
            if (top) return HTTOP;
            if (bottom) return HTBOTTOM;
            return HTCLIENT;
        }
        case WM_GETMINMAXINFO: {
            // Without a caption the maximized window would cover the taskbar.
            HMONITOR mon = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
            MONITORINFO info{sizeof(info)};
            if (GetMonitorInfoW(mon, &info)) {
                auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
                mmi->ptMaxPosition.x = info.rcWork.left - info.rcMonitor.left;
                mmi->ptMaxPosition.y = info.rcWork.top - info.rcMonitor.top;
                mmi->ptMaxSize.x = info.rcWork.right - info.rcWork.left;
                mmi->ptMaxSize.y = info.rcWork.bottom - info.rcWork.top;
            }
            return 0;
        }
        case WM_DPICHANGED: {
            const RECT* r = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_MOVE:
        case WM_MOVING:
            if (s->controller_) s->controller_->NotifyParentWindowPositionChanged();
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE && s->core_) s->core_->gameSuspend();
            break;
        case WM_CLOSE:
            if (s->core_ && !s->core_->confirmClose()) return 0;
            s->closeAllFloats(); // reparent Scene/Game back before the owned windows are destroyed
            DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            KillTimer(h, kTickTimer);
            // Hosted game and Scene release their GPU devices before the child windows go away.
            if (s->core_) s->core_->shutdown();
            if (s->controller_) s->controller_->Close();
            s->web_.reset();
            s->controller_.reset(); // COM objects go before CoUninitialize
            PostQuitMessage(0);
            return 0;
        default: break;
        }
        return DefWindowProcW(h, msg, wp, lp);
    }
    static LRESULT CALLBACK surfaceProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        WinShell* s = gShell;
        if (s && s->core_) {
            const int which = int(GetWindowLongPtrW(h, GWLP_USERDATA));
            const LRESULT handled = which == 0 ? s->sceneInput(h, msg, wp, lp) : s->gameInput(h, msg, wp, lp);
            if (handled != LRESULT(-1)) return handled;
        }
        switch (msg) {
        case WM_ERASEBKGND: return 1; // the GPU paints every pixel
        case WM_PAINT: ValidateRect(h, nullptr); return 0;
        case WM_MOUSEACTIVATE: return MA_ACTIVATE;
        default: return DefWindowProcW(h, msg, wp, lp);
        }
    }
    // A dock tab torn out of the editor. Real caption (dark) so it moves, resizes and closes on its own.
    // Scene and Game stay the same child windows: placeSurface reparents them into whichever window shows the panel.
    static LRESULT CALLBACK floatProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_NCCREATE) {
            auto* created = reinterpret_cast<CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(created->lpCreateParams));
            return DefWindowProcW(h, msg, wp, lp);
        }
        WinShell* s = gShell;
        auto* pane = reinterpret_cast<FloatPane*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (!s || !pane) return DefWindowProcW(h, msg, wp, lp);
        switch (msg) {
        case WM_SIZE:
            if (pane->controller && wp != SIZE_MINIMIZED) {
                RECT bounds{};
                GetClientRect(h, &bounds);
                if (bounds.right > 0 && bounds.bottom > 0) {
                    pane->controller->put_Bounds(bounds);
                    if (pane->web) pane->web->ExecuteScript(L"window.dispatchEvent(new Event('resize'))", nullptr);
                }
            }
            return 0;
        case WM_MOVE:
            if (pane->controller) pane->controller->NotifyParentWindowPositionChanged();
            return 0;
        case WM_ENTERSIZEMOVE:
            GetCursorPos(&pane->press);
            pane->overMain = false;
            pane->moved = false;
            return 0;
        case WM_MOVING: {
            POINT cursor{};
            GetCursorPos(&cursor);
            const int dx = cursor.x - pane->press.x, dy = cursor.y - pane->press.y;
            if (dx > 12 || dx < -12 || dy > 12 || dy < -12) pane->moved = true;
            RECT main{};
            GetWindowRect(s->main_, &main);
            const bool over = pane->moved && PtInRect(&main, cursor);
            if (over != pane->overMain && s->core_ && !s->closingFloats_) {
                pane->overMain = over;
                Json payload = Json::object();
                payload.set("over", Json::fromBool(over));
                s->core_->sendEventPayload("window.dockHover", payload);
            }
            return DefWindowProcW(h, msg, wp, lp);
        }
        case WM_EXITSIZEMOVE:
            s->sendFloatFrame(pane);
            if (pane->overMain && !s->closingFloats_) {
                pane->overMain = false;
                if (s->core_) {
                    Json payload = Json::object();
                    payload.set("over", Json::fromBool(false));
                    s->core_->sendEventPayload("window.dockHover", payload);
                }
                PostMessageW(h, WM_CLOSE, 0, 0); // release docks every tab back into the main window
            }
            return 0;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lp);
            const UINT dpi = GetDpiForWindow(h);
            info->ptMinTrackSize.x = MulDiv(280, int(dpi), 96);
            info->ptMinTrackSize.y = MulDiv(180, int(dpi), 96);
            return 0;
        }
        case WM_DPICHANGED: {
            const RECT* suggested = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(h, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_CLOSE:
            s->releaseSurfaces(h); // the GPU child must not die with this window
            DestroyWindow(h);
            return 0;
        case WM_DESTROY: {
            const std::string key = pane->key;
            const bool tell = !s->closingFloats_;
            if (pane->controller) pane->controller->Close();
            pane->web.reset();
            pane->controller.reset();
            s->eraseFloat(h);
            if (tell && s->core_) s->core_->sendEvent("window.floatClosed", key);
            return 0;
        }
        default: break;
        }
        return DefWindowProcW(h, msg, wp, lp);
    }

private:
    static int index(studio::StudioSurface s) { return s == studio::StudioSurface::Scene ? 0 : 1; }
    static void postTask(HWND target, std::function<void()> fn) {
        auto* task = new std::function<void()>(std::move(fn));
        if (!target || !PostMessageW(target, kRunTask, 0, LPARAM(task))) delete task;
    }
    void later(std::function<void()> fn) { postTask(main_, std::move(fn)); }
    void fatal(const std::string& message) {
        logLine("AIVERSE Studio: " + message);
        MessageBoxW(main_, widen(message).c_str(), L"AIVERSE Studio", MB_OK | MB_ICONERROR);
    }
    double scale() const { return double(GetDpiForWindow(main_)) / 96.0; }

    std::string lastProject() {
        wchar_t buf[4096];
        DWORD size = sizeof(buf);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\AIVERSE\\Studio", L"LastProject", RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS)
            return {};
        std::error_code ec;
        return fs::is_directory(fs::path(buf), ec) ? narrow(buf) : std::string();
    }
    void setFolder(IFileDialog* dialog, const std::string& dir) {
        if (dir.empty()) return;
        Com<IShellItem> folder;
        if (SUCCEEDED(SHCreateItemFromParsingName(widen(dir).c_str(), nullptr, IID_IShellItem, reinterpret_cast<void**>(folder.put()))))
            dialog->SetFolder(folder.p);
    }
    std::string resultPath(IFileDialog* dialog) {
        Com<IShellItem> item;
        PWSTR path = nullptr;
        std::string out;
        if (SUCCEEDED(dialog->GetResult(item.put())) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) out = narrow(path);
        CoTaskMemFree(path);
        return out;
    }
    std::string pickFolder(const std::wstring& title, const std::string& startDir) {
        Com<IFileOpenDialog> dialog;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog, reinterpret_cast<void**>(dialog.put()))))
            return {};
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        dialog->SetTitle(title.c_str());
        setFolder(dialog.p, startDir);
        if (FAILED(dialog->Show(main_))) return {};
        return resultPath(dialog.p);
    }

    // ------------------------------------------------------------ WebView2
    fs::path webRoot() const {
        const fs::path bundled = exeDir() / L"studio-web";
        std::error_code ec;
        if (fs::is_regular_file(bundled / L"index.html", ec)) return bundled;
        return fs::path(AIVERSE_STUDIO_SOURCE_DIR) / "apps" / "studio-web" / "dist";
    }
    bool startWebView() {
        using CreateEnv = HRESULT(STDAPICALLTYPE*)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*,
                                                   ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
        HMODULE loader = LoadLibraryW((exeDir() / L"WebView2Loader.dll").c_str());
        if (!loader) loader = LoadLibraryW(L"WebView2Loader.dll");
        auto create = loader ? reinterpret_cast<CreateEnv>(reinterpret_cast<void*>(GetProcAddress(loader, "CreateCoreWebView2EnvironmentWithOptions"))) : nullptr;
        if (!create) { fatal("WebView2Loader.dll is missing next to AiverseStudio.exe"); return false; }
        const std::wstring userData = (studioDataDir() / L"WebView2").wstring();
        auto* envDone = makeCallback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment*>(
            IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, [this](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(hr) || !env) {
                    fatal("Microsoft Edge WebView2 Runtime is not installed.\nInstall it from https://go.microsoft.com/fwlink/p/?LinkId=2124703");
                    PostQuitMessage(1);
                    return S_OK;
                }
                env->AddRef();
                env_.reset(env);
                auto* controllerDone = makeCallback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>(
                    IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, [this](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                        if (FAILED(result) || !controller) { fatal("The WebView2 control could not be created"); PostQuitMessage(1); return S_OK; }
                        onController(controller);
                        return S_OK;
                    });
                env->CreateCoreWebView2Controller(main_, controllerDone);
                controllerDone->Release();
                return S_OK;
            });
        const HRESULT hr = create(nullptr, userData.c_str(), nullptr, envDone);
        envDone->Release();
        if (FAILED(hr)) {
            fatal("Microsoft Edge WebView2 Runtime is not available.\nInstall it from https://go.microsoft.com/fwlink/p/?LinkId=2124703");
            return false;
        }
        return true;
    }
    void onController(ICoreWebView2Controller* controller) {
        controller->AddRef();
        controller_.reset(controller);
        controller_->get_CoreWebView2(web_.put());
        {
            Com<ICoreWebView2Controller2> c2;
            if (SUCCEEDED(controller_->QueryInterface(IID_ICoreWebView2Controller2, reinterpret_cast<void**>(c2.put())))) {
                COREWEBVIEW2_COLOR dark{255, 14, 17, 23};
                c2->put_DefaultBackgroundColor(dark);
            }
            Com<ICoreWebView2Controller4> c4;
            // Files dropped from Explorer would navigate the UI away; Assets import goes through the dialog.
            if (SUCCEEDED(controller_->QueryInterface(IID_ICoreWebView2Controller4, reinterpret_cast<void**>(c4.put())))) c4->put_AllowExternalDrop(FALSE);
        }
        Com<ICoreWebView2Settings> settings;
        if (SUCCEEDED(web_->get_Settings(settings.put()))) {
            settings->put_AreDevToolsEnabled(dev_ ? TRUE : FALSE);
            settings->put_AreDefaultContextMenusEnabled(dev_ ? TRUE : FALSE);
            settings->put_IsStatusBarEnabled(FALSE);
            settings->put_IsZoomControlEnabled(FALSE);
            settings->put_AreHostObjectsAllowed(FALSE);
        }
        web_->AddScriptToExecuteOnDocumentCreated(kBridgeShim, nullptr);
        // The built UI is served from its folder under a private host name (ES modules need a real origin).
        Com<ICoreWebView2_3> web3;
        const fs::path root = webRoot();
        if (!dev_) {
            std::error_code ec;
            if (!fs::is_regular_file(root / L"index.html", ec)) { fatal("The Studio UI is missing: " + root.string() + "\\index.html"); }
            if (FAILED(web_->QueryInterface(IID_ICoreWebView2_3, reinterpret_cast<void**>(web3.put())))) {
                fatal("This WebView2 Runtime is too old; update Microsoft Edge WebView2");
                PostQuitMessage(1);
                return;
            }
            web3->SetVirtualHostNameToFolderMapping(kVirtualHost, root.wstring().c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
        }
        EventRegistrationToken token{};
        web_->add_WebMessageReceived(makeCallback<ICoreWebView2WebMessageReceivedEventHandler, ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs*>(
            IID_ICoreWebView2WebMessageReceivedEventHandler, [this](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                takeMessage(sender, args);
                return S_OK;
            }), &token);
        web_->add_NavigationStarting(makeCallback<ICoreWebView2NavigationStartingEventHandler, ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*>(
            IID_ICoreWebView2NavigationStartingEventHandler, [this](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                LPWSTR uri = nullptr;
                if (SUCCEEDED(args->get_Uri(&uri)) && !trusted(takeString(uri))) args->put_Cancel(TRUE);
                return S_OK;
            }), &token);
        web_->add_NewWindowRequested(makeCallback<ICoreWebView2NewWindowRequestedEventHandler, ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs*>(
            IID_ICoreWebView2NewWindowRequestedEventHandler, [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                args->put_Handled(TRUE); // no pop-up windows from the UI
                return S_OK;
            }), &token);
        web_->add_NavigationCompleted(makeCallback<ICoreWebView2NavigationCompletedEventHandler, ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*>(
            IID_ICoreWebView2NavigationCompletedEventHandler, [this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                BOOL ok = FALSE;
                args->get_IsSuccess(&ok);
                logLine(ok ? "[studio-web] navigation finished" : "[studio-web] navigation failed");
                if (ok && core_) core_->webReady();
                return S_OK;
            }), &token);
        web_->add_ProcessFailed(makeCallback<ICoreWebView2ProcessFailedEventHandler, ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*>(
            IID_ICoreWebView2ProcessFailedEventHandler, [](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*) -> HRESULT {
                logLine("[studio-web] WebView2 process failed");
                return S_OK;
            }), &token);
        resizeWeb();
        controller_->put_IsVisible(TRUE);
        const std::wstring url = dev_ ? widen(kDevOrigin) : std::wstring(L"https://") + kVirtualHost + L"/index.html";
        logLine("[studio-web] loading " + narrow(url.c_str()));
        web_->Navigate(url.c_str());
    }
    bool trusted(const std::string& uri) const {
        // Only the bundled UI (or the dev server with --dev) may talk to the host or be navigated to.
        auto starts = [&](const char* prefix) { return uri.rfind(prefix, 0) == 0; };
        if (starts(kTrustedOrigin)) return true;
        return dev_ && starts(kDevOrigin);
    }
    void resizeWeb() {
        if (!controller_) return;
        RECT r{};
        GetClientRect(main_, &r);
        controller_->put_Bounds(r);
    }
    // The page measures the Scene on its own frames. A system size loop started from the web
    // callback never reaches those frames until the button is released, so the timer moves the
    // frame itself and asks the page to measure while the button is still down.
    void stepSize() {
        if (!size_.on || size_.busy || !IsWindow(main_)) return;
        if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0) { size_.on = false; return; }
        POINT now{};
        GetCursorPos(&now);
        RECT next = size_.start;
        const int dx = now.x - size_.origin.x, dy = now.y - size_.origin.y;
        if (size_.edge & 1) next.left += dx;
        if (size_.edge & 2) next.right += dx;
        if (size_.edge & 4) next.top += dy;
        if (size_.edge & 8) next.bottom += dy;
        const UINT dpi = GetDpiForWindow(main_);
        const int minW = MulDiv(480, int(dpi), 96), minH = MulDiv(320, int(dpi), 96);
        if (next.right - next.left < minW) {
            if (size_.edge & 1) next.left = next.right - minW;
            else next.right = next.left + minW;
        }
        if (next.bottom - next.top < minH) {
            if (size_.edge & 4) next.top = next.bottom - minH;
            else next.bottom = next.top + minH;
        }
        RECT cur{};
        GetWindowRect(main_, &cur);
        if (cur.left == next.left && cur.top == next.top && cur.right == next.right && cur.bottom == next.bottom) return;
        size_.busy = true;
        SetWindowPos(main_, nullptr, next.left, next.top, next.right - next.left, next.bottom - next.top, SWP_NOZORDER | SWP_NOACTIVATE);
        size_.busy = false;
        if (web_) web_->ExecuteScript(L"window.dispatchEvent(new Event('resize'))", nullptr);
    }
    void takeMessage(ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) {
        LPWSTR source = nullptr, json = nullptr;
        if (FAILED(args->get_Source(&source))) return;
        const std::string origin = takeString(source);
        if (!trusted(origin)) return;
        if (FAILED(args->get_WebMessageAsJson(&json))) return;
        const std::string body = takeString(json);
        if (!core_ || body.size() > 1024 * 1024) return;
        currentWeb_ = sender;
        core_->handleMessage(body);
        currentWeb_ = nullptr;
    }
    HWND windowForWeb(ICoreWebView2* web) const {
        if (web && web != web_.p)
            for (const auto& pane : floats_)
                if (pane->web.p == web && pane->hwnd) return pane->hwnd;
        return main_;
    }
    FloatPane* findFloat(const std::string& key) {
        for (auto& pane : floats_) if (pane->key == key) return pane.get();
        return nullptr;
    }
    void eraseFloat(HWND hwnd) {
        for (auto it = floats_.begin(); it != floats_.end(); ++it)
            if ((*it)->hwnd == hwnd) { floats_.erase(it); return; }
    }
    void releaseSurfaces(HWND host) {
        for (int i = 0; i < 2; ++i) {
            HWND child = surfaces_[i];
            if (!child || GetParent(child) != host) continue;
            ShowWindow(child, SW_HIDE);
            SetParent(child, main_);
            if (core_) core_->surfaceDetached(i == 0 ? studio::StudioSurface::Scene : studio::StudioSurface::Game);
        }
    }
    void closeAllFloats() {
        closingFloats_ = true;
        std::vector<HWND> hwnds;
        for (const auto& pane : floats_) if (pane->hwnd) hwnds.push_back(pane->hwnd);
        for (HWND hwnd : hwnds) if (IsWindow(hwnd)) SendMessageW(hwnd, WM_CLOSE, 0, 0);
    }
    void sendFloatFrame(FloatPane* pane) {
        if (!pane || !pane->hwnd || closingFloats_ || !core_) return;
        RECT frame{};
        GetWindowRect(pane->hwnd, &frame);
        Json payload = Json::object();
        payload.set("window", Json::string(pane->key));
        payload.set("x", Json::number(double(frame.left), 1));
        payload.set("y", Json::number(double(frame.top), 1));
        payload.set("width", Json::number(double(frame.right - frame.left), 1));
        payload.set("height", Json::number(double(frame.bottom - frame.top), 1));
        core_->sendEventPayload("window.floatFrame", payload);
    }
    bool cursorOverStudio() const {
        POINT pt{};
        if (!GetCursorPos(&pt)) return false;
        HWND hit = WindowFromPoint(pt);
        while (hit) {
            if (hit == main_) return true;
            for (const auto& pane : floats_) if (pane->hwnd == hit) return true;
            HWND next = GetParent(hit);
            if (!next || next == hit) break;
            hit = next;
        }
        return false;
    }
    static void applyFloatChrome(HWND hwnd) {
        const BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
        const COLORREF ink = RGB(14, 17, 23);
        const COLORREF text = RGB(232, 228, 220);
        DwmSetWindowAttribute(hwnd, 35 /* DWMWA_CAPTION_COLOR */, &ink, sizeof(ink));
        DwmSetWindowAttribute(hwnd, 36 /* DWMWA_TEXT_COLOR */, &text, sizeof(text));
        DwmSetWindowAttribute(hwnd, 34 /* DWMWA_BORDER_COLOR */, &ink, sizeof(ink));
    }
    void attachFloat(FloatPane* pane, ICoreWebView2Controller* controller, const std::wstring& url) {
        controller->AddRef();
        pane->controller.reset(controller);
        pane->controller->get_CoreWebView2(pane->web.put());
        Com<ICoreWebView2Controller2> c2;
        if (SUCCEEDED(pane->controller->QueryInterface(IID_ICoreWebView2Controller2, reinterpret_cast<void**>(c2.put())))) {
            COREWEBVIEW2_COLOR dark{255, 14, 17, 23};
            c2->put_DefaultBackgroundColor(dark);
        }
        Com<ICoreWebView2Controller4> c4;
        if (SUCCEEDED(pane->controller->QueryInterface(IID_ICoreWebView2Controller4, reinterpret_cast<void**>(c4.put()))))
            c4->put_AllowExternalDrop(FALSE);
        Com<ICoreWebView2Settings> settings;
        if (pane->web && SUCCEEDED(pane->web->get_Settings(settings.put()))) {
            settings->put_AreDevToolsEnabled(dev_ ? TRUE : FALSE);
            settings->put_AreDefaultContextMenusEnabled(dev_ ? TRUE : FALSE);
            settings->put_IsStatusBarEnabled(FALSE);
            settings->put_IsZoomControlEnabled(FALSE);
            settings->put_AreHostObjectsAllowed(FALSE);
        }
        if (!pane->web) return;
        pane->web->AddScriptToExecuteOnDocumentCreated(kBridgeShim, nullptr);
        if (!dev_) {
            Com<ICoreWebView2_3> web3;
            if (FAILED(pane->web->QueryInterface(IID_ICoreWebView2_3, reinterpret_cast<void**>(web3.put()))) ||
                FAILED(web3->SetVirtualHostNameToFolderMapping(kVirtualHost, webRoot().wstring().c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS))) {
                logLine("[studio-web] floating window could not map its UI");
                PostMessageW(pane->hwnd, WM_CLOSE, 0, 0);
                return;
            }
        }
        EventRegistrationToken token{};
        const std::string key = pane->key;
        pane->web->add_WebMessageReceived(makeCallback<ICoreWebView2WebMessageReceivedEventHandler, ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs*>(
            IID_ICoreWebView2WebMessageReceivedEventHandler, [this](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                takeMessage(sender, args);
                return S_OK;
            }), &token);
        pane->web->add_NavigationStarting(makeCallback<ICoreWebView2NavigationStartingEventHandler, ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*>(
            IID_ICoreWebView2NavigationStartingEventHandler, [this](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                LPWSTR uri = nullptr;
                if (SUCCEEDED(args->get_Uri(&uri)) && !trusted(takeString(uri))) args->put_Cancel(TRUE);
                return S_OK;
            }), &token);
        pane->web->add_NewWindowRequested(makeCallback<ICoreWebView2NewWindowRequestedEventHandler, ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs*>(
            IID_ICoreWebView2NewWindowRequestedEventHandler, [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                args->put_Handled(TRUE);
                return S_OK;
            }), &token);
        pane->web->add_NavigationCompleted(makeCallback<ICoreWebView2NavigationCompletedEventHandler, ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*>(
            IID_ICoreWebView2NavigationCompletedEventHandler, [this, key](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                BOOL ok = FALSE;
                args->get_IsSuccess(&ok);
                logLine(std::string("[studio-web] float ") + key + (ok ? " ready" : " failed to load"));
                return S_OK;
            }), &token);
        RECT bounds{};
        GetClientRect(pane->hwnd, &bounds);
        pane->controller->put_Bounds(bounds);
        pane->controller->put_IsVisible(TRUE);
        pane->web->Navigate(url.c_str());
    }
    Json openFloating(const Json& params) {
        Json result = Json::object();
        const std::string key = params.getStr("window");
        if (!floatKeyOk(key)) throw std::runtime_error("Invalid floating window");
        if (FloatPane* existing = findFloat(key)) {
            if (existing->hwnd && IsWindow(existing->hwnd)) {
                ShowWindow(existing->hwnd, SW_SHOW);
                SetForegroundWindow(existing->hwnd);
                result.set("opened", Json::fromBool(true));
                return result;
            }
        }
        if (!env_ || floats_.size() >= 8) { result.set("opened", Json::fromBool(false)); return result; }
        if (params.getBool("ifOutside") && cursorOverStudio()) { result.set("opened", Json::fromBool(false)); return result; }
        std::string panels;
        const auto& list = params["panels"].items();
        for (size_t i = 0; i < list.size() && i < 16; ++i) {
            const std::string panel = list[i].str();
            if (!floatPanelOk(panel)) throw std::runtime_error("Invalid floating panel");
            if (!panels.empty()) panels += ',';
            panels += panel;
        }
        if (panels.empty()) throw std::runtime_error("A floating window needs a panel");
        const std::string titleUtf = params.getStr("title");
        if (titleUtf.size() > 64) throw std::runtime_error("Invalid window title");
        const std::wstring title = titleUtf.empty() ? std::wstring(L"AIVERSE") : widen(titleUtf);
        POINT cursor{};
        GetCursorPos(&cursor);
        const UINT dpi = GetDpiForWindow(main_);
        const bool big = panels.find("scene") != std::string::npos || panels.find("game") != std::string::npos;
        int x = cursor.x - MulDiv(40, int(dpi), 96);
        int y = cursor.y - MulDiv(16, int(dpi), 96);
        int w = MulDiv(big ? 960 : 440, int(dpi), 96);
        int h = MulDiv(big ? 640 : 560, int(dpi), 96);
        if (params["x"].isNumber() && params["y"].isNumber() && params["width"].isNumber() && params["height"].isNumber()) {
            const int aw = std::clamp(int(params["width"].toDouble()), 220, 8000);
            const int ah = std::clamp(int(params["height"].toDouble()), 160, 6000);
            RECT asked{LONG(params["x"].toDouble()), LONG(params["y"].toDouble()), 0, 0};
            asked.right = asked.left + aw;
            asked.bottom = asked.top + ah;
            if (MonitorFromRect(&asked, MONITOR_DEFAULTTONULL)) { x = asked.left; y = asked.top; w = aw; h = ah; }
        } else {
            MONITORINFO work{sizeof(work)};
            if (GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &work)) {
                if (x < work.rcWork.left) x = work.rcWork.left;
                if (y < work.rcWork.top) y = work.rcWork.top;
                if (x + w > work.rcWork.right) x = std::max(int(work.rcWork.left), int(work.rcWork.right) - w);
                if (y + h > work.rcWork.bottom) y = std::max(int(work.rcWork.top), int(work.rcWork.bottom) - h);
            }
        }
        auto pane = std::make_unique<FloatPane>();
        pane->key = key;
        FloatPane* raw = pane.get();
        raw->hwnd = CreateWindowExW(WS_EX_APPWINDOW, kFloatClass, title.c_str(), WS_OVERLAPPEDWINDOW,
                                     x, y, w, h, main_, nullptr, GetModuleHandleW(nullptr), raw);
        if (!raw->hwnd) { result.set("opened", Json::fromBool(false)); return result; }
        applyFloatChrome(raw->hwnd);
        floats_.push_back(std::move(pane));
        ShowWindow(raw->hwnd, SW_SHOW);
        SetForegroundWindow(raw->hwnd);
        const std::wstring url = dev_
            ? widen(std::string(kDevOrigin) + "?window=" + key + "&panels=" + panels)
            : std::wstring(L"https://") + kVirtualHost + L"/index.html?window=" + widen(key) + L"&panels=" + widen(panels);
        auto* done = makeCallback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>(
            IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,
            [this, key, url](HRESULT hr, ICoreWebView2Controller* controller) -> HRESULT {
                FloatPane* live = findFloat(key);
                if (!live || !IsWindow(live->hwnd) || FAILED(hr) || !controller) {
                    if (controller) controller->Close();
                    if (live && IsWindow(live->hwnd)) PostMessageW(live->hwnd, WM_CLOSE, 0, 0);
                    else logLine("[studio-web] floating window could not be created");
                    return S_OK;
                }
                attachFloat(live, controller, url);
                return S_OK;
            });
        env_->CreateCoreWebView2Controller(raw->hwnd, done);
        done->Release();
        result.set("opened", Json::fromBool(true));
        return result;
    }

    // ------------------------------------------------------------ input
    studio::StudioSceneMods mods() const {
        studio::StudioSceneMods m;
        m.shift = GetKeyState(VK_SHIFT) < 0;
        m.ctrl = GetKeyState(VK_CONTROL) < 0;
        m.alt = GetKeyState(VK_MENU) < 0;
        m.command = m.ctrl; // Windows: Ctrl plays the role of Cmd (toggle select, duplicate, pan with Alt)
        return m;
    }
    void viewSize(HWND h, double& w, double& hgt) const {
        RECT r{};
        GetClientRect(h, &r);
        const double k = scale();
        w = double(r.right) / k;
        hgt = double(r.bottom) / k;
    }
    // Returns -1 when the message is not the Scene's.
    LRESULT sceneInput(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        const double k = scale();
        double w = 0, hh = 0;
        viewSize(h, w, hh);
        auto at = [&](double& x, double& y) { x = GET_X_LPARAM(lp) / k; y = GET_Y_LPARAM(lp) / k; };
        int button = -1, phase = -1;
        switch (msg) {
        case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: button = 0; phase = 0; break;
        case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: button = 1; phase = 0; break;
        case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: button = 2; phase = 0; break;
        case WM_LBUTTONUP: button = 0; phase = 2; break;
        case WM_RBUTTONUP: button = 1; phase = 2; break;
        case WM_MBUTTONUP: button = 2; phase = 2; break;
        case WM_MOUSEMOVE: button = captured_ >= 0 ? captured_ : 0; phase = 1; break;
        default: break;
        }
        if (phase >= 0) {
            double x = 0, y = 0;
            at(x, y);
            if (phase == 0) {
                SetFocus(h);
                SetCapture(h);
                captured_ = button;
                last_ = {x, y};
            } else if (phase == 2) {
                if (button != captured_) return 0;
                captured_ = -1;
                ReleaseCapture();
            } else if (!tracking_) {
                TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0};
                tracking_ = TrackMouseEvent(&t) != FALSE;
            }
            const double dx = x - last_.first, dy = y - last_.second;
            last_ = {x, y};
            core_->sceneMouse(phase, button, x, y, dx, dy, w, hh, mods());
            return 0;
        }
        switch (msg) {
        case WM_MOUSELEAVE:
            tracking_ = false;
            if (captured_ < 0) core_->hover(-1, -1, w, hh);
            return 0;
        case WM_CAPTURECHANGED:
            if (captured_ >= 0 && HWND(lp) != h) { // capture taken away mid-drag: finish it
                const int b = captured_;
                captured_ = -1;
                core_->sceneMouse(2, b, last_.first, last_.second, 0, 0, w, hh, mods());
            }
            return 0;
        case WM_MOUSEWHEEL:
            core_->sceneWheel(double(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA);
            return 0;
        case WM_KEYDOWN: case WM_SYSKEYDOWN: case WM_KEYUP: case WM_SYSKEYUP: {
            using K = studio::StudioSceneKey;
            K key = K::Other;
            switch (wp) {
            case 'A': key = K::A; break; case 'D': key = K::D; break; case 'E': key = K::E; break; case 'F': key = K::F; break;
            case 'Q': key = K::Q; break; case 'R': key = K::R; break; case 'S': key = K::S; break; case 'V': key = K::V; break;
            case 'W': key = K::W; break; case VK_LEFT: key = K::Left; break; case VK_RIGHT: key = K::Right; break;
            case VK_UP: key = K::Up; break; case VK_DOWN: key = K::Down; break; case VK_HOME: key = K::Home; break;
            case VK_DELETE: case VK_BACK: key = K::Delete; break; case VK_ESCAPE: key = K::Escape; break;
            case VK_OEM_4: key = K::BracketLeft; break; case VK_OEM_6: key = K::BracketRight; break;
            default: break;
            }
            const bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
            if (key != K::Other && core_->sceneKey(key, down, mods(), GetKeyState(VK_RBUTTON) < 0)) return 0;
            return -1;
        }
        default: return -1;
        }
    }
    static aiverse::platform::Key gameKey(WPARAM vk) {
        using aiverse::platform::Key;
        if (vk >= 'A' && vk <= 'Z') return Key(int(Key::A) + int(vk - 'A'));
        if (vk >= '0' && vk <= '9') return Key(int(Key::Num0) + int(vk - '0'));
        if (vk >= VK_F1 && vk <= VK_F12) return Key(int(Key::F1) + int(vk - VK_F1));
        switch (vk) {
        case VK_ESCAPE: return Key::Escape; case VK_RETURN: return Key::Enter; case VK_TAB: return Key::Tab;
        case VK_BACK: return Key::Backspace; case VK_DELETE: return Key::Delete; case VK_INSERT: return Key::Insert;
        case VK_SPACE: return Key::Space; case VK_LEFT: return Key::Left; case VK_RIGHT: return Key::Right;
        case VK_UP: return Key::Up; case VK_DOWN: return Key::Down; case VK_HOME: return Key::Home; case VK_END: return Key::End;
        case VK_PRIOR: return Key::PageUp; case VK_NEXT: return Key::PageDown;
        case VK_SHIFT: return Key::LeftShift; case VK_CONTROL: return Key::LeftCtrl; case VK_MENU: return Key::LeftAlt;
        case VK_LWIN: return Key::LeftSuper; case VK_RWIN: return Key::RightSuper;
        case VK_OEM_MINUS: return Key::Minus; case VK_OEM_PLUS: return Key::Equal; case VK_OEM_COMMA: return Key::Comma;
        case VK_OEM_PERIOD: return Key::Period; case VK_OEM_2: return Key::Slash; case VK_OEM_1: return Key::Semicolon;
        case VK_OEM_4: return Key::LeftBracket; case VK_OEM_6: return Key::RightBracket; case VK_OEM_7: return Key::Apostrophe;
        case VK_OEM_5: return Key::Backslash; case VK_OEM_3: return Key::GraveAccent;
        default: return Key::Unknown;
        }
    }
    static uint8_t gameMods() {
        using namespace aiverse::platform;
        uint8_t m = 0;
        if (GetKeyState(VK_SHIFT) < 0) m |= kModShift;
        if (GetKeyState(VK_CONTROL) < 0) m |= kModCtrl;
        if (GetKeyState(VK_MENU) < 0) m |= kModAlt;
        if (GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0) m |= kModSuper;
        return m;
    }
    LRESULT gameInput(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        using namespace aiverse::platform;
        const double k = scale();
        Event e;
        e.mods = gameMods();
        auto mouse = [&](EventType type, int button) {
            e.type = type;
            e.button = button;
            e.x = float(GET_X_LPARAM(lp) / k);
            e.y = float(GET_Y_LPARAM(lp) / k);
            e.deltaX = e.x - float(lastGame_.first);
            e.deltaY = e.y - float(lastGame_.second);
            lastGame_ = {e.x, e.y};
            if (type == EventType::MouseDown) { SetFocus(h); SetCapture(h); }
            if (type == EventType::MouseUp && !(wp & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON))) ReleaseCapture();
            core_->gameEvent(e);
            return LRESULT(0);
        };
        switch (msg) {
        case WM_LBUTTONDOWN: return mouse(EventType::MouseDown, 0);
        case WM_RBUTTONDOWN: return mouse(EventType::MouseDown, 1);
        case WM_MBUTTONDOWN: return mouse(EventType::MouseDown, 2);
        case WM_LBUTTONUP: return mouse(EventType::MouseUp, 0);
        case WM_RBUTTONUP: return mouse(EventType::MouseUp, 1);
        case WM_MBUTTONUP: return mouse(EventType::MouseUp, 2);
        case WM_MOUSEMOVE: return mouse(EventType::MouseMove, 0);
        case WM_MOUSEWHEEL:
            e.type = EventType::MouseWheel;
            e.x = float(lastGame_.first);
            e.y = float(lastGame_.second);
            e.wheelY = float(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA;
            core_->gameEvent(e);
            return 0;
        case WM_KEYDOWN: case WM_SYSKEYDOWN: case WM_KEYUP: case WM_SYSKEYUP:
            if (msg == WM_SYSKEYDOWN && wp == VK_F4) return -1; // keep Alt+F4
            e.type = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) ? EventType::KeyDown : EventType::KeyUp;
            e.key = gameKey(wp);
            e.repeat = (lp & (1 << 30)) != 0 && e.type == EventType::KeyDown;
            core_->gameEvent(e);
            return 0;
        case WM_CHAR: {
            uint32_t c = uint32_t(wp);
            if (c >= 0xd800 && c <= 0xdbff) { highSurrogate_ = c; return 0; }
            if (c >= 0xdc00 && c <= 0xdfff && highSurrogate_) { c = 0x10000 + ((highSurrogate_ - 0xd800) << 10) + (c - 0xdc00); highSurrogate_ = 0; }
            if (c >= 32 && !(e.mods & (kModCtrl | kModSuper))) { e.type = EventType::Char; e.codepoint = c; core_->gameEvent(e); }
            return 0;
        }
        case WM_KILLFOCUS:
            e.type = EventType::FocusLost;
            core_->gameEvent(e);
            return -1;
        default: return -1;
        }
    }

    std::string project_;
    bool dev_ = false;
    HWND main_ = nullptr;
    HWND surfaces_[2] = {nullptr, nullptr}; // Scene, Game
    Com<ICoreWebView2Controller> controller_;
    Com<ICoreWebView2> web_;
    std::unique_ptr<studio::StudioHost> core_;
    int captured_ = -1;
    bool tracking_ = false;
    std::pair<double, double> last_{0, 0}, lastGame_{0, 0};
    uint32_t highSurrogate_ = 0;
    Com<ICoreWebView2Environment> env_;
    std::vector<std::unique_ptr<FloatPane>> floats_;
    ICoreWebView2* currentWeb_ = nullptr;
    bool closingFloats_ = false;
    struct SizeDrag {
        bool on = false;
        bool busy = false;
        int edge = 0; // 1 left, 2 right, 4 top, 8 bottom
        POINT origin{};
        RECT start{};
    };
    SizeDrag size_;
};
} // namespace

// PlatformWin32.cpp (aiverse_runtime) owns WinMain / main and forwards to aiverseMain().
int aiverseMain(int argc, char** argv) {
    // Per-monitor DPI (the manifest asks for it too; this covers a missing manifest).
    if (HMODULE user = GetModuleHandleW(L"user32.dll")) {
        using SetContext = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        if (auto set = reinterpret_cast<SetContext>(reinterpret_cast<void*>(GetProcAddress(user, "SetProcessDpiAwarenessContext"))))
            set(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
    std::string project;
    bool dev = false;
    // The UTF-8 code page manifest makes argv UTF-8; read the wide command line anyway (older Windows).
    int wargc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    for (int i = 1; i < wargc; ++i) {
        const std::wstring arg = wargv[i];
        if (arg == L"--dev") dev = true;
        else if (arg == L"--project" && i + 1 < wargc) project = narrow(wargv[++i]);
        else {
            LocalFree(wargv);
            MessageBoxW(nullptr, L"Usage: AiverseStudio.exe [--dev] [--project <folder>]", L"AIVERSE Studio", MB_OK);
            return 2;
        }
    }
    LocalFree(wargv);
    (void)argc; (void)argv;
    try {
        WinShell shell(project, dev);
        gShell = &shell;
        const int code = shell.run();
        gShell = nullptr;
        return code;
    } catch (const std::exception& e) {
        logLine(std::string("Studio startup failed: ") + e.what());
        MessageBoxW(nullptr, widen(e.what()).c_str(), L"AIVERSE Studio", MB_OK | MB_ICONERROR);
        return 1;
    }
}
