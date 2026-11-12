// Windows: XInput (xinput1_4.dll, else 9_1_0, loaded at run time so nothing is linked), the four
// XInput slots. DirectInput-only pads (old generic joysticks) are not read.
#include "engine/platform/Gamepad.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xinput.h>

namespace aiverse::platform::detail {
namespace {
using GetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
GetStateFn g_get = nullptr;
bool g_tried = false;
DWORD g_retryAt[kMaxGamepads] = {};

float stick(SHORT v) { return v < 0 ? float(v) / 32768.f : float(v) / 32767.f; }
} // namespace

void pollGamepadBackend(GamepadState (&pads)[kMaxGamepads]) {
    if (!g_tried) {
        g_tried = true;
        HMODULE m = LoadLibraryA("xinput1_4.dll");
        if (!m) m = LoadLibraryA("xinput9_1_0.dll");
        if (m) g_get = reinterpret_cast<GetStateFn>(reinterpret_cast<void*>(GetProcAddress(m, "XInputGetState")));
    }
    if (!g_get) return;
    DWORD now = GetTickCount();
    for (DWORD i = 0; i < DWORD(kMaxGamepads); i++) {
        GamepadState& p = pads[i];
        if (!p.connected && now < g_retryAt[i]) continue;   // polling an empty slot is slow
        XINPUT_STATE s{};
        if (g_get(i, &s) != ERROR_SUCCESS) { p.connected = false; g_retryAt[i] = now + 2000; continue; }
        if (!p.connected) p.name = "XInput controller";
        p.connected = true;
        const WORD b = s.Gamepad.wButtons;
        auto set = [&](PadButton pb, WORD mask) { p.buttons[int(pb)] = (b & mask) != 0; };
        set(PadButton::A, XINPUT_GAMEPAD_A); set(PadButton::B, XINPUT_GAMEPAD_B); set(PadButton::X, XINPUT_GAMEPAD_X); set(PadButton::Y, XINPUT_GAMEPAD_Y);
        set(PadButton::LB, XINPUT_GAMEPAD_LEFT_SHOULDER); set(PadButton::RB, XINPUT_GAMEPAD_RIGHT_SHOULDER);
        set(PadButton::Back, XINPUT_GAMEPAD_BACK); set(PadButton::Start, XINPUT_GAMEPAD_START);
        set(PadButton::LStick, XINPUT_GAMEPAD_LEFT_THUMB); set(PadButton::RStick, XINPUT_GAMEPAD_RIGHT_THUMB);
        set(PadButton::Up, XINPUT_GAMEPAD_DPAD_UP); set(PadButton::Down, XINPUT_GAMEPAD_DPAD_DOWN);
        set(PadButton::Left, XINPUT_GAMEPAD_DPAD_LEFT); set(PadButton::Right, XINPUT_GAMEPAD_DPAD_RIGHT);
        p.axes[int(PadAxis::LX)] = stick(s.Gamepad.sThumbLX);
        p.axes[int(PadAxis::LY)] = -stick(s.Gamepad.sThumbLY);   // XInput: up = +
        p.axes[int(PadAxis::RX)] = stick(s.Gamepad.sThumbRX);
        p.axes[int(PadAxis::RY)] = -stick(s.Gamepad.sThumbRY);
        p.axes[int(PadAxis::LT)] = float(s.Gamepad.bLeftTrigger) / 255.f;
        p.axes[int(PadAxis::RT)] = float(s.Gamepad.bRightTrigger) / 255.f;
    }
}

} // namespace aiverse::platform::detail
