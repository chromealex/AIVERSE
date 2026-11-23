#pragma once
// Portable half of the HTML5 Studio host: everything between the WebView bridge, the StudioSession and the
// native Scene / Game GPU views that does not depend on the window system. mac/Main.mm (Cocoa, WKWebView, Metal)
// and win/Main.cpp (Win32, WebView2, Vulkan) are thin shells around it: they own windows, web views, native
// views, dialogs and the timer, and forward bridge messages, input and frame ticks here.
#include "apps/studio-native/StudioSession.h"
#include "apps/studio-native/StudioScene.h"
#include "apps/studio-native/StudioGameSurface.h"
#include "apps/studio-native/StudioViewportLayout.h"
#include "apps/studio-native/StudioRenderThread.h"
#include "engine/data/Json.h"
#include "engine/platform/Platform.h"
#include <atomic>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace aiverse::studio {

enum class StudioSurface { Scene, Game, Fx };

// What a shell provides. Called on the main (UI) thread only, except runInBackground's work.
class StudioHostPlatform {
public:
    virtual ~StudioHostPlatform() = default;
    // Web views. currentOnly: the web view whose message is being handled (all of them when there is none).
    virtual void evaluateScript(const std::string& js, bool currentOnly) = 0;
    // GPU devices for the native views (created lazily, the first time the view is shown). The Scene's device is
    // created ON THE SCENE RENDER THREAD (its GPU caches bind to that thread): use a handle captured beforehand,
    // never UI objects.
    virtual std::unique_ptr<gfx::Device> createDevice(StudioSurface surface) = 0;
    // Wraps work on the Scene render thread that draws (macOS: autorelease pool + explicit CATransaction).
    virtual void sceneThreadScope(const std::function<void()>& body) { body(); }
    // The native view is shown in a visible, not minimised window.
    virtual bool surfaceDrawable(StudioSurface surface) = 0;
    // The Game view has keyboard focus (input goes to the game).
    virtual bool gameFocused() = 0;
    // studio.viewportRect: move / show / hide the native view over the panel's DOM rect, in the web view that sent it.
    // Fill `out` (pixel size for the drawable). False: a stale report from a window that no longer hosts the view.
    virtual bool placeSurface(StudioSurface surface, const ViewportRect& rect, NativeViewportPlacement& out) = 0;
    // The Game device profile changed (macOS draws the phone bezel and safe area over the Game view).
    virtual void gameDeviceChanged(const Json& state) { (void)state; }
    // Threads and time.
    virtual double now() = 0; // seconds, monotonic
    virtual void runInBackground(std::function<void()> work, std::function<void()> thenOnMain) = 0;
    // Dialogs. Callbacks run on the main thread; an empty path / list means cancelled.
    virtual bool confirmDiscardChanges(const std::string& title, const std::string& message, const std::string& discardLabel) = 0;
    virtual void chooseProject(std::function<void(std::string)> done) = 0;
    virtual void chooseFolder(const std::string& message, const std::string& startDir, std::function<void(std::string)> done) = 0;
    virtual void chooseSaveFile(const std::string& startDir, const std::string& defaultName, std::function<void(std::string)> done) = 0;
    virtual void chooseImportSources(const std::string& startDir, std::function<void(std::vector<std::filesystem::path>)> done) = 0;
    virtual void revealInFileManager(const std::filesystem::path& path) = 0;
    // Per-user cache folder (asset thumbnails) and the last opened project (reopened on the next launch).
    virtual std::filesystem::path cacheDir() = 0;
    virtual void rememberLastProject(const std::string& dir) = 0;
    // Window requests from the UI: studio.windowOpen / windowClose / windowTitle / windowDrag / windowZoom.
    // Returns the result (throws on bad input). A shell without floating windows answers {opened:false}.
    virtual Json windowRequest(const std::string& method, const Json& params) = 0;
    // Floating panel windows are open (they follow the main window's selection through selection.changed).
    virtual bool hasFloatingWindows() { return false; }
    // Optional decoder for image formats stb_image does not read (macOS: TIFF, WebP through ImageIO).
    virtual bool decodeImage(const std::filesystem::path& file, int& width, int& height, std::vector<uint8_t>& rgba) {
        (void)file; (void)width; (void)height; (void)rgba; return false;
    }
    virtual void log(const std::string& message) = 0;
};

// Pointer / keyboard input of the Scene view in a shell-neutral form (Unity Scene view mapping, see sceneInput*).
struct StudioSceneMods { bool shift = false, ctrl = false, alt = false, command = false; }; // command: Cmd on macOS, Ctrl on Windows
enum class StudioSceneKey { Other, A, D, E, F, Q, R, S, V, W, Left, Right, Up, Down, Home, Delete, Escape, BracketLeft, BracketRight };

