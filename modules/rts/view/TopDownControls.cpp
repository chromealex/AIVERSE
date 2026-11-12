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
    const Json& aj = j["aimAssist"];
    if (aj.isObject()) {
        c.assist.touch = aj.getBool("touch", true);
        c.assist.pad = aj.getBool("pad", false);
        c.assist.mouse = aj.getBool("mouse", false);
        c.assist.cone = std::clamp(float(aj["cone"].toDouble(c.assist.cone)), 1.0f, 90.0f);
        c.assist.range = std::max(0.0f, float(aj["range"].toDouble(c.assist.range)));
        c.assist.stickiness = std::clamp(float(aj["stickiness"].toDouble(c.assist.stickiness)), 1.0f, 3.0f);
        c.assist.tapFire = aj.getBool("tapFire", c.assist.tapFire);
        c.assist.tapTime = std::clamp(float(aj["tapTime"].toDouble(c.assist.tapTime)), 0.05f, 1.0f);
        c.assist.tapFireTime = std::clamp(float(aj["tapFireTime"].toDouble(c.assist.tapFireTime)), 0.05f, 3.0f);
    } else if (!aj.isNull() && aj.boolean(false)) {
        c.assist.touch = c.assist.pad = true;
    }
    for (auto& bj : j["buttons"].items()) {
        TopDownButtonConfig b;
        b.ability = bj.getStr("ability");
        for (size_t i = 0; i < heroAbilities.size(); ++i) if (heroAbilities[i] == b.ability) b.abilityIndex = int(i);
        if (const std::string ws = bj.getStr("weapon"); ws == "next") b.weaponStep = 1; else if (ws == "prev") b.weaponStep = -1;
        if (b.ability == "escape" && b.abilityIndex < 0) b.abilityIndex = topdown::kEscapeAbility;
        const std::string mode = bj.getStr("mode", "tap");
        b.mode = mode == "aim" ? TopDownButtonConfig::Mode::Aim : mode == "hold" ? TopDownButtonConfig::Mode::Hold : TopDownButtonConfig::Mode::Tap;
        b.dragRadius = std::max(16.0f, float(bj["dragRadius"].toDouble(b.dragRadius)));
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
    num("velocityLookAhead", c.camera.velocityLookAhead); num("maxLookAhead", c.camera.maxLookAhead);
    if (const Json& sz = cj["speedZoom"]; sz.isObject()) {
        c.camera.speedFrom = std::max(0.0f, float(sz["from"].toDouble(0)));
        c.camera.speedTo = float(sz["to"].toDouble(0));
        c.camera.speedDistance = float(sz["distance"].toDouble(c.camera.distance));
        c.camera.speedSmoothing = std::max(0.0f, float(sz["smoothing"].toDouble(c.camera.speedSmoothing)));
    }
    c.camera.velocityLookAhead = std::max(0.0f, c.camera.velocityLookAhead);
    c.camera.maxLookAhead = std::max(0.0f, c.camera.maxLookAhead);
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
    dragX_.assign(cfg_.buttons.size(), 0); dragY_ = downX_ = downY_ = dragX_;
    keyHeld_.assign(cfg_.buttons.size(), 0);
    events_.clear();
    escape_ = {};
    haveSent_ = false;
    camInit_ = false;
    haveLast_ = false; stickLook_ = true;
    velX_ = velZ_ = speed_ = vOffX_ = vOffZ_ = zoomAdd_ = 0;
    tapPending_ = false; tapUntil_ = 0; lockId_ = tapId_ = 0;
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

float VirtualStick::rawDeflection() const {
    return active_ && cfg_.radius > 0 ? std::hypot(vx_, vy_) / cfg_.radius : 0.0f;
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
        if (buttonPointers_[size_t(b)] >= 0) return true;   // a second finger on a held button
        buttonPointers_[size_t(b)] = id;
        downX_[size_t(b)] = dragX_[size_t(b)] = x; downY_[size_t(b)] = dragY_[size_t(b)] = y;
        buttonDown(b);
        return true;
    }
    if (move_.begin(id, x, y, W, H)) return true;
    if (!aim_.begin(id, x, y, W, H)) return false;
    aimDownAt_ = clock_;
    aimMaxDefl_ = aim_.rawDeflection();
    return true;
}

