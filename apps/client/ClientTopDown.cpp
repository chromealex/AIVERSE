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
    tdHooked_ = nullptr;
    tdInput_ = {};
    const TopDown* td = sim_ ? mod().topDown() : nullptr;
    if (!td || !td->active()) { topDown_ = {}; tdPredict_.reset(); return; }
    topDown_.configure(manifest_, td->rules(), mod().rules());
    {   // game.json topDown.prediction (render only)
        const Json& pj = manifest_["topDown"]["prediction"];
        TopDownPredictionConfig pc;
        if (pj.isObject()) {
            pc.enabled = pj.getBool("enabled", true);
            pc.maxLead = float(pj["maxLead"].toDouble(pc.maxLead));
            pc.maxOffset = float(pj["maxOffset"].toDouble(pc.maxOffset));
            pc.easeHalfLifeMs = float(pj["easeHalfLifeMs"].toDouble(pc.easeHalfLifeMs));
            pc.correctionHalfLifeMs = float(pj["correctionHalfLifeMs"].toDouble(pc.correctionHalfLifeMs));
        } else if (!pj.isNull()) pc.enabled = pj.boolean(true);
        tdPredict_.configure(pc);
    }
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
    case EventType::FocusLost:   // held buttons cancel; the escape preview stops (Drone Fall: losing focus stops it)
        topDown_.cancelAll(); topDown_.escape().cancel(); tdMouseHeld_ = tdFireHeld_ = false;
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
            if (topDown_.config().buttons[i].key != 0 && int(e.key) == topDown_.config().buttons[i].key) { topDown_.buttonDown(int(i)); return true; }
        if (commerceKey(e.key)) return true;   // deals at a door (rules "commerce"): E, then 1..9
        if (mod().arsenal() && mod().arsenal()->active()) {   // weapons (rules "arsenal"): Z / X cycle, 1..9 pick a slot
            int64_t pick = 0;
            if (e.key == Key::X || e.key == Key::Tab) pick = kWeaponNext;
            else if (e.key == Key::Z) pick = kWeaponPrev;
            else if (int(e.key) >= int(Key::Num1) && int(e.key) <= int(Key::Num9)) pick = kWeaponSlot + (int(e.key) - int(Key::Num1));
            if (pick != 0) { Command c; c.type = kCmdTopDownWeapon; c.param = pick; issue(c); return true; }
        }
        if (e.key == Key::F || e.key == Key::Enter) {   // get into the nearest vehicle / out of the car (rules "driving")
            if (mod().driving() && mod().driving()->active()) { Command c; c.type = kCmdTopDownEnter; issue(c); }
            return true;
        }
        return true;   // WASD etc. steer the hero, not RTS hotkeys
    case EventType::KeyUp:
        for (size_t i = 0; i < topDown_.config().buttons.size(); ++i)
            if (topDown_.config().buttons[i].key != 0 && int(e.key) == topDown_.config().buttons[i].key) topDown_.buttonUp(int(i));
        return !uiWants;
    default: return false;
    }
}

