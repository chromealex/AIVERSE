#include "engine/platform/Gamepad.h"

#include <cmath>

namespace aiverse::platform {
namespace {
GamepadState g_pads[kMaxGamepads];
void (*g_backend)(GamepadState (&)[kMaxGamepads]) = nullptr;
const char* kNames[] = {"a", "b", "x", "y", "lb", "rb", "back", "start", "ls", "rs", "up", "down", "left", "right", "guide"};
} // namespace

int pollGamepads() {
    for (auto& p : g_pads)
        for (int i = 0; i < int(PadButton::Count); i++) p.prev[i] = p.buttons[i];
    if (g_backend) g_backend(g_pads);
    else detail::pollGamepadBackend(g_pads);
    int n = 0;
    for (auto& p : g_pads) {
        if (!p.connected) {   // a pad that left releases everything
            for (auto& b : p.buttons) b = false;
            for (auto& a : p.axes) a = 0;
        }
        n += p.connected;
    }
    return n;
}

const GamepadState& gamepad(int index) {
    static const GamepadState none;
    return index >= 0 && index < kMaxGamepads ? g_pads[index] : none;
}

const char* padButtonName(PadButton b) { return int(b) < int(PadButton::Count) ? kNames[int(b)] : ""; }

PadButton padButtonFromName(const std::string& n) {
    for (int i = 0; i < int(PadButton::Count); i++) if (n == kNames[i]) return PadButton(i);
    return PadButton::Count;
}

void applyDeadZone(float& x, float& y, float dz) {
    float len = std::sqrt(x * x + y * y);
    if (len <= dz || len <= 0) { x = y = 0; return; }
    float k = std::fmin(1.f, (len - dz) / (1.f - dz)) / len;
    x *= k;
    y *= k;
}

namespace detail {
void setGamepadBackend(void (*poll)(GamepadState (&)[kMaxGamepads])) { g_backend = poll; }
} // namespace detail

} // namespace aiverse::platform