bool TopDownControls::pointerMove(int64_t id, float x, float y) {
    if (!active()) return false;
    bool used = move_.move(id, x, y);
    if (!used && aim_.move(id, x, y)) { used = true; aimMaxDefl_ = std::max(aimMaxDefl_, aim_.rawDeflection()); }
    for (size_t i = 0; i < buttonPointers_.size(); ++i)
        if (buttonPointers_[i] == id) { dragX_[i] = x; dragY_[i] = y; used = true; }
    return used;
}

bool TopDownControls::pointerUp(int64_t id, float x, float y) {
    if (!active()) return false;
    bool used = move_.end(id);
    if (!used && aim_.end(id)) {
        used = true;
        // a short tap that barely moved the stick: shoot at the nearest target (aim assist)
        if (cfg_.assist.touch && cfg_.assist.tapFire && clock_ - aimDownAt_ <= cfg_.assist.tapTime && aimMaxDefl_ < 0.35f) tapPending_ = true;
    }
    for (size_t i = 0; i < buttonPointers_.size(); ++i)
        if (buttonPointers_[i] == id) {
            dragX_[i] = x; dragY_[i] = y;
            buttonUp(int(i));
            buttonPointers_[i] = -1;
            used = true;
        }
    return used;
}

void TopDownControls::cancelAll() {
    move_.cancel();
    aim_.cancel();
    for (size_t i = 0; i < buttonPointers_.size(); ++i)   // held aim / hold buttons: nothing is cast
        if ((buttonPointers_[i] >= 0 || keyHeld_[i]) && cfg_.buttons[i].mode != TopDownButtonConfig::Mode::Tap)
            events_.push_back({int(i), topdown::Phase::Cancel, false, 0, 0});
    tapPending_ = false; tapUntil_ = 0;
    std::fill(buttonPointers_.begin(), buttonPointers_.end(), -1);
    std::fill(keyHeld_.begin(), keyHeld_.end(), 0);
}

void TopDownControls::buttonDown(int b) {
    if (b < 0 || size_t(b) >= cfg_.buttons.size()) return;
    const auto& bc = cfg_.buttons[size_t(b)];
    if (buttonPointers_[size_t(b)] < 0) keyHeld_[size_t(b)] = 1;
    if (bc.mode == TopDownButtonConfig::Mode::Tap) events_.push_back({b, topdown::Phase::Tap, false, 0, 0});
    else events_.push_back({b, topdown::Phase::Press, false, 0, 0});
}

void TopDownControls::buttonUp(int b) {
    if (b < 0 || size_t(b) >= cfg_.buttons.size()) return;
    const auto& bc = cfg_.buttons[size_t(b)];
    const bool touch = buttonPointers_[size_t(b)] >= 0;
    if (!touch && !keyHeld_[size_t(b)]) return;
    keyHeld_[size_t(b)] = 0;
    if (bc.mode == TopDownButtonConfig::Mode::Tap) return;
    TopDownButtonEvent e{b, topdown::Phase::Release, false, 0, 0};
    if (touch && bc.mode == TopDownButtonConfig::Mode::Aim) {   // the drag aims: direction + share of the range
        const float dx = dragX_[size_t(b)] - downX_[size_t(b)], dy = dragY_[size_t(b)] - downY_[size_t(b)];
        const float len = std::hypot(dx, dy);
        if (len > bc.radius * 0.35f) {
            const float k = std::min(1.0f, len / bc.dragRadius) / len;
            e.hasDir = true; e.dirX = dx * k; e.dirY = dy * k;
        }
    }
    events_.push_back(e);
}

void TopDownControls::screenToWorldDir(float sx, float sy, float yaw, float& wx, float& wz) { screenToWorld(sx, sy, yaw, wx, wz); }

topdown::Ability TopDownControls::abilityEvent(const TopDownButtonEvent& e, int abilityIndex, float yaw) {
    topdown::Ability a;
    a.index = abilityIndex;
    a.phase = e.phase;
    if (e.hasDir) {
        float wx, wz;
        screenToWorld(e.dirX, e.dirY, yaw, wx, wz);
        const float len = std::hypot(wx, wz);
        if (len > 1e-4f) {
            a.hasAngle = true;
            a.angle = binaryAngle(wx, wz);
            a.distance = int32_t(std::lround(std::clamp(len, 0.0f, 1.0f) * 1000.0f));
        }
    }
    return a;
}