void GameClient::topDownFrame(double dt) {
    if (!topDownActive()) return;
    TopDown* td = mod().topDown();
    World& w = sim_->world();
    const Entity hero = td->heroOf(*sim_, human_);
    TopDownFrameInput in;
    tdClock_ += float(std::max(0.0, dt));
    in.time = tdClock_;
    // aim assist (game.json topDown "aimAssist"): the targets in reach, only for the sources it is on for
    const TopDownAimAssistConfig& assist = topDown_.config().assist;
    tdTargets_.clear();
    if (hero.valid() && ((assist.touch && topDownTouchUi()) || assist.pad || assist.mouse)) {
        tdTargets_ = td->aimCandidates(*sim_, human_, Fixed::fromRatio(int64_t(assist.range * 256), 256));
        const Vec3 hp = w.get<Transform>(hero).pos;
        for (auto& t : tdTargets_)
            in.targets.push_back({t.e.packed(), float((t.pos.x - hp.x).toDouble()), float((t.pos.z - hp.z).toDouble())});
    }
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
    topdown::Input q = topDown_.compose(in, cam_.yaw);
    auto toFx = [](float v) { return Fixed::fromRatio(int64_t(std::lround(double(v) * 256.0)), 256); };
    TopDownEscapePreview& esc = topDown_.escape();
    if (esc.active) {   // escape preview: the stick walks the ghost; the hero stands, nothing fires
        q = topdown::Input{};
        float mx, mz;
        topDown_.worldMove(mx, mz);
        if (esc.update(float(dt), mx, mz, [&](float x, float z) { return topDownPassable(hero, x, z); })) {
            topdown::Ability a; a.index = topdown::kEscapeAbility; a.hasPoint = true;
            Command c; c.type = kCmdTopDownAbility; c.param = topdown::abilityParam(a);
            c.point = {toFx(esc.x), Fixed{}, toFx(esc.z)};
            issue(c);
        }
    }
    tdInput_ = q;
    if (topDown_.changed(q)) {
        Command c; c.type = kCmdTopDown; c.param = topdown::pack(q);
        issue(c);
        topDown_.sent(q);
    }
    for (const TopDownButtonEvent& ev : topDown_.takeButtonEvents()) {
        const TopDownButtonConfig& bc = topDown_.config().buttons[size_t(ev.button)];
        if (bc.weaponStep != 0) {   // weapon switch buttons (rules "arsenal")
            const bool press = ev.phase == topdown::Phase::Tap || ev.phase == topdown::Phase::Press;
            if (press && mod().arsenal() && mod().arsenal()->active()) { Command c; c.type = kCmdTopDownWeapon; c.param = bc.weaponStep > 0 ? kWeaponNext : kWeaponPrev; issue(c); }
            continue;
        }
        if (bc.abilityIndex < 0 || !hero.valid()) continue;
        if (bc.abilityIndex == topdown::kEscapeAbility) {   // starts the preview; the destination goes out when it ends
            if (ev.phase != topdown::Phase::Tap && ev.phase != topdown::Phase::Press) continue;
            if (esc.active || td->escapeBudget(*sim_, human_).raw <= 0 || sim_->tick() < td->escapeReadyAt(*sim_, human_)) continue;
            const Vec3 p = w.get<Transform>(hero).pos;
            esc.begin(float(p.x.toDouble()), float(p.z.toDouble()), float(mod().stat(*sim_, hero, Stat::Speed).toDouble()),
                      float(td->rules().escape.duration.toDouble()));
            continue;
        }
        if (esc.active) continue;   // Drone Fall: combat actions wait for the escape
        topdown::Ability a = TopDownControls::abilityEvent(ev, bc.abilityIndex, cam_.yaw);
        Command c; c.type = kCmdTopDownAbility;
        // aimed with the mouse (no drag): the cursor point; touch / keys without a drag: the sim aims along the stick
        if (!a.hasAngle && in.mouseAim && ev.phase != topdown::Phase::Cancel && groundAt(mouseX_, mouseY_, g)) {
            a.hasPoint = true;
            c.point = {toFx(g.x), Fixed{}, toFx(g.z)};
        }
        c.param = topdown::abilityParam(a);
        issue(c);
    }
}

bool GameClient::topDownPassable(Entity hero, float x, float z) {
    auto& g = mod().state(*sim_).grid;
    uint8_t layer = kLayerGround;
    if (hero.valid() && sim_->world().alive(hero)) {
        const UnitType& t = rules_.types[sim_->world().get<Unit>(hero).type];
        if ((t.layer & kLayerAir) && !g.airSolid) return x >= 0 && z >= 0 && x < float(g.w) && z < float(g.h);
        layer = t.pathLayer;
    }
    const int cx = g.cellX(Fixed::fromRatio(int64_t(x * 256), 256)), cy = g.cellY(Fixed::fromRatio(int64_t(z * 256), 256));
    return g.inside(cx, cy) && g.passable(cx, cy, layer);
}

bool GameClient::topDownPaused() const {
    if (!topDownActive() || netMatch_ || !topDown_.escape().active) return false;
    const TopDown* td = const_cast<GameClient*>(this)->mod().topDown();
    return td && td->rules().escape.pause;
}

TopDownPrediction::Positions GameClient::topDownPositions() {
    TopDownPrediction::Positions out;
    TopDown* td = mod().topDown();
    World& w = sim_->world();
    for (auto& p : sim_->players()) {
        if (Entity h = td->heroOf(*sim_, p.slot); h.valid()) {
            const Vec3 v = w.get<Transform>(h).pos;
            out[h.packed()] = {float(v.x.toDouble()), float(v.z.toDouble())};
        }
        for (Entity m : td->squadOf(*sim_, p.slot)) {
            const Vec3 v = w.get<Transform>(m).pos;
            out[m.packed()] = {float(v.x.toDouble()), float(v.z.toDouble())};
        }
    }
    return out;
}

