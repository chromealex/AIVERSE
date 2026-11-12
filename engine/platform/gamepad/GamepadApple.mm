// macOS / iOS: GameController framework (Xbox, PlayStation, Switch Pro, MFi pads), extended gamepad
// profile; controllers in connection order fill the four slots.
#include "engine/platform/Gamepad.h"

#import <GameController/GameController.h>

namespace aiverse::platform::detail {

void pollGamepadBackend(GamepadState (&pads)[kMaxGamepads]) {
    @autoreleasepool {
        NSArray<GCController*>* list = GCController.controllers;
        for (int i = 0; i < kMaxGamepads; i++) {
            GamepadState& p = pads[i];
            GCExtendedGamepad* g = i < int(list.count) ? list[NSUInteger(i)].extendedGamepad : nil;
            p.connected = g != nil;
            if (!g) continue;
            if (p.name.empty()) p.name = list[NSUInteger(i)].vendorName.UTF8String ?: "Gamepad";
            auto set = [&](PadButton b, GCControllerButtonInput* in) { p.buttons[int(b)] = in && in.isPressed; };
            set(PadButton::A, g.buttonA); set(PadButton::B, g.buttonB); set(PadButton::X, g.buttonX); set(PadButton::Y, g.buttonY);
            set(PadButton::LB, g.leftShoulder); set(PadButton::RB, g.rightShoulder);
            set(PadButton::Back, g.buttonOptions); set(PadButton::Start, g.buttonMenu); set(PadButton::Guide, g.buttonHome);
            set(PadButton::LStick, g.leftThumbstickButton); set(PadButton::RStick, g.rightThumbstickButton);
            set(PadButton::Up, g.dpad.up); set(PadButton::Down, g.dpad.down); set(PadButton::Left, g.dpad.left); set(PadButton::Right, g.dpad.right);
            p.axes[int(PadAxis::LX)] = g.leftThumbstick.xAxis.value;
            p.axes[int(PadAxis::LY)] = -g.leftThumbstick.yAxis.value;   // GameController: up = +
            p.axes[int(PadAxis::RX)] = g.rightThumbstick.xAxis.value;
            p.axes[int(PadAxis::RY)] = -g.rightThumbstick.yAxis.value;
            p.axes[int(PadAxis::LT)] = g.leftTrigger.value;
            p.axes[int(PadAxis::RT)] = g.rightTrigger.value;
        }
    }
}

} // namespace aiverse::platform::detail