bool TopDownEscapePreview::update(float dt, float dirX, float dirZ, const std::function<bool(float, float)>& passable) {
    if (!active) return false;
    const float len = std::hypot(dirX, dirZ);
    float step = std::min(speed * std::min(len, 1.0f) * dt, std::max(0.0f, budget - travelled));
    if (step > 0 && len > 0) {   // walls are kept: try the full step, then each axis (slide), in small pieces
        const float ux = dirX / len, uz = dirZ / len;
        const int n = std::max(1, int(std::ceil(step / 0.2f)));
        for (int i = 0; i < n; ++i) {
            const float s = step / float(n);
            if (!passable || passable(x + ux * s, z + uz * s)) { x += ux * s; z += uz * s; travelled += s; }
            else if (passable(x + ux * s, z)) { x += ux * s; travelled += std::fabs(ux * s); }
            else if (passable(x, z + uz * s)) { z += uz * s; travelled += std::fabs(uz * s); }
            else break;
        }
    }
    timeLeft -= dt;
    if (timeLeft > 0) return false;
    active = false;
    return true;
}

// ================================================================= input

topdown::Input TopDownControls::compose(const TopDownFrameInput& in, float yaw) {
    clock_ = in.time;
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
    bool aim = false, assisted = false;
    aim_.value(ax, ay);
    if (ax != 0 || ay != 0) assisted = cfg_.assist.touch;
    else {
        const float alen = std::hypot(in.padAimX, in.padAimY);
        if (alen > cfg_.padDeadzone) { ax = in.padAimX; ay = in.padAimY; assisted = cfg_.assist.pad; }
    }
    if (ax != 0 || ay != 0) { screenToWorld(ax, ay, yaw, awx, awz); aim = true; }
    else if (in.mouseAim && cfg_.mouseAim != TopDownControlConfig::MouseAim::Off && (in.mouseDirX != 0 || in.mouseDirZ != 0)) {
        awx = in.mouseDirX; awz = in.mouseDirZ; aim = true; assisted = cfg_.assist.mouse;
    }
    // aim assist: bend the aim to the target least off it (inside the cone; the locked one inside a wider cone),
    // nearer targets winning ties; a tap on the aim stick shoots at the nearest target for tapFireTime
    bool tapFire = false;
    const uint64_t prevLock = lockId_;
    lockId_ = 0;
    if (aim && assisted && !in.targets.empty()) {
        const float al0 = std::hypot(awx, awz);
        float best = 1e9f, maxD = 1e-3f;
        for (auto& t : in.targets) maxD = std::max(maxD, std::hypot(t.dx, t.dz));
        const TopDownFrameInput::Target* pick = nullptr;
        for (auto& t : in.targets) {
            const float d = std::hypot(t.dx, t.dz);
            if (d <= 1e-4f || al0 <= 0) continue;
            const float cosA = std::clamp((t.dx * awx + t.dz * awz) / (d * al0), -1.0f, 1.0f);
            const float deg = std::acos(cosA) * 180 / kPi;
            const float limit = cfg_.assist.cone * (t.id == prevLock ? cfg_.assist.stickiness : 1.0f);
            if (deg > limit) continue;
            const float score = deg / cfg_.assist.cone + 0.5f * d / maxD - (t.id == prevLock ? 0.25f : 0.0f);
            if (score < best) { best = score; pick = &t; }
        }
        if (pick) { awx = pick->dx; awz = pick->dz; lockId_ = pick->id; }
        tapUntil_ = 0;   // a held stick ends a tap's burst
    }
    if (tapPending_) {
        tapPending_ = false;
        const TopDownFrameInput::Target* near = nullptr;
        for (auto& t : in.targets) if (!near || std::hypot(t.dx, t.dz) < std::hypot(near->dx, near->dz)) near = &t;
        if (near) { tapId_ = near->id; tapUntil_ = clock_ + cfg_.assist.tapFireTime; }
    }
    if (!aim && tapUntil_ > clock_) {
        const TopDownFrameInput::Target* t = nullptr;
        for (auto& c : in.targets) if (c.id == tapId_) t = &c;
        if (t) { awx = t->dx; awz = t->dz; aim = true; tapFire = true; lockId_ = t->id; }
        else tapUntil_ = 0;
    }
    q.aim = aim;
    if (aim) q.aimAngle = binaryAngle(awx, awz);
    const float al = std::hypot(awx, awz);
    aiming_ = aim;
    aimDirX_ = al > 0 ? awx / al : 0; aimDirZ_ = al > 0 ? awz / al : 0;
    q.fire = in.fireKey || in.padFire || tapFire;
    return TopDown::requantize(rules_, q);
}