class StudioHost {
public:
    StudioHost(StudioHostPlatform& platform, const std::string& engineRoot, bool htmlComposite);
    ~StudioHost();
    StudioSession& session() { return *session_; }
    StudioScene* scene() { return preview_.get(); }
    // Runs fn on the Scene render thread between its frames and waits (inline when there is none yet).
    // Shells use it for Scene surface changes, e.g. resizing the drawable.
    void runOnSceneThread(const std::function<void()>& fn) { if (renderThread_) renderThread_->invoke(fn); else fn(); }
    // Separate dedicated FX drawable; both GPU Scenes have the same owner
    // thread, but independent devices/swapchains and camera lifetimes.
    void fxCameraOrbit(double dx,double dy);
    void fxCameraZoom(double wheel);
    bool projectLoading() const { return projectLoading_; }
    bool htmlComposite() const { return htmlComposite_; }

    // Frame tick (~60 Hz) from the shell's timer.
    void tick();
    // A bridge message from a trusted web view (JSON text, at most 1 MB).
    void handleMessage(const std::string& json);
    // The main web view finished loading: the --project / last project opens now (the UI can show the splash).
    void webReady();
    void setPendingProject(const std::string& path) { pendingProject_ = path; }
    void openProject(const std::string& path, int64_t requestId);
    // Before the main window closes or the app quits. False: keep it open.
    bool confirmClose();
    void shutdown();

    // Events to every web view (window.__aiverseReceive).
    void sendEvent(const std::string& event, const std::string& message);
    void sendEventPayload(const std::string& event, const Json& payload);
    void sendReply(const Json& reply);

    // Scene view operations (coordinates in view points, origin top-left).
    void orbit(double dx, double dy);
    void zoom(double amount);
    void look(double dx, double dy);
    void pan(double dx, double dy);
    void moveCamera(double right, double up, double forward);
    void focusSelection();
    void resetView();
    void frameAll();
    void setPivotSnap(bool on);
    void setTool(int tool);
    int tool();
    bool gizmoAt(double x, double y, double w, double h);
    bool moveBegin(double x, double y, double w, double h);
    void moveUpdate(double x, double y, double w, double h, bool invertSnap);
    void moveEnd();
    void marquee(double ax, double ay, double bx, double by, double w, double h);
    void marqueeEnd(double ax, double ay, double bx, double by, double w, double h, const std::string& mode);
    void hover(double x, double y, double w, double h); // x < 0: left the view
    void select(double x, double y, double w, double h, const std::string& mode);
    void selectionAction(const std::string& method);
    bool brushArmed() const;
    bool updateBrushCursor(double x, double y, double w, double h);
    void brushAdjust(double factor, bool disarm);
    bool splineDrag(int phase, double x, double y, double w, double h);
    bool terrainStroke(int phase, double x, double y, double w, double h);
    bool placeAsset(const std::string& asset, double x, double y, double w, double h);
    bool placeType(const std::string& type, double x, double y, double w, double h);
    // Files dropped from the file manager on the Assets panel (folder: the panel's drop target).
    void importDroppedFiles(const std::vector<std::filesystem::path>& files, const std::string& folder);

    // Shell-neutral Scene input: a shell that does not map events itself (Windows) forwards them here.
    // button 0 left, 1 right, 2 middle. phase 0 press, 1 drag/move, 2 release. Deltas in points.
    void sceneMouse(int phase, int button, double x, double y, double dx, double dy, double w, double h, StudioSceneMods mods);
    void sceneWheel(double amount);
    // True when handled. rightHeld: flythrough (WASD/QE fly while RMB is held).
    bool sceneKey(StudioSceneKey key, bool down, StudioSceneMods mods, bool rightHeld);

