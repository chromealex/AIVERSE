// Translates AIVERSE platform events/gamepad polls to the presentation-side AIVerse InputFlow core.
// A hosted view may feed its own forwarded Event stream. Do not feed both host and guest events.
#pragma once
#include "input/InputFlowCore.h"
#include "Platform.h"
#include "Gamepad.h"
#include <cmath>
#include <cstdint>
#include <iterator>
#include <map>

namespace aiverse::platform {

class InputFlowAdapter {
public:
    explicit InputFlowAdapter(lib::gameplay::inputflow::InputFlow& input) : input_(input) {}

    void onEvent(const Event& event) {
        using namespace lib::gameplay::inputflow;
        switch (event.type) {
        case EventType::KeyDown:
            if (event.key != Key::Unknown)
                input_.setControl({Device::Keyboard, int(event.key)}, 1);
            break;
        case EventType::KeyUp:
            if (event.key != Key::Unknown)
                input_.setControl({Device::Keyboard, int(event.key)}, 0);
            break;
        case EventType::MouseDown:
            input_.setControl({Device::MouseButton, event.button}, 1);
            break;
        case EventType::MouseUp:
            input_.setControl({Device::MouseButton, event.button}, 0);
            break;
        case EventType::MouseMove:
            if (event.deltaX != 0) input_.addDelta({Device::MouseAxis, 0}, event.deltaX);
            if (event.deltaY != 0) input_.addDelta({Device::MouseAxis, 1}, event.deltaY);
            break;
        case EventType::MouseWheel:
            if (event.wheelX != 0) input_.addDelta({Device::MouseWheel, 0}, event.wheelX);
            if (event.wheelY != 0) input_.addDelta({Device::MouseWheel, 1}, event.wheelY);
            break;
        case EventType::TouchBegin: {
            if (touches_.contains(event.touchId)) break; // repeated touch event is not another finger
            int slot = 0;
            for (; slot < 32; ++slot) {
                bool used = false;
                for (const auto& [id, touch] : touches_) {
                    (void)id;
                    if (touch.slot == slot) { used = true; break; }
                }
                if (!used) break;
            }
            if (slot == 32) break; // ignore excess touches; profile indices are 0..31
            touches_[event.touchId] = {slot,event.x,event.y};
            input_.setControl({Device::Touch, 0, slot}, 1);
            updateGesture(true);
            break;
        }
        case EventType::TouchMove: {
            const auto it = touches_.find(event.touchId);
            if (it != touches_.end()) {
                touchMotion(it->second,event);
                updateGesture(false);
            }
            break;
        }
        case EventType::TouchEnd:
        case EventType::TouchCancel: {
            const auto it = touches_.find(event.touchId);
            if (it != touches_.end()) {
                if (event.type == EventType::TouchEnd) touchMotion(it->second,event);
                input_.setControl({Device::Touch, 0, it->second.slot}, 0);
                touches_.erase(it);
                updateGesture(true);
            }
            break;
        }
        case EventType::FocusLost:
            focusLost();
            break;
        default:
            break; // IME/Char/text, UI and OS lifecycle events are not gameplay keys.
        }
    }

    // Call once per presentation frame. Dead gamepads send zero levels to release held actions.
    // Do not poll a gamepad backend twice in a single frame when another host owns polling.
    void pollGamepads() {
        using namespace lib::gameplay::inputflow;
        ::aiverse::platform::pollGamepads();
        for (int pad = 0; pad < kMaxGamepads; ++pad) {
            const auto& state = ::aiverse::platform::gamepad(pad);
            for (int button = 0; button < int(PadButton::Count); ++button) {
                const float value = state.connected && state.down(PadButton(button)) ? 1.0f : 0.0f;
                input_.setControl({Device::GamepadButton, button, pad}, value);
            }
            for (int axis = 0; axis < int(PadAxis::Count); ++axis) {
                const float value = state.connected ? state.axis(PadAxis(axis)) : 0.0f;
                input_.setControl({Device::GamepadAxis, axis, pad}, value);
            }
        }
    }

    // Per-device ownership/capture changes must not clear other local devices.
    // Canceled events for affected Actions are resolved at the next update().
    void cancelKeyboard() {
        using namespace lib::gameplay::inputflow;
        input_.cancelDevices({Device::Keyboard});
    }
    void cancelPointer() {
        using namespace lib::gameplay::inputflow;
        input_.cancelDevices({Device::MouseButton,Device::MouseAxis,Device::MouseWheel});
    }
    void cancelTouch() {
        using namespace lib::gameplay::inputflow;
        input_.cancelDevices({Device::Touch});
        touches_.clear();
        gestureActive_=false;
        gestureDistance_=gestureCenterX_=gestureCenterY_=gestureAngle_=0;
    }
    void focusLost() {
        input_.cancelAll();
        touches_.clear();
        gestureActive_=false;
        gestureDistance_=gestureCenterX_=gestureCenterY_=gestureAngle_=0;
    }
private:
    struct TouchEntry { int slot=0; float x=0,y=0; };
    void touchMotion(TouchEntry& touch,const Event& event) {
        using namespace lib::gameplay::inputflow;
        // Touch code 0 is finger-down; code 1/2 are per-frame relative motion.
        // Coalesced TouchMove events accumulate until InputFlow::update().
        const float dx=event.x-touch.x, dy=event.y-touch.y;
        if(dx!=0) input_.addDelta({Device::Touch,1,touch.slot},dx);
        if(dy!=0) input_.addDelta({Device::Touch,2,touch.slot},dy);
        touch.x=event.x;touch.y=event.y;
    }
    // Codes 3,4,5 on touch deviceIndex 0: pinch distance delta, two-finger
    // centroid X/Y delta. No jump when the second finger joins or leaves.
    void updateGesture(bool reset) {
        using namespace lib::gameplay::inputflow;
        if(touches_.size()!=2) { gestureActive_=false; return; }
        const auto& a=touches_.begin()->second;
        const auto& b=std::next(touches_.begin())->second;
        const float centerX=(a.x+b.x)*0.5f, centerY=(a.y+b.y)*0.5f;
        const float distance=std::hypot(a.x-b.x,a.y-b.y);
        const float angle=std::atan2(a.y-b.y,a.x-b.x);
        if(gestureActive_ && !reset) {
            const float pinch=distance-gestureDistance_;
            const float dx=centerX-gestureCenterX_,dy=centerY-gestureCenterY_;
            if(pinch!=0)input_.addDelta({Device::Touch,3,0},pinch);
            if(dx!=0)input_.addDelta({Device::Touch,4,0},dx);
            if(dy!=0)input_.addDelta({Device::Touch,5,0},dy);
            float twist=angle-gestureAngle_;
            constexpr float pi=3.14159265358979323846f;
            if(twist>pi)twist-=2.0f*pi;
            if(twist< -pi)twist+=2.0f*pi;
            if(twist!=0)input_.addDelta({Device::Touch,6,0},twist);
        }
        gestureDistance_=distance;
        gestureCenterX_=centerX;
        gestureCenterY_=centerY;
        gestureAngle_=angle;
        gestureActive_=true;
    }
    lib::gameplay::inputflow::InputFlow& input_;
    std::map<std::int64_t, TouchEntry> touches_;
    bool gestureActive_=false;
    float gestureDistance_=0,gestureCenterX_=0,gestureCenterY_=0,gestureAngle_=0;
};

// Source compatibility for code including the historical EnhancedInputAdapter.h.
using EnhancedInputAdapter = InputFlowAdapter;
} // namespace aiverse::platform
