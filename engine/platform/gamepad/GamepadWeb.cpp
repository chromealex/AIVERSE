// Web: the Gamepad API (navigator.getGamepads()); pads with mapping "standard" use the W3C layout
// (buttons 0..16, axes 0..3), which is the Xbox layout of PadButton / PadAxis.
#include "engine/platform/Gamepad.h"

#include <emscripten.h>

namespace aiverse::platform::detail {
namespace {
// out: 4 pads x (connected, 17 buttons, 4 axes) floats
EM_JS(int, aiverse_web_gamepads, (float* out), {
    var pads = navigator.getGamepads ? navigator.getGamepads() : [];
    var n = 0;
    for (var i = 0; i < 4; i++) {
        var p = pads[i], base = (out >> 2) + i * 22;
        HEAPF32[base] = p && p.connected ? 1 : 0;
        if (!p || !p.connected) continue;
        n++;
        for (var b = 0; b < 17; b++) HEAPF32[base + 1 + b] = p.buttons[b] ? p.buttons[b].value : 0;
        for (var a = 0; a < 4; a++) HEAPF32[base + 18 + a] = p.axes[a] || 0;
    }
    return n;
});
} // namespace

void pollGamepadBackend(GamepadState (&pads)[kMaxGamepads]) {
    float raw[kMaxGamepads * 22] = {};
    aiverse_web_gamepads(raw);
    static const PadButton kMap[17] = {PadButton::A, PadButton::B, PadButton::X, PadButton::Y, PadButton::LB, PadButton::RB,
                                       PadButton::Count, PadButton::Count,   // 6, 7: triggers (axes below)
                                       PadButton::Back, PadButton::Start, PadButton::LStick, PadButton::RStick,
                                       PadButton::Up, PadButton::Down, PadButton::Left, PadButton::Right, PadButton::Guide};
    for (int i = 0; i < kMaxGamepads; i++) {
        const float* r = raw + i * 22;
        GamepadState& p = pads[i];
        p.connected = r[0] > 0.5f;
        if (!p.connected) continue;
        if (p.name.empty()) p.name = "Gamepad";
        for (int b = 0; b < 17; b++) if (kMap[b] != PadButton::Count) p.buttons[int(kMap[b])] = r[1 + b] > 0.5f;
        p.axes[int(PadAxis::LT)] = r[1 + 6];
        p.axes[int(PadAxis::RT)] = r[1 + 7];
        p.axes[int(PadAxis::LX)] = r[18]; p.axes[int(PadAxis::LY)] = r[19];
        p.axes[int(PadAxis::RX)] = r[20]; p.axes[int(PadAxis::RY)] = r[21];
    }
}

} // namespace aiverse::platform::detail