    // Game view.
    void gameEvent(const platform::Event& event);
    void gameSuspend() { if (game_) game_->suspend(); }
    // A native view went back to the main window (its floating window closed): hidden until the UI reports its rect.
    void surfaceDetached(StudioSurface surface);
    Json gameState();

private:
    // The render thread owns the Scene and its GPU resources: every Scene call from the UI thread runs
    // there and waits for its result (inline before the thread exists).
    template <class F> auto onScene(F&& fn) -> decltype(fn()) {
        if (!renderThread_) return fn();
        return renderThread_->invoke(std::forward<F>(fn));
    }
    // Inputs of one map frame, snapshotted on the UI thread for the render thread.
    struct FrameInputs {
        int width = 0, height = 0;
        std::shared_ptr<const editor::MapDoc> map;
        std::shared_ptr<const Json> rules;
        bool fps = false;
        std::string selected, project, hover;
        float levelHeight = 1.5f;
        std::set<std::string> selection, hidden, locked;
        StudioWindState wind;
    };
    // Results of the last finished Scene frame, consumed by the UI thread.
    struct FrameOutputs {
        std::string error, facialError, lightingStatus;
        bool facialReady = false;
        StudioScene::LoadProgress load;
        StudioScene::CameraState camera;
        std::vector<std::string> thumbnails; // Assets panel model thumbnails finished since the last frame
        uint32_t drawCalls = 0;
    };
    void tickFx();                     // UI thread: independent native FX surface
    void tickScene();                  // UI thread: snapshot inputs, kick the render thread, publish results
    void renderSceneFrame();           // render thread: one map frame
    void publishSceneFrame(const FrameOutputs& out);
    void failScene(const std::string& message);
    Json request(const char* method, Json params);
    void sendLoading(const std::string& phase, const std::string& message, int done, int total);
    void sendProjectSplash(const std::string& path);
    void sendProjectCancelled(int64_t requestId);
    void sendError(const Json& id, const std::string& message);
    void rebuildOverlay();
    void publishCamera(const StudioScene::CameraState& camera);
    void tickGame();
    void tickAgentScreenshot();
    void pollAgentRequests();
    Json assetThumbnail(const std::string& path);
    Json viewportInput(const Json& input);
    Json runSpectraPreview(const Json& params); // shared HTML5 / authenticated MCP bridge
    void applyViewport(const Json& params);
    bool handleHostMethod(const Json& req); // methods the host answers itself; false: StudioSession's
    void afterSessionReply(const Json& req, const Json& reply);
    // Image helpers (stb): read any image the platform or stb can decode, scale to fit, encode as a data URL.
    bool readImage(const std::filesystem::path& file, int& w, int& h, std::vector<uint8_t>& rgba);
    std::string imageDataUrl(const std::filesystem::path& file, int maxSide, bool jpeg);

    StudioHostPlatform& platform_;
    std::string engineRoot_;
    bool htmlComposite_ = false;
    std::unique_ptr<StudioSession> session_;
    std::unique_ptr<StudioScene> preview_;             // created, used and destroyed on renderThread_
    std::unique_ptr<StudioScene> fxPreview_;           // separate GPU drawable + isolated camera; same owner render thread
    std::unique_ptr<StudioRenderThread> renderThread_;
    std::mutex renderIoMutex_;                         // guards renderInputs_ / renderOutputs_
    FrameInputs renderInputs_;
    FrameOutputs renderOutputs_;
    bool renderOutputsFresh_ = false;
    std::shared_ptr<const editor::MapDoc> mapSnapshot_;
    const editor::MapDoc* mapSnapshotSource_ = nullptr;
    std::shared_ptr<const Json> rulesSnapshot_;
    std::string rulesSnapshotProject_;
    size_t rulesSnapshotHash_ = 0;
    double nextMapSnapshot_ = 0, nextRulesCheck_ = 0;
    std::unique_ptr<StudioGameSurface> game_;
    NativeViewportPlacement placement_{}, gamePlacement_{}, fxPlacement_{};
    // Independent native FX drawable/swapchain: no Scene surface reparent,
    // no camera hijack. Both run on the existing native render owner thread.
    bool spectraDockActive_=false;
    double gameFrameTime_ = 0;
    struct AgentShot { std::string png; int w = 0, h = 0; std::atomic<bool> done{false}; };
    std::shared_ptr<AgentShot> agentShot_; // agent view_screenshot capture on its way
    bool readyEmitted_ = false, projectLoading_ = false, loadingShown_ = false, surfaceSnap_ = false;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true); // background completions after shutdown
    double nextGizmoPoll_ = 0, nextAgentPoll_ = 0, lastSceneFrame_ = 0, sceneFrameMs_ = 0;
    uint64_t sceneFrameCount_ = 0;
    std::deque<std::pair<float, uint32_t>> frameHistory_;
    std::vector<StudioScene::OverlayShape> extShapes_;
    Json splineCache_;
    std::string splineDrag_, lastActionCaption_, hoverId_, pendingProject_;
    unsigned lightingStatusTicks_ = 0;
    int lastLoadDone_ = -1, lastLoadTotal_ = -1;
    double lastCameraYaw_ = 0, lastCameraPitch_ = 0, lastCameraDistance_ = 0;
    bool lastCameraOrtho_ = false, cameraSent_ = false;
    // sceneMouse state (shells that use the neutral mapping)
    struct { double pressX = 0, pressY = 0; bool dragged = false, move = false, stroke = false, spline = false, marquee = false, active = false; } mouse_;
    double lastHoverTime_ = 0;
};

} // namespace aiverse::studio
