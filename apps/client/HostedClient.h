// The shared game client hosted inside a native application surface: the same GameClient as AiverseGame (menus /
// meta screens of ui/main.lua, view scripts, script UI, mapClick input, audio, pause screen), rendered
// into the window's offscreen target. While the host calls into the client it sets
// platform::setHostedView (size / scale / safe area / content root of the device screen) and
// restores the editor's ImGui context afterwards. Events of the editor window are mapped into the
// device screen (logical points) and forwarded: pointer events inside it (or captured by a press
// inside), keys while the Game window has focus. On a touch device profile the left button is a finger.
#pragma once
#include <memory>
#include <optional>
#include <vector>
#include <cstdint>
#include <string>
#include "engine/gfx/Gfx.h"
#include "engine/render/RenderDeviceLifetime.h"
#include "engine/platform/Platform.h"

namespace aiverse { class Simulation; }
namespace aiverse::client { class GameClient; }

namespace aiverse::client {

class HostedClient {
public:
    HostedClient();
    ~HostedClient();
    // projectDir: content root; engineAssets: folder with the engine's assets ("engine/..." paths)
    void start(gfx::Device& dev, const std::string& projectDir, const std::string& engineAssets,
               render::RenderDeviceLifetime::Lease deviceLease={}, std::optional<size_t> initialMap={});
    void stop();
    // "Play from here": the first match camera (see GameClient::setStartCamera); call after start().
    void setStartCamera(float x, float z, float distance, float pitchDeg, float yawDeg);
    // A stopped hosted client may still own GPU handles referenced by a
    // submitted editor frame. Retire its GPU allocations only when the
    // backend confirms that frame's GPU fence. Never guess CPU frame age.
    size_t collectGpuRetirements();
    // Caller has explicitly synchronized *all* previously submitted GPU
    // work and is outside any render pass. Used at host teardown/pressure.
    size_t drainAfterGpuIdle();
    // Terminal device loss: never invoke deferred GameClient::shutdown with
    // a destroyed graphics device. Intentionally keep abandoned C++ owners
    // inert until process exit (no GPU destroy and no destructor callbacks).
    size_t abandonOnDeviceLoss();
    size_t pendingRetiredClients() const { return retired_.size(); }
    bool active() const { return client_ != nullptr; }
    bool loading() const;
    float progress() const;   // 0..1 while loading
    // One frame into color / depth (w x h pixels) showing a screen of pointsW x pointsH logical points.
    // While loading it runs the next init step instead (the window shows the progress).
    void frame(double dt, gfx::TextureHandle color, gfx::TextureHandle depth, int w, int h, float pointsW, float pointsH,
               bool touch, const platform::Insets& safe);
    // Where the screen is in the editor window (points) and how many editor points one screen point
    // takes; focused: the Game window has keyboard focus.
    void place(float x, float y, float k, bool focused);  // emits synthetic hosted focus edges
    void event(const platform::Event& e);
    void suspendInput(); // Game tab hidden: cancel held keys/touches even without a GameWindow draw
    bool quitRequested() const { return view_.quitRequested; }
    bool capturing() const { return captured_; }
    Simulation* simulation();
    bool inMatch() const;
    std::string errorText() const;
    bool canControl() const;
    bool paused() const;
    bool canStep() const;
    bool setPaused(bool paused);
    bool setSpeed(double speed);
    bool stepOnce();

private:
    struct Scope;
    static constexpr int kSteps = 6;   // GameClient::kInitSteps
    std::unique_ptr<client::GameClient> client_;
    struct RetiredClient {
        std::unique_ptr<client::GameClient> client;
        platform::HostedView view;
        gfx::Device* device = nullptr;
        uint64_t completionToken = 0; // zero means quarantine until GPU idle
        bool initialized = false;
    };
    std::vector<RetiredClient> retired_;
    platform::HostedView view_;
    gfx::Device* dev_ = nullptr;
    int step_ = 0;
    std::optional<size_t> initialMap_;
    float x_ = 0, y_ = 0, k_ = 1;
    bool focused_ = false, captured_ = false;
    bool finger_ = false;   // touch profile: the left button is down as finger 1 (event)
};

} // namespace aiverse::client
