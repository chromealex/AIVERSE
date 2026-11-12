// Platforms without a gamepad backend: no pads.
#include "engine/platform/Gamepad.h"

namespace aiverse::platform::detail {
void pollGamepadBackend(GamepadState (&)[kMaxGamepads]) {}
} // namespace aiverse::platform::detail
