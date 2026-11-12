// Top-down hero controls on the client (TopDownControls.h). Floats are fine here: only the quantized integers of
// compose() reach the simulation, inside an ordinary Command.
#include "TopDownControls.h"
#include <algorithm>
#include <cmath>
#include "../RtsData.h"
#include "../../../engine/platform/Platform.h"
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmisleading-indentation"   // the library header's one-line else (not ours to restyle)
#endif
#include "../../../libraries/include/aiverselib/gameplay/Camera.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include "../../../libraries/include/aiverselib/gameplay/Input.h"

namespace aiverse::game {

namespace {
constexpr float kPi = 3.14159265358979f;

int parseKey(const std::string& s) {
    using platform::Key;
    if (s.size() == 1 && s[0] >= 'A' && s[0] <= 'Z') return int(Key::A) + (s[0] - 'A');
    if (s.size() == 1 && s[0] >= 'a' && s[0] <= 'z') return int(Key::A) + (s[0] - 'a');
    if (s.size() == 1 && s[0] >= '0' && s[0] <= '9') return int(Key::Num0) + (s[0] - '0');
    if (s == "Space") return int(Key::Space);
    if (s == "Shift") return int(Key::LeftShift);
    if (s == "Ctrl") return int(Key::LeftCtrl);
    if (s == "Tab") return int(Key::Tab);
    if (s == "Enter") return int(Key::Enter);
    return 0;
}
void readPair(const Json& j, float* out) {
    if (j.isArray() && j.items().size() >= 2) { out[0] = float(j[0].toDouble(out[0])); out[1] = float(j[1].toDouble(out[1])); }
}
TopDownStickConfig readStick(const Json& j, TopDownStickConfig c) {
    if (!j.isObject()) { if (!j.isNull()) c.enabled = j.boolean(c.enabled); return c; }   // "aimStick": true
    c.enabled = j.getBool("enabled", c.enabled);
    c.floating = j.getStr("mode", c.floating ? "floating" : "fixed") != "fixed";
    c.radius = std::max(8.0f, float(j["radius"].toDouble(c.radius)));
    c.deadzone = std::clamp(float(j["deadzone"].toDouble(c.deadzone)), 0.0f, 0.9f);
    if (j["area"].isArray() && j["area"].items().size() == 4)
        for (int i = 0; i < 4; ++i) c.area[i] = std::clamp(float(j["area"][size_t(i)].toDouble(c.area[i])), 0.0f, 1.0f);
    readPair(j["center"], c.center);
    return c;
}
// Screen vector (x right, y down) -> world ground direction for a camera with this yaw (RTSCamera: the camera
// looks along -(sin yaw, cos yaw); screen up is that direction on the ground).
void screenToWorld(float sx, float sy, float yaw, float& wx, float& wz) {
    const float s = std::sin(yaw), c = std::cos(yaw);
    const float fwdX = -s, fwdZ = -c, rightX = c, rightZ = -s;
    wx = rightX * sx - fwdX * sy;
    wz = rightZ * sx - fwdZ * sy;
}
int32_t binaryAngle(float wx, float wz) {   // 0 = +z, a quarter turn = +x (RtsTopDown.h)
    float t = std::atan2(wx, wz) / (2 * kPi);
    if (t < 0) t += 1;
    return int32_t(std::lround(t * 65536.0f)) & 0xFFFF;
}
uint32_t rgba(int r, int g, int b, int a) { return uint32_t(r) << 24 | uint32_t(g) << 16 | uint32_t(b) << 8 | uint32_t(a); }
void disc(ViewDrawBatch& b, float cx, float cy, float r, uint32_t col, float from = 0, float to = 1) {
    const int n = 40;
    const int k0 = int(std::floor(from * n)), k1 = int(std::ceil(to * n));
    for (int k = k0; k < k1; ++k) {
        float a0 = std::max(from, float(k) / n) * 2 * kPi - kPi / 2, a1 = std::min(to, float(k + 1) / n) * 2 * kPi - kPi / 2;
        b.verts.push_back({cx, cy, 0, 0, col});
        b.verts.push_back({cx + std::cos(a0) * r, cy + std::sin(a0) * r, 0, 0, col});
        b.verts.push_back({cx + std::cos(a1) * r, cy + std::sin(a1) * r, 0, 0, col});
    }
}
void ring(ViewDrawBatch& b, float cx, float cy, float r, float w, uint32_t col) {
    const int n = 48;
    for (int k = 0; k < n; ++k) {
        float a0 = float(k) / n * 2 * kPi, a1 = float(k + 1) / n * 2 * kPi;
        float ix0 = cx + std::cos(a0) * (r - w), iy0 = cy + std::sin(a0) * (r - w), ox0 = cx + std::cos(a0) * r, oy0 = cy + std::sin(a0) * r;
        float ix1 = cx + std::cos(a1) * (r - w), iy1 = cy + std::sin(a1) * (r - w), ox1 = cx + std::cos(a1) * r, oy1 = cy + std::sin(a1) * r;
        b.verts.push_back({ix0, iy0, 0, 0, col}); b.verts.push_back({ox0, oy0, 0, 0, col}); b.verts.push_back({ox1, oy1, 0, 0, col});
        b.verts.push_back({ix0, iy0, 0, 0, col}); b.verts.push_back({ox1, oy1, 0, 0, col}); b.verts.push_back({ix1, iy1, 0, 0, col});
    }
}
} // namespace

// ================================================================= config

TopDownControlConfig parseTopDownControls(const Json& g, const std::vector<std::string>& heroAbilities) {
    TopDownControlConfig c;
    const Json& j = g["topDown"];
    if (!j.isObject()) return c;
    c.enabled = j.getBool("enabled", true);
    const std::string touch = j.getStr("touchControls", "auto");
    c.touch = touch == "always" ? TopDownControlConfig::Touch::Always : touch == "never" ? TopDownControlConfig::Touch::Never : TopDownControlConfig::Touch::Auto;
    const std::string ma = j.getStr("mouseAim", "hold");
    c.mouseAim = ma == "always" ? TopDownControlConfig::MouseAim::Always : ma == "off" ? TopDownControlConfig::MouseAim::Off : TopDownControlConfig::MouseAim::Hold;
    c.move = readStick(j["joystick"], c.move);
    TopDownStickConfig aimDef;
    aimDef.enabled = false;
    aimDef.area[0] = 0.5f; aimDef.area[2] = 1.0f;
    aimDef.center[0] = 0.86f;
    c.aim = readStick(j["aimStick"], aimDef);
    c.padDeadzone = std::clamp(float(j["gamepad"]["deadzone"].toDouble(c.padDeadzone)), 0.0f, 0.9f);
    for (auto& bj : j["buttons"].items()) {
        TopDownButtonConfig b;
        b.ability = bj.getStr("ability");
        for (size_t i = 0; i < heroAbilities.size(); ++i) if (heroAbilities[i] == b.ability) b.abilityIndex = int(i);
        b.key = parseKey(bj.getStr("key"));
        b.padButton = int(bj.getInt("gamepad", -1));
        readPair(bj["anchor"], b.anchor);
        readPair(bj["offset"], b.offset);
        b.radius = std::max(12.0f, float(bj["radius"].toDouble(b.radius)));
        c.buttons.push_back(b);
    }
    const Json& cj = j["camera"];
    auto num = [&](const char* k, float& v) { v = float(cj[k].toDouble(v)); };
    num("pitch", c.camera.pitch); num("yaw", c.camera.yaw); num("distance", c.camera.distance);
    num("minDistance", c.camera.minDistance); num("maxDistance", c.camera.maxDistance);
    num("smoothing", c.camera.smoothing); num("lookAhead", c.camera.lookAhead); num("aimLookAhead", c.camera.aimLookAhead);
    num("lookAheadSmoothing", c.camera.lookAheadSmoothing); num("margin", c.camera.margin);
    c.camera.lockOffsetIfNoInput = cj.getBool("lockOffsetIfNoInput", c.camera.lockOffsetIfNoInput);
    c.camera.bounds = cj.getBool("bounds", c.camera.bounds);
    c.camera.pitch = std::clamp(c.camera.pitch, 5.0f, 90.0f);
    c.camera.minDistance = std::max(1.0f, std::min(c.camera.minDistance, c.camera.distance));
    c.camera.maxDistance = std::max(c.camera.maxDistance, c.camera.distance);
    return c;
}

void TopDownControls::configure(const Json& gameJson, const TopDownRules& rules, const Rules& rts) {
    rules_ = rules;
    std::vector<std::string> abilities;
    if (rules.enabled && !rules.heroTypes.empty() && rules.heroTypes[0] < rts.types.size())
        for (auto& a : rts.types[rules.heroTypes[0]].abilities) abilities.push_back(a.id);
    cfg_ = parseTopDownControls(gameJson, abilities);
    move_.configure(cfg_.move);
    aim_.configure(cfg_.aim);
    buttonPointers_.assign(cfg_.buttons.size(), -1);
    presses_.clear();
    haveSent_ = false;
    camInit_ = false;
    offX_ = offZ_ = 0;
}

// ================================================================= sticks

bool VirtualStick::begin(int64_t id, float x, float y, float W, float H) {
    if (!cfg_.enabled || active_ || W <= 0 || H <= 0) return false;
    if (cfg_.floating) {
        if (x < cfg_.area[0] * W || x > cfg_.area[2] * W || y < cfg_.area[1] * H || y > cfg_.area[3] * H) return false;
        ox_ = x; oy_ = y;
    } else {
        ox_ = cfg_.center[0] * W; oy_ = cfg_.center[1] * H;
        if (std::hypot(x - ox_, y - oy_) > cfg_.radius * 1.6f) return false;
    }
    active_ = true;
    pointer_ = id;
    vx_ = x - ox_; vy_ = y - oy_;
    return true;
}

bool VirtualStick::move(int64_t id, float x, float y) {
    if (!active_ || id != pointer_) return false;
    vx_ = x - ox_; vy_ = y - oy_;
    const float len = std::hypot(vx_, vy_);
    if (cfg_.floating && len > cfg_.radius * 1.5f) {   // a floating base follows a finger that runs away
        const float k = (len - cfg_.radius) / len;
        ox_ += vx_ * k; oy_ += vy_ * k;
        vx_ = x - ox_; vy_ = y - oy_;
    }
    return true;
}

bool VirtualStick::end(int64_t id) {
    if (!active_ || id != pointer_) return false;
    cancel();
    return true;
}

void VirtualStick::value(float& x, float& y) const {
    x = y = 0;
    if (!active_) return;
    const float len = std::hypot(vx_, vy_);
    if (len <= 0) return;
    // per-mille dead zone of the shared gameplay input helpers, applied to the length
    const int64_t m = lib::gameplay::deadzone(int64_t(std::min(1.0f, len / cfg_.radius) * 1000.0f), int64_t(cfg_.deadzone * 1000.0f));
    const float k = float(m) / 1000.0f / len;
    x = vx_ * k; y = vy_ * k;
}

void VirtualStick::geometry(float W, float H, float& bx, float& by, float& kx, float& ky) const {
    if (active_) { bx = ox_; by = oy_; }
    else { bx = cfg_.center[0] * W; by = cfg_.center[1] * H; }
    float dx = active_ ? vx_ : 0, dy = active_ ? vy_ : 0;
    const float len = std::hypot(dx, dy);
    if (len > cfg_.radius) { dx *= cfg_.radius / len; dy *= cfg_.radius / len; }
    kx = bx + dx; ky = by + dy;
}

// ================================================================= pointers

void TopDownControls::buttonCenter(size_t i, float W, float H, float& x, float& y) const {
    const auto& b = cfg_.buttons[i];
    x = b.anchor[0] * W + b.offset[0];
    y = b.anchor[1] * H + b.offset[1];
}

int TopDownControls::buttonAt(float x, float y, float W, float H) const {
    for (size_t i = 0; i < cfg_.buttons.size(); ++i) {
        float bx, by;
        buttonCenter(i, W, H, bx, by);
        if (std::hypot(x - bx, y - by) <= cfg_.buttons[i].radius) return int(i);
    }
    return -1;
}

bool TopDownControls::pointerDown(int64_t id, float x, float y, float W, float H, bool touchUi) {
    if (!active() || !touchUi) return false;
    if (int b = buttonAt(x, y, W, H); b >= 0) {   // buttons first: they sit inside the aim stick's area
        buttonPointers_[size_t(b)] = id;
        pressAbility(b);
        return true;
    }
    if (move_.begin(id, x, y, W, H)) return true;
    return aim_.begin(id, x, y, W, H);
}

bool TopDownControls::pointerMove(int64_t id, float x, float y) {
    if (!active()) return false;
    bool used = move_.move(id, x, y) || aim_.move(id, x, y);
    for (auto p : buttonPointers_) if (p == id) used = true;
    return used;
}

bool TopDownControls::pointerUp(int64_t id, float, float) {
    if (!active()) return false;
    bool used = move_.end(id) || aim_.end(id);
    for (auto& p : buttonPointers_) if (p == id) { p = -1; used = true; }
    return used;
}

void TopDownControls::cancelAll() {
    move_.cancel();
    aim_.cancel();
    std::fill(buttonPointers_.begin(), buttonPointers_.end(), -1);
}

// ================================================================= input

topdown::Input TopDownControls::compose(const TopDownFrameInput& in, float yaw) {
    // move: the touch stick, else the pad stick, else the keys (screen axes, +y down)
    float sx = 0, sy = 0;
    move_.value(sx, sy);
    if (sx == 0 && sy == 0) {
        const int64_t dz = int64_t(cfg_.padDeadzone * 1000.0f);
        const float plen = std::hypot(in.padMoveX, in.padMoveY);
        if (plen > 0) {
            const float m = float(lib::gameplay::deadzone(int64_t(std::min(1.0f, plen) * 1000.0f), dz)) / 1000.0f;
            sx = in.padMoveX / plen * m; sy = in.padMoveY / plen * m;
        }
    }
    if (sx == 0 && sy == 0) {
        sx = float(in.right) - float(in.left);
        sy = float(in.down) - float(in.up);
        const float l = std::hypot(sx, sy);
        if (l > 0) { sx /= l; sy /= l; }
    }
    topdown::Input q;
    const float mag = std::min(1.0f, std::hypot(sx, sy));
    float wx = 0, wz = 0;
    screenToWorld(sx, sy, yaw, wx, wz);
    q.moveAngle = binaryAngle(wx, wz);
    q.moveMag = int32_t(std::lround(mag * 1000.0f));
    const float wl = std::hypot(wx, wz);
    moveDirX_ = wl > 0 ? wx / wl * mag : 0; moveDirZ_ = wl > 0 ? wz / wl * mag : 0;

    // aim: the aim stick, else the pad's right stick, else the cursor
    float ax = 0, ay = 0, awx = 0, awz = 0;
    bool aim = false;
    aim_.value(ax, ay);
    if (ax == 0 && ay == 0) {
        const float alen = std::hypot(in.padAimX, in.padAimY);
        if (alen > cfg_.padDeadzone) { ax = in.padAimX; ay = in.padAimY; }
    }
    if (ax != 0 || ay != 0) { screenToWorld(ax, ay, yaw, awx, awz); aim = true; }
    else if (in.mouseAim && cfg_.mouseAim != TopDownControlConfig::MouseAim::Off && (in.mouseDirX != 0 || in.mouseDirZ != 0)) {
        awx = in.mouseDirX; awz = in.mouseDirZ; aim = true;
    }
    q.aim = aim;
    if (aim) q.aimAngle = binaryAngle(awx, awz);
    const float al = std::hypot(awx, awz);
    aiming_ = aim;
    aimDirX_ = al > 0 ? awx / al : 0; aimDirZ_ = al > 0 ? awz / al : 0;
    q.fire = in.fireKey || in.padFire;
    return TopDown::requantize(rules_, q);
}

// ================================================================= camera

void TopDownControls::snapCamera(render::RTSCamera& cam, render::float3 hero, float mapW, float mapH) {
    camInit_ = false;
    updateCamera(cam, hero, true, 0, mapW, mapH);
}

void TopDownControls::updateCamera(render::RTSCamera& cam, render::float3 hero, bool haveHero, float dt, float mapW, float mapH) {
    const TopDownCameraConfig& c = cfg_.camera;
    namespace gc = lib::gameplay::camera;
    gc::Limits lim;
    if (c.bounds && mapW > 0 && mapH > 0) {
        lim.minX = std::min(c.margin, mapW * 0.5f); lim.maxX = std::max(mapW - c.margin, mapW * 0.5f);
        lim.minZ = std::min(c.margin, mapH * 0.5f); lim.maxZ = std::max(mapH - c.margin, mapH * 0.5f);
    }
    lim.minZoom = c.minDistance; lim.maxZoom = c.maxDistance;
    lim.minPitch = 5; lim.maxPitch = 90;
    // look-ahead: eases toward the stick (and the aim); a released stick keeps the offset (lockOffsetIfNoInput)
    const bool input = moveDirX_ != 0 || moveDirZ_ != 0 || aiming_;
    if (input || !c.lockOffsetIfNoInput) {
        const float tx = moveDirX_ * c.lookAhead + (aiming_ ? aimDirX_ * c.aimLookAhead : 0);
        const float tz = moveDirZ_ * c.lookAhead + (aiming_ ? aimDirZ_ * c.aimLookAhead : 0);
        const float t = c.lookAheadSmoothing > 0 ? 1 - std::exp(-c.lookAheadSmoothing * std::max(0.0f, dt)) : 1;
        offX_ += (tx - offX_) * t; offZ_ += (tz - offZ_) * t;
    }
    gc::Pose current{cam.target.x, cam.target.z, cam.distance, cam.yaw * 180 / kPi, cam.pitch * 180 / kPi};
    gc::Pose target = current;
    if (haveHero) { target.x = hero.x + offX_; target.z = hero.z + offZ_; }
    target.yaw = c.yaw; target.pitch = c.pitch;
    target.zoom = camInit_ ? std::clamp(cam.distance, c.minDistance, c.maxDistance) : c.distance;
    if (!camInit_ || c.smoothing <= 0) { gc::constrain(target, lim); current = target; camInit_ = haveHero; }
    else if (!gc::advance(current, target, lim, std::max(0.0f, dt), c.smoothing)) { gc::constrain(target, lim); current = target; }
    cam.target = {current.x, 0, current.z};
    cam.distance = current.zoom;
    cam.minDistance = c.minDistance; cam.maxDistance = c.maxDistance;
    cam.yaw = current.yaw * kPi / 180;
    cam.pitch = current.pitch * kPi / 180;
}

// ================================================================= drawing

std::vector<ViewDrawBatch> TopDownControls::draw(float W, float H, const std::vector<float>& ready) const {
    std::vector<ViewDrawBatch> out(1);
    ViewDrawBatch& b = out[0];
    b.layer = 100;
    auto stick = [&](const VirtualStick& s) {
        if (!s.config().enabled) return;
        float bx, by, kx, ky;
        s.geometry(W, H, bx, by, kx, ky);
        const float r = s.config().radius;
        disc(b, bx, by, r, rgba(16, 20, 28, s.active() ? 110 : 60));
        ring(b, bx, by, r, 3, rgba(235, 240, 255, s.active() ? 200 : 110));
        disc(b, kx, ky, r * 0.42f, rgba(235, 240, 255, s.active() ? 230 : 140));
    };
    stick(move_);
    stick(aim_);
    for (size_t i = 0; i < cfg_.buttons.size(); ++i) {
        float x, y;
        buttonCenter(i, W, H, x, y);
        const float r = cfg_.buttons[i].radius;
        const float charge = i < ready.size() ? std::clamp(ready[i], 0.0f, 1.0f) : 1.0f;
        const bool held = buttonPointers_[i] >= 0;
        disc(b, x, y, r, rgba(16, 20, 28, 150));
        if (charge < 1) disc(b, x, y, r * 0.86f, rgba(120, 190, 255, 120), 0, charge);   // recharging: a pie
        else disc(b, x, y, r * 0.86f, rgba(120, 200, 255, held ? 250 : 200));
        ring(b, x, y, r, 3, rgba(235, 240, 255, 200));
    }
    return out;
}

} // namespace aiverse::game
