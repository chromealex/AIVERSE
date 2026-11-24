#include "HostedClient.h"
#include <cstdlib>
#include <cstdio>
#include <stdexcept>
#include "imgui.h"
#include "apps/client/GameClient.h"

namespace aiverse::client {

using platform::Event;
using platform::EventType;

// Around every call into the client: its platform view, then the editor's ImGui context back.
struct HostedClient::Scope {
    ImGuiContext* imgui;
    platform::HostedView* prev;
    explicit Scope(platform::HostedView& view) : imgui(ImGui::GetCurrentContext()), prev(platform::hostedView()) { platform::setHostedView(&view); }
    explicit Scope(HostedClient& h) : Scope(h.view_) {}
    Scope(const Scope&)=delete;
    Scope& operator=(const Scope&)=delete;
    ~Scope() { platform::setHostedView(prev); ImGui::SetCurrentContext(imgui); }
};

static_assert(client::GameClient::kInitSteps == 6, "HostedClient::kSteps follows GameClient::kInitSteps");

HostedClient::HostedClient() = default;
bool HostedClient::currentEpochValid() const noexcept {
    return dev_ && epoch_.alive() && epoch_.device()==dev_ && epoch_.validFor(*dev_);
}
bool HostedClient::retiredEpochValid(const RetiredClient& item) const noexcept {
    return currentEpochValid() && item.device==dev_ && item.epoch.alive() &&
        item.epoch.generation()==epoch_.generation() && item.epoch.validFor(*dev_);
}
HostedClient::RetirementDiagnostics HostedClient::retirementDiagnostics() const noexcept {
    RetirementDiagnostics result;
    result.pending=retired_.size();
    for(const auto& item:retired_) {
        if(item.shutdownFailed)++result.quarantined;
        if(!retiredEpochValid(item))++result.staleEpoch;
        if(item.client && !item.client->canShutdown())++result.busy;
    }
    return result;
}
HostedClient::~HostedClient() {
    try {
        stop();
        // No device dereference after an already completed explicit drain.
        if(retired_.empty())return;
        if (canShutdown() && dev_ && dev_->gpuHealthy() && dev_->waitForSubmittedGpuWork()) {
            drainAfterGpuIdle();
            return;
        }
    } catch(const std::exception& e) {
        std::fprintf(stderr,"[hosted] destructor teardown refused: %s\n",e.what());
    } catch(...) {
        std::fputs("[hosted] destructor teardown refused\n",stderr);
    }
    // Terminal fail-closed path, including outstanding readers or partial
    // teardown: preserve owners rather than invoke unsafe destructors.
    try { stop(); abandonOnDeviceLoss(); }
    catch(...) {
        (void)client_.release();
        for(auto& item:retired_)(void)item.client.release();
    }
}

void HostedClient::start(gfx::Device& dev, const std::string& projectDir, const std::string& engineAssets,
                       render::RenderDeviceLifetime::Lease deviceLease, std::optional<size_t> initialMap) {
    if(deviceLease.device() && !deviceLease.validFor(dev))
        throw std::logic_error("HostedClient GPU device generation mismatch");
    const bool pending=client_ || !retired_.empty();
    if(pending) {
        if(!currentEpochValid() || dev_!=&dev ||
           (deviceLease.device() && deviceLease.generation()!=epoch_.generation()))
            throw std::logic_error("HostedClient previous device epoch still owns clients");
        // Do not switch an external host epoch to an implicit one mid-life.
        deviceLease=epoch_;
    } else {
        ownedEpoch_.reset();
        if(!deviceLease.device()) {
            ownedEpoch_=std::make_unique<render::RenderDeviceLifetime>(dev);
            deviceLease=ownedEpoch_->lease();
        }
        epoch_=deviceLease;
    }
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

void HostedClient::setStartCamera(float x, float z, float distance, float pitchDeg, float yawDeg) {
    if (client_) client_->setStartCamera(x, z, distance, pitchDeg, yawDeg);
}

void HostedClient::stop() {
    if (!client_) return;
    // Client may have submitted draws into the editor's current frame even
    // though the Game tab is now closing. Move ownership into an archive
    // tagged with a real GPU submission fence; do not destroy textures now.
    // Allocate/copy before transferring unique ownership. An allocation
    // failure must leave the active client intact.
    RetiredClient old;
    old.view = view_;
    old.device = dev_;
    old.epoch = epoch_;
    old.completionToken = currentEpochValid() ? dev_->gpuSubmissionFence() : 0;
    old.initialized = step_ > 0;
    retired_.push_back(std::move(old));
    retired_.back().client=std::move(client_);
    step_ = 0;
    captured_ = finger_ = focused_ = false;
}

size_t HostedClient::collectGpuRetirements() {
    if (!currentEpochValid()) return 0;
    if (!dev_->gpuHealthy()) {
        abandonOnDeviceLoss();
        return 0;
    }
    const uint64_t completed = dev_->gpuCompletedFence();
    if (!completed) return 0;
    size_t collected = 0;
    for (auto it = retired_.begin(); it != retired_.end();) {
        if (!retiredEpochValid(*it) || !it->completionToken ||
            it->completionToken > completed) {
            ++it;
            continue;
        }
        if(it->shutdownFailed || (it->client && !it->client->canShutdown())) {
            ++it;continue;
        }
        try {
            // Restore both contexts on success AND exception, before erase.
            Scope scope(it->view);
            if (it->initialized && it->client) it->client->shutdown();
            it->client.reset();
        } catch(const std::exception& e) {
            it->shutdownFailed=true;
            std::fprintf(stderr,"[hosted] retirement quarantined: %s\n",e.what());
            ++it;continue;
        } catch(...) {
            it->shutdownFailed=true;
            std::fputs("[hosted] retirement quarantined: unknown shutdown exception\n",stderr);
            ++it;continue;
        }
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

bool HostedClient::canShutdown() const noexcept {
    if(client_ && (!currentEpochValid() || !client_->canShutdown()))return false;
    for(const auto& item:retired_)
        if(!retiredEpochValid(item) || item.shutdownFailed || (item.client && !item.client->canShutdown()))return false;
    return true;
}

size_t HostedClient::drainAfterGpuIdle() {
    // Caller proves GPU idle. Refuse all known blockers before starting.
    // A successful prefix is erased immediately, so a failed suffix cannot
    // be replayed against already destroyed/null clients on another drain.
    for(const auto& item:retired_) {
        if(item.shutdownFailed || (item.client && !item.client->canShutdown()))
            throw std::logic_error("HostedClient drain: blocked or quarantined client");
        if(!retiredEpochValid(item))
            throw std::logic_error("HostedClient drain: stale device epoch");
    }
    size_t drained = 0;
    for(auto it=retired_.begin();it!=retired_.end();) {
        try {
            Scope scope(it->view);
            if(it->initialized && it->client)it->client->shutdown();
            it->client.reset();
        } catch(...) {
            // An arbitrary shutdown failure may have changed other owners.
            // Do not retry blindly; retain this client for terminal recovery.
            it->shutdownFailed=true;
            throw;
        }
        it=retired_.erase(it);
        ++drained;
    }
    return drained;
}

float HostedClient::progress() const {
    float done = 0;
    for (int i = 0; i < step_ && i < kSteps; ++i) done += client::GameClient::initWeight(i);
    return done;
}

void HostedClient::frame(double dt, gfx::TextureHandle color, gfx::TextureHandle depth, int w, int h, float pointsW, float pointsH,
                       bool touch, const platform::Insets& safe) {
    if (!client_ || !currentEpochValid() || w <= 0 || h <= 0 || pointsW <= 0 || pointsH <= 0) return;
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
