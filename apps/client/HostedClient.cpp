#include "HostedClient.h"
#include <cstdlib>
#include "imgui.h"
#include "apps/client/GameClient.h"

namespace aiverse::client {

using platform::Event;
using platform::EventType;

// Around every call into the client: its platform view, then the editor's ImGui context back.
struct HostedClient::Scope {
    ImGuiContext* imgui;
    platform::HostedView* prev;
    explicit Scope(HostedClient& h) : imgui(ImGui::GetCurrentContext()), prev(platform::hostedView()) { platform::setHostedView(&h.view_); }
    ~Scope() { platform::setHostedView(prev); ImGui::SetCurrentContext(imgui); }
};

static_assert(client::GameClient::kInitSteps == 6, "HostedClient::kSteps follows GameClient::kInitSteps");

HostedClient::HostedClient() = default;
HostedClient::~HostedClient() {
    stop();
    // Prefer a real submitted-work GPU-idle wait; never rely on the CPU
    // frame serial. If the device is lost/unknown, preserve the client owner
    // inert rather than implicitly running GameClient's destructor, which
    // could transitively touch an invalid graphics device.
    if (dev_ && dev_->gpuHealthy() && dev_->waitForSubmittedGpuWork())
        drainAfterGpuIdle();
    else
        abandonOnDeviceLoss();
}

void HostedClient::start(gfx::Device& dev, const std::string& projectDir, const std::string& engineAssets,
                       render::RenderDeviceLifetime::Lease deviceLease, std::optional<size_t> initialMap) {
    if(deviceLease.device() && !deviceLease.validFor(dev))
        throw std::logic_error("HostedClient GPU device generation mismatch");
    stop();
    dev_ = &dev;
    initialMap_ = initialMap;
    view_ = {};
    view_.contentRoot = projectDir;
    view_.engineAssets = engineAssets;
    view_.width = 360; view_.height = 640; view_.fbWidth = 360; view_.fbHeight = 640;   // until the first frame
    client_ = std::make_unique<client::GameClient>();
    client_->setGpuDeviceLease(std::move(deviceLease));
    step_ = 0;
    captured_ = finger_ = focused_ = false;
}

void HostedClient::stop() {
    if (!client_) return;
    // Client may have submitted draws into the editor's current frame even
    // though the Game tab is now closing. Move ownership into an archive
    // tagged with a real GPU submission fence; do not destroy textures now.
    RetiredClient old;
    old.client = std::move(client_);
    old.view = view_;
    old.device = dev_;
    old.completionToken = dev_ ? dev_->gpuSubmissionFence() : 0;
    old.initialized = step_ > 0;
    retired_.push_back(std::move(old));
    step_ = 0;
    captured_ = finger_ = focused_ = false;
}

size_t HostedClient::collectGpuRetirements() {
    if (!dev_) return 0;
    if (!dev_->gpuHealthy()) {
        abandonOnDeviceLoss();
        return 0;
    }
    const uint64_t completed = dev_->gpuCompletedFence();
    if (!completed) return 0;
    size_t collected = 0;
    for (auto it = retired_.begin(); it != retired_.end();) {
        if (it->device != dev_ || !it->completionToken ||
            it->completionToken > completed) {
            ++it;
            continue;
        }
        // The outgoing client has a separate HostedView: restore the
        // previous OS/editor context even if the new client is active.
        platform::HostedView* prior = platform::hostedView();
        ImGuiContext* imgui = ImGui::GetCurrentContext();
        platform::setHostedView(&it->view);
        if (it->initialized) it->client->shutdown();
        it->client.reset();
        platform::setHostedView(prior);
        ImGui::SetCurrentContext(imgui);
        it = retired_.erase(it);
        ++collected;
    }
    return collected;
}

size_t HostedClient::abandonOnDeviceLoss() {
    // Terminal recovery only. Retired owners may contain nested renderers
    // whose destructors invoke Device methods. Preserve CPU state in a
    // process-lifetime no-dtor sink instead of running those destructors
    // against a dead GPU context. This is an intentional memory leak until
    // process exit, not ordinary hot-reload memory retirement.
    static auto* abandoned = new std::vector<RetiredClient>();
    const size_t count = retired_.size();
    for (auto& item : retired_)
        abandoned->push_back(std::move(item));
    retired_.clear();
    return count;
}

size_t HostedClient::drainAfterGpuIdle() {
    // Requires the host to prove GPU idle; unlike completed-token collection
    // this also drains entries for which no submission fence was available.
    size_t drained = 0;
    for (auto& item : retired_) {
        platform::HostedView* prior = platform::hostedView();
        ImGuiContext* imgui = ImGui::GetCurrentContext();
        platform::setHostedView(&item.view);
        if (item.initialized) item.client->shutdown();
        item.client.reset();
        platform::setHostedView(prior);
        ImGui::SetCurrentContext(imgui);
        ++drained;
    }
    retired_.clear();
    return drained;
}

float HostedClient::progress() const {
    float done = 0;
    for (int i = 0; i < step_ && i < kSteps; ++i) done += client::GameClient::initWeight(i);
    return done;
}

void HostedClient::frame(double dt, gfx::TextureHandle color, gfx::TextureHandle depth, int w, int h, float pointsW, float pointsH,
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
    if (step_ < kSteps) {
        client_->initStep(*dev_, step_++);
        // AIVERSE_HOSTED_MAP=<index>: straight into a match of that map (automated tests / profiling, like the client's --map)
        if (step_ == kSteps) {
            if (initialMap_) client_->autostart(*initialMap_);
            else if (const char* m = std::getenv("AIVERSE_HOSTED_MAP"); m && *m) client_->autostart(size_t(std::atoi(m)));
        }
        return;
    }
    if (resized) { Event e; e.type = EventType::Resized; client_->event(e); }
    client_->frame(*dev_, dt);
}

void HostedClient::place(float x,float y,float k,bool focused) {
    x_=x; y_=y; k_=k>0?k:1;
    const bool previous=focused_;
    focused_=focused;
    if(previous==focused_ || !client_ || step_<kSteps) return;
    // Moving from Game to Inspector is not an OS focus event: release hosted input.
    if(!focused_) captured_=finger_=false;
    Event focus;
    focus.type=focused_?EventType::FocusGained:EventType::FocusLost;
    Scope scope(*this);
    client_->event(focus);
}

void HostedClient::suspendInput() {
    const bool held=focused_||captured_;
    focused_=captured_=finger_=false;
    if(!held || !client_ || step_<kSteps)return;
    Event e;e.type=EventType::FocusLost;
    Scope scope(*this);
    client_->event(e);
}

void HostedClient::event(const Event& in) {
    if (!client_ || step_ < kSteps) return;
    Event e = in;
    // A touch device profile gets touches, as on the phone (and as AIVERSE_TOUCH_EMULATE in PlatformLinux): the left button
    // is finger 1 (TouchBegin / Move / End, no hover moves), so the client's touch paths run - one-finger pan, touch
    // map-drags (view.targeting().touch: e.g. an aim offset above the finger). Right / middle button and wheel stay mouse.
    const bool left = (in.type == EventType::MouseDown || in.type == EventType::MouseUp) && in.button == 0;
    if (in.type == EventType::MouseDown && left) finger_ = view_.touch;
    if (in.type == EventType::FocusLost) finger_ = false;   // the client cancels its pointer interactions itself
    if (finger_) {
        if (left) { e.type = in.type == EventType::MouseDown ? EventType::TouchBegin : EventType::TouchEnd; e.touchId = 1; }
        else if (in.type == EventType::MouseMove) { e.type = EventType::TouchMove; e.touchId = 1; }
        if (in.type == EventType::MouseUp && left) finger_ = false;
    } else if (view_.touch && in.type == EventType::MouseMove) {
        return;   // no hover on a touch screen
    }
    switch (e.type) {
    case EventType::MouseMove: case EventType::MouseDown: case EventType::MouseUp: case EventType::MouseWheel:
    case EventType::TouchBegin: case EventType::TouchMove: case EventType::TouchEnd: case EventType::TouchCancel: {
        e.x = (in.x - x_) / k_;
        e.y = (in.y - y_) / k_;
        const bool inside = e.x >= 0 && e.y >= 0 && e.x < view_.width && e.y < view_.height;
        if (e.type == EventType::MouseDown || e.type == EventType::TouchBegin) {
            if (!inside) { finger_ = false; return; }
            captured_ = true;
        } else if (e.type == EventType::MouseUp || e.type == EventType::TouchEnd || e.type == EventType::TouchCancel) {
            if (!captured_) return;
            captured_ = false;
        } else if (e.type == EventType::MouseWheel) {
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

Simulation* HostedClient::simulation() { return client_ && step_ >= kSteps ? client_->simulation() : nullptr; }
bool HostedClient::loading() const { return client_ && (step_ < kSteps || client_->mapLoading()); }
std::string HostedClient::errorText() const { return client_ ? client_->errorText() : std::string(); }
bool HostedClient::canControl() const { return client_ && step_ >= kSteps && client_->hostCanControl(); }
bool HostedClient::paused() const { return client_ && step_ >= kSteps && client_->hostPaused(); }
bool HostedClient::canStep() const { return client_ && step_ >= kSteps && client_->hostCanStep(); }
bool HostedClient::setPaused(bool paused) {
    if (!canControl()) return false;
    Scope scope(*this);return client_->setHostPaused(paused);
}
bool HostedClient::stepOnce() {
    if (!canStep()) return false;
    Scope scope(*this);return client_->requestHostStep();
}
bool HostedClient::inMatch() const { return client_ && step_ >= kSteps && client_->inMatch(); }

} // namespace aiverse::client
