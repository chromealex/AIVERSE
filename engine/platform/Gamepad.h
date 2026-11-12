// Gamepads / joysticks: polled state of up to four controllers in the standard layout (Xbox naming:
// A bottom, B right, X left, Y top; PlayStation / Switch pads are mapped to the same positions).
//
//   int n = platform::pollGamepads();         // once per frame (the game client does it)
//   const auto& p = platform::gamepad(0);
//   if (p.connected && p.pressed(PadButton::A)) ...;  float x = p.axis(PadAxis::LX);
//
// Backends: Linux joystick API (/dev/input/js*), Windows XInput, web Gamepad API (standard mapping),
// macOS / iOS GameController framework, Android input events (AINPUT_SOURCE_GAMEPAD / JOYSTICK).
// Presentation / input only: the game turns pad input into the same Commands as mouse and touch.
#pragma once
#include <cstdint>
#include <string>

namespace aiverse::platform {

enum class PadButton : uint8_t { A, B, X, Y, LB, RB, Back, Start, LStick, RStick, Up, Down, Left, Right, Guide, Count };
enum class PadAxis : uint8_t { LX, LY, RX, RY, LT, RT, Count };   // sticks -1..1 (LY / RY: down = +1), triggers 0..1

constexpr int kMaxGamepads = 4;

struct GamepadState {
    bool connected = false;
    std::string name;
    bool buttons[int(PadButton::Count)] = {};
    float axes[int(PadAxis::Count)] = {};
    bool prev[int(PadButton::Count)] = {};    // last frame (pressed / released edges)

    bool down(PadButton b) const { return buttons[int(b)]; }
    bool pressed(PadButton b) const { return buttons[int(b)] && !prev[int(b)]; }
    bool released(PadButton b) const { return !buttons[int(b)] && prev[int(b)]; }
    float axis(PadAxis a) const { return axes[int(a)]; }
};

// Refreshes every pad (keeps last frame's buttons for edges); returns how many are connected.
int pollGamepads();
const GamepadState& gamepad(int index);
const char* padButtonName(PadButton b);   // "a", "b", "x", "y", "lb", "rb", "back", "start", "ls", "rs", "up", "down", "left", "right", "guide"
PadButton padButtonFromName(const std::string& name);   // Count when unknown
// Radial dead zone with rescaling (|v| < dz -> 0, then linear up to 1).
void applyDeadZone(float& x, float& y, float deadZone);

namespace detail {
// Implemented per platform (engine/platform/gamepad/Gamepad<Platform>.*): fills the current state.
void pollGamepadBackend(GamepadState (&pads)[kMaxGamepads]);
// Tests / tools: replaces the backend (nullptr = the platform's own).
void setGamepadBackend(void (*poll)(GamepadState (&pads)[kMaxGamepads]));
} // namespace detail

} // namespace aiverse::platform