// ================================================================= camera

void TopDownControls::snapCamera(render::RTSCamera& cam, render::float3 hero, float mapW, float mapH) {
    camInit_ = false;
    haveLast_ = false;
    velX_ = velZ_ = speed_ = vOffX_ = vOffZ_ = zoomAdd_ = 0;
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
    // speed camera: the hero's velocity from its motion between frames (a jump = respawn / teleport: no speed)
    const bool speedCam = c.speedTo > c.speedFrom || c.velocityLookAhead > 0;
    if (speedCam && haveHero && dt > 0) {
        if (haveLast_) {
            float vx = (hero.x - lastX_) / dt, vz = (hero.z - lastZ_) / dt;
            if (std::hypot(hero.x - lastX_, hero.z - lastZ_) > 8) vx = vz = 0;
            const float k = 1 - std::exp(-10 * dt);   // frames are jittery: a short low-pass
            velX_ += (vx - velX_) * k; velZ_ += (vz - velZ_) * k;
        }
        lastX_ = hero.x; lastZ_ = hero.z; haveLast_ = true;
    } else if (!haveHero || !speedCam) { haveLast_ = false; velX_ = velZ_ = 0; }
    speed_ = std::hypot(velX_, velZ_);
    if (c.speedTo > c.speedFrom) {
        float t = std::clamp((speed_ - c.speedFrom) / (c.speedTo - c.speedFrom), 0.0f, 1.0f);
        t = t * t * (3 - 2 * t);
        const float want = (c.speedDistance - c.distance) * t;
        const float k = c.speedSmoothing > 0 ? 1 - std::exp(-c.speedSmoothing * std::max(0.0f, dt)) : 1;
        zoomAdd_ += (want - zoomAdd_) * (camInit_ ? k : 1);
    } else zoomAdd_ = 0;
    const float addLo = std::min(0.0f, c.speedDistance - c.distance), addHi = std::max(0.0f, c.speedDistance - c.distance);
    lim.minZoom = std::max(1.0f, c.minDistance + (c.speedTo > c.speedFrom ? addLo : 0));
    lim.maxZoom = c.maxDistance + (c.speedTo > c.speedFrom ? addHi : 0);
    lim.minPitch = 5; lim.maxPitch = 90;
    // look-ahead: eases toward the stick (and the aim); a released stick keeps the offset (lockOffsetIfNoInput)
    const bool input = (stickLook_ && (moveDirX_ != 0 || moveDirZ_ != 0)) || aiming_;
    if (input || !c.lockOffsetIfNoInput || !stickLook_) {
        const float mx = stickLook_ ? moveDirX_ : 0, mz = stickLook_ ? moveDirZ_ : 0;
        const float tx = mx * c.lookAhead + (aiming_ ? aimDirX_ * c.aimLookAhead : 0);
        const float tz = mz * c.lookAhead + (aiming_ ? aimDirZ_ * c.aimLookAhead : 0);
        const float t = c.lookAheadSmoothing > 0 ? 1 - std::exp(-c.lookAheadSmoothing * std::max(0.0f, dt)) : 1;
        offX_ += (tx - offX_) * t; offZ_ += (tz - offZ_) * t;
    }
    gc::Pose current{cam.target.x, cam.target.z, cam.distance, cam.yaw * 180 / kPi, cam.pitch * 180 / kPi};
    gc::Pose target = current;
    if (c.velocityLookAhead > 0) {   // ahead of the motion (never locked: it follows the speed down to zero)
        const float t = c.lookAheadSmoothing > 0 ? 1 - std::exp(-c.lookAheadSmoothing * std::max(0.0f, dt)) : 1;
        vOffX_ += (velX_ * c.velocityLookAhead - vOffX_) * t; vOffZ_ += (velZ_ * c.velocityLookAhead - vOffZ_) * t;
    } else vOffX_ = vOffZ_ = 0;
    float ox = offX_ + vOffX_, oz = offZ_ + vOffZ_;
    if (const float ol = std::hypot(ox, oz); c.maxLookAhead > 0 && ol > c.maxLookAhead) { ox *= c.maxLookAhead / ol; oz *= c.maxLookAhead / ol; }
    if (haveHero) { target.x = hero.x + ox; target.z = hero.z + oz; }
    target.yaw = c.yaw; target.pitch = c.pitch;
    // the player's own zoom (wheel / pinch) is what the camera has now minus the speed part; the speed part goes on top
    baseZoom_ = camInit_ ? std::clamp(baseZoom_ + (cam.distance - appliedZoom_), c.minDistance, c.maxDistance) : c.distance;
    target.zoom = baseZoom_ + zoomAdd_;
    if (!camInit_ || c.smoothing <= 0) { gc::constrain(target, lim); current = target; camInit_ = haveHero; }
    else if (!gc::advance(current, target, lim, std::max(0.0f, dt), c.smoothing)) { gc::constrain(target, lim); current = target; }
    cam.target = {current.x, 0, current.z};
    cam.distance = appliedZoom_ = current.zoom;
    cam.minDistance = c.minDistance + zoomAdd_; cam.maxDistance = c.maxDistance + zoomAdd_;
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
        if (const int ws = cfg_.buttons[i].weaponStep) {   // weapon switch: an arrow, right = next, left = previous
            const float s = r * 0.45f * float(ws);
            const uint32_t col = rgba(16, 20, 28, 230);
            b.verts.push_back({x + s, y, 0, 0, col});
            b.verts.push_back({x - s * 0.7f, y - std::abs(s), 0, 0, col});
            b.verts.push_back({x - s * 0.7f, y + std::abs(s), 0, 0, col});
        }
        if (held && cfg_.buttons[i].mode == TopDownButtonConfig::Mode::Aim) {   // the aim drag: a knob off the button
            float dx = dragX_[i] - downX_[i], dy = dragY_[i] - downY_[i];
            const float len = std::hypot(dx, dy), maxLen = cfg_.buttons[i].dragRadius;
            if (len > maxLen) { dx *= maxLen / len; dy *= maxLen / len; }
            if (len > r * 0.35f) {
                ring(b, x, y, maxLen + r * 0.3f, 2, rgba(235, 240, 255, 90));
                disc(b, x + dx, y + dy, r * 0.45f, rgba(255, 220, 120, 230));
            }
        }
    }
    if (escape_.active) {   // escape preview: the time left fills the escape button
        for (size_t i = 0; i < cfg_.buttons.size(); ++i)
            if (cfg_.buttons[i].abilityIndex == topdown::kEscapeAbility) {
                float x, y;
                buttonCenter(i, W, H, x, y);
                disc(b, x, y, cfg_.buttons[i].radius * 0.86f, rgba(255, 200, 90, 220), 0, std::clamp(escape_.progress(), 0.0f, 1.0f));
            }
    }
    return out;
}

ViewDrawBatch TopDownControls::drawLock(float x, float y, float r, float time) {
    ViewDrawBatch b;
    b.layer = 99;
    const float pulse = 1 + 0.08f * std::sin(time * 8);
    const float rr = r * pulse;
    ring(b, x, y, rr, 2.5f, rgba(255, 70, 60, 220));
    for (int k = 0; k < 4; ++k) {   // four ticks pointing in
        const float a = float(k) * kPi / 2 + time * 1.5f;
        const float cx = std::cos(a), cy = std::sin(a), px = -cy, py = cx;
        const float o = rr + 9, i = rr + 1, w = 4;
        b.verts.push_back({x + cx * o + px * w, y + cy * o + py * w, 0, 0, rgba(255, 70, 60, 230)});
        b.verts.push_back({x + cx * o - px * w, y + cy * o - py * w, 0, 0, rgba(255, 70, 60, 230)});
        b.verts.push_back({x + cx * i, y + cy * i, 0, 0, rgba(255, 70, 60, 230)});
    }
    return b;
}

} // namespace aiverse::game
