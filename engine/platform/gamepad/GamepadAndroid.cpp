// Android: gamepad key and motion events from the NativeActivity input queue (PlatformAndroid.cpp
// hands every event to androidGamepadEvent first). Devices are numbered in the order they send
// input; AKEYCODE_BUTTON_* and AMOTION_EVENT_AXIS_* use the Android standard gamepad layout.
#include "engine/platform/Gamepad.h"

#include <android/input.h>

#include <algorithm>

namespace aiverse::platform::detail {
namespace {
GamepadState g_live[kMaxGamepads];
int32_t g_device[kMaxGamepads] = {-1, -1, -1, -1};

int slot(int32_t device) {
    for (int i = 0; i < kMaxGamepads; i++) if (g_device[i] == device) return i;
    for (int i = 0; i < kMaxGamepads; i++)
        if (g_device[i] < 0) { g_device[i] = device; g_live[i] = GamepadState{}; g_live[i].connected = true; g_live[i].name = "Gamepad"; return i; }
    return -1;
}

PadButton button(int32_t key) {
    switch (key) {
    case AKEYCODE_BUTTON_A: return PadButton::A;
    case AKEYCODE_BUTTON_B: return PadButton::B;
    case AKEYCODE_BUTTON_X: return PadButton::X;
    case AKEYCODE_BUTTON_Y: return PadButton::Y;
    case AKEYCODE_BUTTON_L1: return PadButton::LB;
    case AKEYCODE_BUTTON_R1: return PadButton::RB;
    case AKEYCODE_BUTTON_SELECT: return PadButton::Back;
    case AKEYCODE_BUTTON_START: return PadButton::Start;
    case AKEYCODE_BUTTON_MODE: return PadButton::Guide;
    case AKEYCODE_BUTTON_THUMBL: return PadButton::LStick;
    case AKEYCODE_BUTTON_THUMBR: return PadButton::RStick;
    case AKEYCODE_DPAD_UP: return PadButton::Up;
    case AKEYCODE_DPAD_DOWN: return PadButton::Down;
    case AKEYCODE_DPAD_LEFT: return PadButton::Left;
    case AKEYCODE_DPAD_RIGHT: return PadButton::Right;
    default: return PadButton::Count;
    }
}
} // namespace

// Returns true when the event came from a gamepad / joystick and was consumed.
bool androidGamepadEvent(const AInputEvent* ev) {
    int32_t src = AInputEvent_getSource(ev);
    bool pad = (src & AINPUT_SOURCE_GAMEPAD) == AINPUT_SOURCE_GAMEPAD || (src & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK;
    if (!pad) return false;
    int s = slot(AInputEvent_getDeviceId(ev));
    if (s < 0) return true;
    GamepadState& p = g_live[s];
    if (AInputEvent_getType(ev) == AINPUT_EVENT_TYPE_KEY) {
        PadButton b = button(AKeyEvent_getKeyCode(ev));
        if (b == PadButton::Count) return false;   // other keys (Back) go to the game as keys
        p.buttons[int(b)] = AKeyEvent_getAction(ev) == AKEY_EVENT_ACTION_DOWN;
        return true;
    }
    auto ax = [&](int32_t a) { return AMotionEvent_getAxisValue(ev, a, 0); };
    p.axes[int(PadAxis::LX)] = ax(AMOTION_EVENT_AXIS_X);
    p.axes[int(PadAxis::LY)] = ax(AMOTION_EVENT_AXIS_Y);
    p.axes[int(PadAxis::RX)] = ax(AMOTION_EVENT_AXIS_Z);
    p.axes[int(PadAxis::RY)] = ax(AMOTION_EVENT_AXIS_RZ);
    p.axes[int(PadAxis::LT)] = std::max(ax(AMOTION_EVENT_AXIS_LTRIGGER), ax(AMOTION_EVENT_AXIS_BRAKE));
    p.axes[int(PadAxis::RT)] = std::max(ax(AMOTION_EVENT_AXIS_RTRIGGER), ax(AMOTION_EVENT_AXIS_GAS));
    float hx = ax(AMOTION_EVENT_AXIS_HAT_X), hy = ax(AMOTION_EVENT_AXIS_HAT_Y);
    p.buttons[int(PadButton::Left)] = hx < -0.5f; p.buttons[int(PadButton::Right)] = hx > 0.5f;
    p.buttons[int(PadButton::Up)] = hy < -0.5f; p.buttons[int(PadButton::Down)] = hy > 0.5f;
    return true;
}

void pollGamepadBackend(GamepadState (&pads)[kMaxGamepads]) {
    for (int i = 0; i < kMaxGamepads; i++) {
        bool prev[int(PadButton::Count)];
        for (int b = 0; b < int(PadButton::Count); b++) prev[b] = pads[i].prev[b];
        pads[i] = g_live[i];
        for (int b = 0; b < int(PadButton::Count); b++) pads[i].prev[b] = prev[b];
    }
}

} // namespace aiverse::platform::detail
