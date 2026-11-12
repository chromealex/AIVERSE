// Gamepad play for any game (engine/platform/Gamepad.h): a virtual cursor on the left stick that
// clicks, drags and right-clicks like a mouse (so menus, UI documents, selection and orders work
// unchanged), camera pan on the right stick, zoom on the triggers, buttons bound to keys / mouse
// buttons in game.json. The cursor can snap to objects (Bunker Wars / Mushroom Wars pads jump
// between buildings): a flick of the stick moves it to the nearest target in that direction, and a
// slow cursor is pulled onto a target under it.
//
//   "gamepad": { "deadZone": 0.2, "cursorSpeed": 900, "panSpeed": 1, "zoomSpeed": 1,
//                "snap": { "targets": "structures", "radius": 70, "flick": true },   // "units", "all", "none"
//                "buttons": { "a": "click", "b": "rightClick", "x": "key:Q", "y": "key:E",
//                             "lb": "key:Num1", "rb": "key:Num4", "start": "key:Escape", "back": "key:Tab",
//                             "up": "pan", "down": "pan", "left": "pan", "right": "pan" } }
//
// Presentation / input only: everything becomes the same platform events as mouse and keyboard.
#pragma once
#include <functional>
#include <string>
#include <vector>
#include "engine/data/Json.h"
#include "engine/platform/Gamepad.h"
#include "engine/platform/Platform.h"

namespace aiverse::client {

class ClientGamepad {
public:
    struct Target { float x, y; };   // screen points of snap candidates
    struct Frame {
        std::vector<platform::Event> events;   // feed to GameClient::event in order
        float panX = 0, panY = 0;              // -1..1 camera pan (right / down)
        float zoom = 0;                        // wheel steps this frame (+ = in)
    };

    void configure(const Json& manifest);
    // targets: snap candidates for this frame (empty = no snapping).
    Frame update(const platform::GamepadState& pad, float dt, float width, float height, const std::vector<Target>& targets);
    bool active() const { return active_; }   // a pad was used recently: draw the cursor, hide the mouse one
    float cursorX() const { return x_; }
    float cursorY() const { return y_; }
    const std::string& snapTargets() const { return snapTargets_; }
    // A real mouse / touch was used: hide the pad cursor until the pad is touched again (it continues from there).
    void mouseUsed(float x, float y) { active_ = false; x_ = x; y_ = y; }

    struct Binding { enum Kind { None, Click, RightClick, MiddleClick, Key, Pan } kind = None; platform::Key key = platform::Key::Unknown; };
    static Binding parseBinding(const std::string& s);
    static platform::Key keyFromName(const std::string& s);

private:
    float deadZone_ = 0.2f, speed_ = 900, panSpeed_ = 1, zoomSpeed_ = 1;
    std::string snapTargets_ = "structures";
    float snapRadius_ = 70;
    bool flick_ = true, enabled_ = true;
    Binding bindings_[int(platform::PadButton::Count)];
    float x_ = -1, y_ = -1;
    bool active_ = false, flicked_ = false;
};

} // namespace aiverse::client
