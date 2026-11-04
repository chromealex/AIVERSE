#include "ClientHost.h"
#include "imgui.h"
#include "apps/client/GameClient.h"

namespace rts::editor {

using platform::Event;
using platform::EventType;

// Around every call into the client: its platform view, then the editor's ImGui context back.
struct ClientHost::Scope {
    ImGuiContext* imgui;
    platform::HostedView* prev;
    explicit Scope(ClientHost& h) : imgui(ImGui::GetCurrentContext()), prev(platform::hostedView()) { platform::setHostedView(&h.view_); }
    ~Scope() { platform::setHostedView(prev); ImGui::SetCurrentContext(imgui); }
};

static_assert(client::GameClient::kInitSteps == 6, "ClientHost::kSteps follows GameClient::kInitSteps");

ClientHost::ClientHost() = default;
ClientHost::~ClientHost() { stop(); }

void ClientHost::start(gfx::Device& dev, const std::string& projectDir, const std::string& engineAssets) {
    stop();
    dev_ = &dev;
    view_ = {};
    view_.contentRoot = projectDir;
    view_.engineAssets = engineAssets;
    view_.width = 360; view_.height = 640; view_.fbWidth = 360; view_.fbHeight = 640;   // until the first frame
    client_ = std::make_unique<client::GameClient>();
    step_ = 0;
    captured_ = false;
}

void ClientHost::stop() {
    if (!client_) return;
    {
        Scope s(*this);
        if (step_ > 0) client_->shutdown();
        client_.reset();
    }
    step_ = 0;
    captured_ = false;
}

float ClientHost::progress() const {
    float done = 0;
    for (int i = 0; i < step_ && i < kSteps; ++i) done += client::GameClient::initWeight(i);
    return done;
}

void ClientHost::frame(double dt, gfx::TextureHandle color, gfx::TextureHandle depth, int w, int h, float pointsW, float pointsH,
                       bool touch, const platform::Insets& safe) {
    if (!client_ || w <= 0 || h <= 0 || pointsW <= 0 || pointsH <= 0) return;
    const bool resized = view_.fbWidth != w || view_.fbHeight != h || view_.width != pointsW || view_.height != pointsH ||
                         view_.safe.left != safe.left || view_.safe.top != safe.top || view_.safe.right != safe.right || view_.safe.bottom != safe.bottom;
    view_.width = pointsW; view_.height = pointsH;
    view_.fbWidth = w; view_.fbHeight = h;
    view_.scale = float(w) / pointsW;
    view_.touch = touch;
    view_.safe = safe;
    Scope s(*this);
    client_->setHostTarget(color, depth, w, h);
    if (step_ < kSteps) { client_->initStep(*dev_, step_++); return; }
    if (resized) { Event e; e.type = EventType::Resized; client_->event(e); }
    client_->frame(*dev_, dt);
}

void ClientHost::event(const Event& in) {
    if (!client_ || step_ < kSteps) return;
    Event e = in;
    switch (in.type) {
    case EventType::MouseMove: case EventType::MouseDown: case EventType::MouseUp: case EventType::MouseWheel:
    case EventType::TouchBegin: case EventType::TouchMove: case EventType::TouchEnd: case EventType::TouchCancel: {
        e.x = (in.x - x_) / k_;
        e.y = (in.y - y_) / k_;
        const bool inside = e.x >= 0 && e.y >= 0 && e.x < view_.width && e.y < view_.height;
        if (in.type == EventType::MouseDown || in.type == EventType::TouchBegin) {
            if (!inside) return;
            captured_ = true;
        } else if (in.type == EventType::MouseUp || in.type == EventType::TouchEnd || in.type == EventType::TouchCancel) {
            if (!captured_) return;
            captured_ = false;
        } else if (in.type == EventType::MouseWheel) {
            if (!inside) return;
        }
        break;   // moves always: hover leaves, edge scrolling stops outside the screen
    }
    case EventType::KeyDown: case EventType::KeyUp: case EventType::Char:
        if (!focused_) return;
        break;
    case EventType::FocusLost: case EventType::FocusGained: break;
    default: return;   // Resized (the host sends its own), quit, open-file: the editor's
    }
    Scope s(*this);
    client_->event(e);
}

Simulation* ClientHost::simulation() { return client_ && step_ >= kSteps ? client_->simulation() : nullptr; }
bool ClientHost::inMatch() const { return client_ && step_ >= kSteps && client_->inMatch(); }

} // namespace rts::editor
