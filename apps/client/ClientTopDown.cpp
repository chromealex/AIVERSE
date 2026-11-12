// Top-down heroes in the client (rules / game.json "topDown"): touch sticks and ability buttons, WASD / arrows +
// mouse aim (held left button) + Space / keys, the follow camera, and the commands they send (kCmdTopDown on
// change, kCmdTopDownAbility per press). The model is modules/rts/view/TopDownControls.h; the sim side RtsTopDown.h.
#include "GameClient.h"
#include <cmath>
#include <cstdlib>

namespace aiverse::client {

using platform::Event;
using platform::EventType;
using platform::Key;

bool GameClient::topDownActive() const {
    return sim_ && !spectator_ && topDown_.active() && !planet();
}

bool GameClient::topDownTouchUi() const {
    static const bool forced = [] { const char* v = std::getenv("AIVERSE_TOUCH_CONTROLS"); return v && *v == '1'; }();   // testing: desktop screenshots
    return forced || topDown_.touchUi(platform::isTouchDevice());
}

void GameClient::topDownConfigure() {
    tdMouseHeld_ = tdFireHeld_ = false;
    const TopDown* td = sim_ ? mod().topDown() : nullptr;
    if (!td || !td->active()) { topDown_ = {}; return; }
    topDown_.configure(manifest_, td->rules(), mod().rules());
    if (!topDownActive()) return;
    Entity h = td->heroOf(*sim_, human_);
    auto& g = mod().state(*sim_).grid;
    if (h.valid()) {
        const Vec3 p = sim_->world().get<Transform>(h).pos;
        topDown_.snapCamera(cam_, {float(p.x.toDouble()), 0, float(p.z.toDouble())}, float(g.w), float(g.h));
    }
}

bool GameClient::topDownEvent(const Event& e, bool uiWants) {
    if (!topDownActive()) return false;
    const float W = platform::windowWidth(), H = platform::windowHeight();
    switch (e.type) {
    case EventType::FocusLost:
        topDown_.cancelAll(); tdMouseHeld_ = tdFireHeld_ = false;
        return false;
    case EventType::TouchBegin:
        if (uiWants) return false;
        topDown_.pointerDown(e.touchId, e.x, e.y, W, H, topDownTouchUi());
        return true;   // the world never sees touches: no selection boxes / camera pans under a hero
    case EventType::TouchMove: topDown_.pointerMove(e.touchId, e.x, e.y); return true;
    case EventType::TouchEnd: case EventType::TouchCancel: topDown_.pointerUp(e.touchId, e.x, e.y); return true;
    case EventType::MouseDown:
        if (uiWants) return false;
        if (topDownTouchUi() && topDown_.pointerDown(-100 - e.button, e.x, e.y, W, H, true)) return true;   // desktop clicks on drawn sticks
        if (e.button == 0) tdMouseHeld_ = true;
        if (e.button == 1) tdFireHeld_ = true;
        return e.button != 2;   // middle button still pans
    case EventType::MouseMove:
        topDown_.pointerMove(-100, e.x, e.y);
        return false;
    case EventType::MouseUp:
        if (topDown_.pointerUp(-100 - e.button, e.x, e.y)) return true;
        if (e.button == 0) tdMouseHeld_ = false;
        if (e.button == 1) tdFireHeld_ = false;
        return e.button != 2;
    case EventType::KeyDown:
        if (uiWants || e.repeat || ui_.textInputFocused()) return false;
        if (e.key == Key::Escape) return false;   // pause menu
        for (size_t i = 0; i < topDown_.config().buttons.size(); ++i)
            if (topDown_.config().buttons[i].key != 0 && int(e.key) == topDown_.config().buttons[i].key) { topDown_.pressAbility(int(i)); return true; }
        if (e.key == Key::F || e.key == Key::Enter) {   // get into the nearest vehicle / out of the car (rules "driving")
            if (mod().driving() && mod().driving()->active()) { Command c; c.type = kCmdTopDownEnter; issue(c); }
            return true;
        }
        return true;   // WASD etc. steer the hero, not RTS hotkeys
    case EventType::KeyUp: return !uiWants;
    default: return false;
    }
}

void GameClient::topDownFrame(double) {
    if (!topDownActive()) return;
    TopDown* td = mod().topDown();
    World& w = sim_->world();
    const Entity hero = td->heroOf(*sim_, human_);
    TopDownFrameInput in;
    if (!ui_.textInputFocused()) {
        auto key = [&](Key k) { return keys_[int(k)]; };
        in.up = key(Key::W) || key(Key::Up);
        in.down = key(Key::S) || key(Key::Down);
        in.left = key(Key::A) || key(Key::Left);
        in.right = key(Key::D) || key(Key::Right);
        in.fireKey = key(Key::Space) || tdFireHeld_;
    }
    float3 g;
    const bool wantMouse = topDown_.config().mouseAim == TopDownControlConfig::MouseAim::Always || tdMouseHeld_;
    if (wantMouse && hero.valid() && !ui_.pointerOverUi() && groundAt(mouseX_, mouseY_, g)) {
        const Vec3 p = w.get<Transform>(hero).pos;
        const float dx = g.x - float(p.x.toDouble()), dz = g.z - float(p.z.toDouble());
        if (std::hypot(dx, dz) > 0.25f) { in.mouseAim = true; in.mouseDirX = dx; in.mouseDirZ = dz; }
    }
    const topdown::Input q = topDown_.compose(in, cam_.yaw);
    if (topDown_.changed(q)) {
        Command c; c.type = kCmdTopDown; c.param = topdown::pack(q);
        issue(c);
        topDown_.sent(q);
    }
    for (int b : topDown_.takeAbilityPresses()) {
        const TopDownButtonConfig& bc = topDown_.config().buttons[size_t(b)];
        if (bc.abilityIndex < 0 || !hero.valid()) continue;
        Command c; c.type = kCmdTopDownAbility; c.param = bc.abilityIndex;
        // aimed with the mouse: the cursor point; touch / keys: the sim aims along the stick (no point)
        if (in.mouseAim && groundAt(mouseX_, mouseY_, g)) c.point = {Fixed::fromRatio(int64_t(g.x * 256), 256), Fixed{}, Fixed::fromRatio(int64_t(g.z * 256), 256)};
        issue(c);
    }
}

void GameClient::topDownCamera(double dt) {
    if (!topDownActive()) return;
    const Entity hero = mod().topDown()->heroOf(*sim_, human_);
    float3 pos{};
    bool have = false;
    for (auto& o : scene_.objects) if (o.entity == hero && hero.valid()) { pos = o.pos; have = true; break; }   // interpolated
    auto& g = mod().state(*sim_).grid;
    topDown_.updateCamera(cam_, pos, have, float(dt), float(g.w), float(g.h));
}

void GameClient::topDownDraw() {
    if (!topDownActive() || !topDownTouchUi()) return;
    std::vector<float> ready;
    const Entity hero = mod().topDown()->heroOf(*sim_, human_);
    World& w = sim_->world();
    for (auto& b : topDown_.config().buttons) {
        float r = 1;
        if (hero.valid() && b.abilityIndex >= 0) {
            const UnitType& t = rules_.types[w.get<Unit>(hero).type];
            auto* st = w.tryGet<AbilityState>(hero);
            if (st && size_t(b.abilityIndex) < t.abilities.size() && b.abilityIndex < 8) {
                const float cd = float(t.abilities[size_t(b.abilityIndex)].cooldown.toDouble()) * float(sim_->tickRate());
                const float left = float(int64_t(st->readyAt[b.abilityIndex]) - int64_t(sim_->tick()));
                if (cd > 0 && left > 0) r = 1 - std::min(1.0f, left / cd);
            }
            if (!mod().canCast(*sim_, hero, b.abilityIndex) && r >= 1) r = 0.999f;
        } else r = 0;
        ready.push_back(r);
    }
    view_.drawCanvas(topDown_.draw(platform::windowWidth(), platform::windowHeight(), ready));
}

} // namespace aiverse::client
