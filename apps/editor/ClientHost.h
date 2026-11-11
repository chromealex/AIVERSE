// The game client hosted inside the editor's Game window: the same GameClient as AiverseGame (menus /
// meta screens of ui/main.lua, view scripts, script UI, mapClick input, audio, pause screen), rendered
// into the window's offscreen target. While the host calls into the client it sets
// platform::setHostedView (size / scale / safe area / content root of the device screen) and
// restores the editor's ImGui context afterwards. Events of the editor window are mapped into the
// device screen (logical points) and forwarded: pointer events inside it (or captured by a press
// inside), keys while the Game window has focus. On a touch device profile the left button is a finger.
#pragma once
#include <memory>
#include <string>
#include "engine/gfx/Gfx.h"
#include "engine/platform/Platform.h"

namespace aiverse { class Simulation; }
namespace aiverse::client { class GameClient; }

namespace aiverse::editor {

class ClientHost {
public:
    ClientHost();
    ~ClientHost();
    // projectDir: content root; engineAssets: folder with the engine's assets ("engine/..." paths)
    void start(gfx::Device& dev, const std::string& projectDir, const std::string& engineAssets);
    void stop();
    bool active() const { return client_ != nullptr; }
    bool loading() const { return active() && step_ < kSteps; }
    float progress() const;   // 0..1 while loading
    // One frame into color / depth (w x h pixels) showing a screen of pointsW x pointsH logical points.
    // While loading it runs the next init step instead (the window shows the progress).
    void frame(double dt, gfx::TextureHandle color, gfx::TextureHandle depth, int w, int h, float pointsW, float pointsH,
               bool touch, const platform::Insets& safe);
    // Where the screen is in the editor window (points) and how many editor points one screen point
    // takes; focused: the Game window has keyboard focus.
    void place(float x, float y, float k, bool focused) { x_ = x; y_ = y; k_ = k > 0 ? k : 1; focused_ = focused; }
    void event(const platform::Event& e);
    bool quitRequested() const { return view_.quitRequested; }
    bool capturing() const { return captured_; }
    Simulation* simulation();
    bool inMatch() const;

private:
    struct Scope;
    static constexpr int kSteps = 6;   // GameClient::kInitSteps
    std::unique_ptr<client::GameClient> client_;
    platform::HostedView view_;
    gfx::Device* dev_ = nullptr;
    int step_ = 0;
    float x_ = 0, y_ = 0, k_ = 1;
    bool focused_ = false, captured_ = false;
    bool finger_ = false;   // touch profile: the left button is down as finger 1 (event)
};

} // namespace aiverse::editor
