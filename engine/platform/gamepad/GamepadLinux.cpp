// Linux joystick API (/dev/input/js0..3): non-blocking reads of js_event, rescanned every two
// seconds for hot-plug. The kernel's xpad / hid-sony / hid-nintendo drivers report the Xbox layout
// for buttons 0..10 and axes 0..7 (LX LY LT RX RY RT HatX HatY); other pads may differ.
#include "engine/platform/Gamepad.h"

#include <fcntl.h>
#include <linux/joystick.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdio>

namespace aiverse::platform::detail {
namespace {
int g_fd[kMaxGamepads] = {-1, -1, -1, -1};
std::chrono::steady_clock::time_point g_lastScan{};

float norm(int16_t v) { return v < 0 ? float(v) / 32768.f : float(v) / 32767.f; }

void applyAxis(GamepadState& p, int axis, int16_t value) {
    float v = norm(value);
    switch (axis) {
    case 0: p.axes[int(PadAxis::LX)] = v; break;
    case 1: p.axes[int(PadAxis::LY)] = v; break;
    case 2: p.axes[int(PadAxis::LT)] = (v + 1) * 0.5f; break;
    case 3: p.axes[int(PadAxis::RX)] = v; break;
    case 4: p.axes[int(PadAxis::RY)] = v; break;
    case 5: p.axes[int(PadAxis::RT)] = (v + 1) * 0.5f; break;
    case 6: p.buttons[int(PadButton::Left)] = v < -0.5f; p.buttons[int(PadButton::Right)] = v > 0.5f; break;
    case 7: p.buttons[int(PadButton::Up)] = v < -0.5f; p.buttons[int(PadButton::Down)] = v > 0.5f; break;
    default: break;
    }
}

void applyButton(GamepadState& p, int b, bool on) {
    static const PadButton kMap[] = {PadButton::A, PadButton::B, PadButton::X, PadButton::Y, PadButton::LB, PadButton::RB,
                                     PadButton::Back, PadButton::Start, PadButton::Guide, PadButton::LStick, PadButton::RStick};
    if (b >= 0 && b < int(sizeof kMap / sizeof kMap[0])) p.buttons[int(kMap[b])] = on;
}
} // namespace

void pollGamepadBackend(GamepadState (&pads)[kMaxGamepads]) {
    auto now = std::chrono::steady_clock::now();
    if (now - g_lastScan > std::chrono::seconds(2)) {
        g_lastScan = now;
        for (int i = 0; i < kMaxGamepads; i++) {
            if (g_fd[i] >= 0) continue;
            char path[32];
            std::snprintf(path, sizeof path, "/dev/input/js%d", i);
            int fd = ::open(path, O_RDONLY | O_NONBLOCK);
            if (fd < 0) continue;
            g_fd[i] = fd;
            char name[128] = "joystick";
            ioctl(fd, JSIOCGNAME(sizeof name), name);
            pads[i] = GamepadState{};
            pads[i].name = name;
            pads[i].connected = true;
        }
    }
    for (int i = 0; i < kMaxGamepads; i++) {
        if (g_fd[i] < 0) continue;
        js_event e;
        ssize_t n;
        while ((n = ::read(g_fd[i], &e, sizeof e)) == ssize_t(sizeof e)) {
            if (e.type & JS_EVENT_BUTTON) applyButton(pads[i], e.number, e.value != 0);
            else if (e.type & JS_EVENT_AXIS) applyAxis(pads[i], e.number, e.value);
        }
        if (n < 0 && errno != EAGAIN) {   // unplugged
            ::close(g_fd[i]);
            g_fd[i] = -1;
            pads[i].connected = false;
        }
    }
}

} // namespace aiverse::platform::detail
