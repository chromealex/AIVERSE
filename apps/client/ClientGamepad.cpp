#include "ClientGamepad.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace aiverse::client {

using platform::Key;
using platform::PadAxis;
using platform::PadButton;

Key ClientGamepad::keyFromName(const std::string& n) {
    static const char* kNames[] = {"Unknown", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T",
                                   "U", "V", "W", "X", "Y", "Z", "Num0", "Num1", "Num2", "Num3", "Num4", "Num5", "Num6", "Num7", "Num8", "Num9",
                                   "Escape", "Enter", "Tab", "Backspace", "Delete", "Insert", "Space", "Left", "Right", "Up", "Down", "Home",
                                   "End", "PageUp", "PageDown", "LeftShift", "RightShift", "LeftCtrl", "RightCtrl", "LeftAlt", "RightAlt",
                                   "LeftSuper", "RightSuper", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
                                   "Minus", "Equal", "LeftBracket", "RightBracket", "Semicolon", "Apostrophe", "Comma", "Period", "Slash",
                                   "Backslash", "GraveAccent"};
    static_assert(sizeof kNames / sizeof kNames[0] == size_t(Key::Count), "key names follow platform::Key");
    for (size_t i = 1; i < size_t(Key::Count); i++) if (n == kNames[i]) return Key(i);
    if (n.size() == 1 && n[0] >= '0' && n[0] <= '9') return Key(int(Key::Num0) + (n[0] - '0'));
    return Key::Unknown;
}

ClientGamepad::Binding ClientGamepad::parseBinding(const std::string& s) {
    Binding b;
    if (s == "click") b.kind = Binding::Click;
    else if (s == "rightClick") b.kind = Binding::RightClick;
    else if (s == "middleClick") b.kind = Binding::MiddleClick;
    else if (s == "pan") b.kind = Binding::Pan;
    else if (s == "upgrade" || s == "cancel" || s.rfind("send:", 0) == 0 || s.rfind("ability:", 0) == 0 ||
             s.rfind("convert:", 0) == 0 || s.rfind("event:", 0) == 0) {
        b.kind = Binding::Action;
        b.action = s;
    }
    else if (s.rfind("key:", 0) == 0) {
        b.key = keyFromName(s.substr(4));
        if (b.key != Key::Unknown) b.kind = Binding::Key;
    }
    return b;
}

int ClientGamepad::slotFromName(const std::string& s) {
    if (s == "lt") return kLT;
    if (s == "rt") return kRT;
    PadButton b = platform::padButtonFromName(s);
    return b == PadButton::Count ? -1 : int(b);
}

void ClientGamepad::configure(const Json& manifest) {
    const Json& g = manifest["gamepad"];
    enabled_ = g.getBool("enabled", true);
    deadZone_ = std::clamp(float(g["deadZone"].toDouble(0.2)), 0.f, 0.9f);
    speed_ = float(g["cursorSpeed"].toDouble(900));
    panSpeed_ = float(g["panSpeed"].toDouble(1));
    zoomSpeed_ = float(g["zoomSpeed"].toDouble(1));
    const Json& snap = g["snap"];
    snapTargets_ = snap.isString() ? snap.str() : snap.getStr("targets", "structures");
    snapRadius_ = float(snap["radius"].toDouble(70));
    flick_ = snap.getBool("flick", true);
    for (auto& b : bindings_) b = Binding{};
    chords_.clear();
    bindings_[int(PadButton::A)] = parseBinding("click");
    bindings_[int(PadButton::B)] = parseBinding("rightClick");
    bindings_[int(PadButton::Start)] = parseBinding("key:Escape");
    for (PadButton d : {PadButton::Up, PadButton::Down, PadButton::Left, PadButton::Right}) bindings_[int(d)] = parseBinding("pan");
    auto chord = [&](const std::string& key, const std::string& what) {
        const size_t plus = key.find('+');
        if (plus == std::string::npos) return;
        Chord c;
        c.mod = slotFromName(key.substr(0, plus));
        c.button = slotFromName(key.substr(plus + 1));
        c.binding = parseBinding(what);
        if (c.mod < 0 || c.button < 0) return;
        for (auto& o : chords_)
            if (o.mod == c.mod && o.button == c.button) { o = c; return; }
        chords_.push_back(c);
    };
    if (g.getStr("scheme", "") == "bunkerwars") {
        // Bunker Wars InputConfig.asset (joystickItems; JoystickButton BottomRow1 / 2 = A / B, TopRow1 / 2 = X / Y)
        const char* slot[] = {"right", "down", "left", "up"};   // SelectSlot1..4 on DPadRight / Down / Left / Up (mode Default)
        for (int i = 0; i < 4; i++) bindings_[slotFromName(slot[i])] = parseBinding("ability:" + std::to_string(i + 1));
        chord("rb+b", "send:25"); chord("rb+a", "send:50"); chord("rb+x", "send:75"); chord("rb+y", "send:100");   // mode UnitsMove (R1)
        chord("lb+a", "upgrade"); chord("lb+x", "convert:bunker"); chord("lb+y", "convert:tower"); chord("lb+b", "convert:smithy");  // UpgradeMenu (L1)
        chord("a+a", "upgrade");                                                                     // BottomRow1 twice
        chord("lt+b", "event:emotion1"); chord("lt+a", "event:emotion2"); chord("lt+x", "event:emotion3"); chord("lt+y", "event:emotion4");  // Emotion (L2)
    }
    for (const auto& [name, v] : g["buttons"].members()) {
        int b = slotFromName(name);
        if (b >= 0) bindings_[b] = parseBinding(v.str());
    }
    for (const auto& [name, v] : g["chords"].members()) chord(name, v.str());
    for (auto& m : isMod_) m = false;
    for (const auto& c : chords_) if (c.mod != c.button) isMod_[c.mod] = true;
}