// Render only: the local hero is drawn where its in-flight input takes it (TopDownPrediction.h); every hero and
// squad member blends rollback corrections in instead of jumping.
void GameClient::topDownPresent(double dt, float alpha) {
    if (!topDownActive()) return;
    TopDown* td = mod().topDown();
    if (netMatch_ && net_ && net_->session() && tdHooked_ != net_->session()) {
        tdHooked_ = net_->session();
        tdHooked_->setRollbackHook([this](bool before) {
            if (!sim_ || !topDownActive()) return;
            if (before) tdPredict_.beforeRollback(topDownPositions());
            else tdPredict_.afterRollback(topDownPositions());
        });
    }
    World& w = sim_->world();
    const Entity hero = td->heroOf(*sim_, human_);
    float wx = 0, wz = 0, hx = 0, hz = 0;
    bool have = false;
    for (auto& o : scene_.objects) if (hero.valid() && o.entity == hero) { hx = o.pos.x; hz = o.pos.z; have = true; break; }
    if (have && tdInput_.moveMag > 0 && !mod().isStunned(*sim_, hero)) {
        const float speed = float(mod().stat(*sim_, hero, Stat::Speed).toDouble()) * float(tdInput_.moveMag) / 1000.0f;
        const Vec2 d = topdown::directionXZ(tdInput_.moveAngle);
        wx = float(d.x.toDouble()) * speed; wz = float(d.y.toDouble()) * speed;
    }
    // input issued now executes inputDelay ticks later; the drawn state lags the newest tick by (1 - alpha) ticks
    const float lead = (float(sim_->config().inputDelayTicks) + 1.0f - alpha) / float(std::max<uint32_t>(1, sim_->tickRate()));
    tdPredict_.frame(float(dt), wx, wz, lead, have && !topDownPaused(), hx, hz, [&](float x, float z) { return topDownPassable(hero, x, z); });
    for (auto& o : scene_.objects) {
        if (!o.entity.valid() || !w.alive(o.entity)) continue;
        const bool local = o.entity == hero;
        const Unit* ou = w.tryGet<Unit>(o.entity);   // scene objects also hold projectiles / pickups
        if (!local && !(ou && (ou->flags & kUnitPossessed)) && !(ou && td->isFollower(*sim_, o.entity))) continue;
        float dx, dz;
        tdPredict_.offset(o.entity.packed(), local, dx, dz);
        o.pos.x += dx; o.pos.z += dz;
    }
    // escape preview: the ghost point (a move marker) and the camera follow it
    if (topDown_.escape().active) scene_.moveMarkers.push_back({topDown_.escape().x, 0, topDown_.escape().z});
}

void GameClient::topDownCamera(double dt) {
    if (!topDownActive()) return;
    const Entity hero = mod().topDown()->heroOf(*sim_, human_);
    float3 pos{};
    bool have = false;
    // at the wheel the hero is off the map (not in the scene): the camera follows his car
    Entity follow = hero;
    const game::DrivingCar* car = mod().driving() && mod().driving()->active() && hero.valid() ? mod().driving()->carOf(*sim_, hero) : nullptr;
    if (car) follow = car->e;
    for (auto& o : scene_.objects) if (o.entity == follow && follow.valid()) { pos = o.pos; have = true; break; }   // interpolated + predicted
    if (!have && follow.valid() && sim_->world().alive(follow)) {   // not drawn this frame: the simulation position
        const Vec3 p = sim_->world().get<Transform>(follow).pos;
        pos = {float(p.x.toDouble()), 0, float(p.z.toDouble())}; have = true;
    }
    if (topDown_.escape().active) { pos = {topDown_.escape().x, pos.y, topDown_.escape().z}; have = true; }   // escape preview
    auto& g = mod().state(*sim_).grid;
    // the stick is pedals + steering there, not a direction: only the velocity look-ahead applies
    topDown_.setStickLookAhead(!car);
    topDown_.updateCamera(cam_, pos, have, float(dt), float(g.w), float(g.h));
}

void GameClient::topDownDraw() {
    if (!topDownActive()) return;
    const bool touchUi = topDownTouchUi();
    std::vector<float> ready;
    const Entity hero = mod().topDown()->heroOf(*sim_, human_);
    World& w = sim_->world();
    for (auto& b : topDown_.config().buttons) {
        float r = 1;
        if (b.weaponStep != 0) r = 1;
        else if (hero.valid() && b.abilityIndex >= 0) {
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
    std::vector<ViewDrawBatch> batches;
    if (touchUi) batches = topDown_.draw(platform::windowWidth(), platform::windowHeight(), ready);
    if (const uint64_t lock = topDown_.assistTarget()) {   // aim assist: a lock marker on the target
        for (auto& o : scene_.objects) {
            if (!o.entity.valid() || o.entity.packed() != lock) continue;
            float2 sp;
            if (cam_.worldToScreen({o.pos.x, 0.3f, o.pos.z}, platform::windowWidth(), platform::windowHeight(), sp))
                batches.push_back(TopDownControls::drawLock(sp.x, sp.y, 20, tdClock_));
            break;
        }
    }
    view_.drawCanvas(batches);
}

} // namespace aiverse::client