ClientGamepad::Frame ClientGamepad::update(const platform::GamepadState& pad, float dt, float w, float h, const std::vector<Target>& targets) {
    Frame f;
    if (!enabled_ || !pad.connected) return f;
    if (x_ < 0 || y_ < 0) { x_ = w * 0.5f; y_ = h * 0.5f; }
    float lx = pad.axis(PadAxis::LX), ly = pad.axis(PadAxis::LY), rx = pad.axis(PadAxis::RX), ry = pad.axis(PadAxis::RY);
    platform::applyDeadZone(lx, ly, deadZone_);
    platform::applyDeadZone(rx, ry, deadZone_);
    float lt = pad.axis(PadAxis::LT), rt = pad.axis(PadAxis::RT);
    if (lt < deadZone_) lt = 0;
    if (rt < deadZone_) rt = 0;
    bool any = lx != 0 || ly != 0 || rx != 0 || ry != 0 || lt != 0 || rt != 0;
    for (int i = 0; i < int(PadButton::Count); i++) any |= pad.buttons[i];
    if (any) active_ = true;
    if (!active_) return f;

    const float ox = x_, oy = y_;
    float mag = std::sqrt(lx * lx + ly * ly);
    bool snapping = snapTargets_ != "none" && !targets.empty();
    if (snapping && flick_ && mag > 0.85f && !flicked_) {
        // flick: jump to the nearest target in the stick's direction (within 50 degrees)
        float dx = lx / mag, dy = ly / mag, best = 1e30f;
        const Target* pick = nullptr;
        for (const Target& t : targets) {
            float vx = t.x - x_, vy = t.y - y_, d = std::sqrt(vx * vx + vy * vy);
            if (d < 8) continue;
            float cosA = (vx * dx + vy * dy) / d;
            if (cosA < 0.64f) continue;
            float score = d * (2.f - cosA);   // nearer and straighter wins
            if (score < best) { best = score; pick = &t; }
        }
        flicked_ = true;
        if (pick) { x_ = pick->x; y_ = pick->y; }
    } else if (mag > 0) {
        if (!(flicked_ && mag > 0.85f)) {   // after a flick the held stick doesn't drift the cursor
            float s = speed_ * mag * mag * dt;   // quadratic: fine control near the centre
            x_ += lx / mag * s;
            y_ += ly / mag * s;
        }
    }
    if (mag < 0.3f) flicked_ = false;
    if (snapping && mag == 0) {   // magnet: a resting cursor settles on a target under it
        const Target* near = nullptr;
        float best = snapRadius_ * snapRadius_;
        for (const Target& t : targets) {
            float d = (t.x - x_) * (t.x - x_) + (t.y - y_) * (t.y - y_);
            if (d < best) { best = d; near = &t; }
        }
        if (near) {
            float k = std::min(1.f, 14.f * dt);
            x_ += (near->x - x_) * k;
            y_ += (near->y - y_) * k;
        }
    }
    x_ = std::clamp(x_, 0.f, w);
    y_ = std::clamp(y_, 0.f, h);
    auto ev = [&](platform::EventType t) {
        platform::Event e;
        e.type = t;
        e.x = x_;
        e.y = y_;
        return e;
    };
    if (std::fabs(x_ - ox) > 0.01f || std::fabs(y_ - oy) > 0.01f) f.events.push_back(ev(platform::EventType::MouseMove));

    const bool trig[2] = {lt > 0.5f, rt > 0.5f};
    auto isDown = [&](int i) { return i < kLT ? pad.buttons[i] : trig[i - kLT]; };
    auto wasDown = [&](int i) { return i < kLT ? pad.prev[i] : trigPrev_[i - kLT]; };
    auto act = [&](const Binding& b) { f.actions.push_back({b.action, x_, y_, originX_, originY_}); };
    for (int i = 0; i < kSlots; i++) {
        lastPress_[i] += dt;
        const Binding& b = bindings_[i];
        const bool down = isDown(i), was = wasDown(i);
        if (down && !was) {
            if (isMod_[i]) {   // a chord modifier: quiet itself; a send modifier fixes the origin
                swallowed_[i] = true;
                for (const auto& c : chords_)
                    if (c.mod == i && c.binding.action.rfind("send:", 0) == 0) { aiming_ = true; originX_ = x_; originY_ = y_; }
                continue;
            }
            const Chord* hit = nullptr;
            for (const auto& c : chords_) {
                if (c.button != i) continue;
                if (c.mod != i && isDown(c.mod)) { hit = &c; break; }
                if (c.mod == i && lastPress_[i] < 0.3f && !hit) hit = &c;
            }
            const bool dbl = hit && hit->mod == i;
            lastPress_[i] = dbl ? 1.f : 0.f;   // a triple press is a double press and a single one
            if (hit && !dbl) {
                swallowed_[i] = true;
                if (hit->binding.kind == Binding::Action) act(hit->binding);
                continue;
            }
            if (dbl && hit->binding.kind == Binding::Action) act(hit->binding);
        } else if (!down && was && swallowed_[i]) {
            swallowed_[i] = false;
            if (isMod_[i]) aiming_ = false;
            continue;
        }
        if (swallowed_[i] || i >= kLT) continue;
        if (b.kind == Binding::Pan) {
            if (!down) continue;
            PadButton pb = PadButton(i);
            if (pb == PadButton::Left) f.panX -= 1;
            if (pb == PadButton::Right) f.panX += 1;
            if (pb == PadButton::Up) f.panY -= 1;
            if (pb == PadButton::Down) f.panY += 1;
            continue;
        }
        if (down == was || b.kind == Binding::None) continue;
        if (b.kind == Binding::Action) {
            if (down) act(b);
        } else if (b.kind == Binding::Key) {
            platform::Event e = ev(down ? platform::EventType::KeyDown : platform::EventType::KeyUp);
            e.key = b.key;
            f.events.push_back(e);
        } else {
            platform::Event e = ev(down ? platform::EventType::MouseDown : platform::EventType::MouseUp);
            e.button = b.kind == Binding::Click ? 0 : b.kind == Binding::RightClick ? 1 : 2;
            f.events.push_back(e);
        }
    }
    trigPrev_[0] = trig[0];
    trigPrev_[1] = trig[1];
    if (isMod_[kLT]) lt = 0;   // a trigger used as a modifier no longer zooms
    if (isMod_[kRT]) rt = 0;
    f.aiming = aiming_;
    f.originX = originX_;
    f.originY = originY_;
    f.panX = std::clamp(f.panX + rx, -1.f, 1.f) * panSpeed_;
    f.panY = std::clamp(f.panY + ry, -1.f, 1.f) * panSpeed_;
    float z = (rt - lt) * zoomSpeed_ * dt * 6.f;   // full trigger: 6 wheel steps per second
    if (z != 0) {
        platform::Event e = ev(platform::EventType::MouseWheel);
        e.wheelY = z;
        f.events.push_back(e);
        f.zoom = z;
    }
    return f;
}

} // namespace aiverse::client

