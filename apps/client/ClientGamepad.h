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
//                             "up": "pan", "down": "pan", "left": "pan", "right": "pan" },
//                "chords": { "rb+a": "send:50", "lb+a": "upgrade", "a+a": "upgrade" } }      // modifier + button, or a double press
//
// Game actions (buttons or chords, run by GameClient at the cursor): "send:N" (N % of the garrison of the
// structure under the cursor when the modifier went down, or of the selected ones, to the structure under the
// cursor), "ability:N" (the N-th usable player ability, 1-based), "upgrade" (the structure under the cursor, or
// the selected one, to its next level: the first `upgrades` entry sharing its tags), "convert:<tag>" (the first
// `upgrades` entry with that tag), "cancel" (Escape); any other "event:<name>" goes to the view scripts as
// ui.on("pad", fn({action = name})). "lt" / "rt" name the triggers as buttons (a trigger used in a chord no
// longer zooms). "scheme": "bunkerwars" = Bunker Wars' pad layout (ME.TexturesGenerator/_DatabaseData/GameInfo/
// Input/InputConfig.asset): RB + B / A / X / Y send 25 / 50 / 75 / 100 %, LB + A upgrade, LB + X / Y / B convert
// to bunker / tower / armory, A double press upgrade, D-pad right / down / left / up = skill slots 1-4, LT + B / A /
// X / Y emotions 1-4 (event:emotion1..4); keys given in "buttons" / "chords" override the scheme.
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
    struct Action { std::string what; float x = 0, y = 0, originX = 0, originY = 0; };
    struct Frame {
        std::vector<platform::Event> events;   // feed to GameClient::event in order
        std::vector<Action> actions;           // game actions (after the events)
        bool aiming = false;                   // a send modifier is held: origin -> cursor arrow
        float originX = 0, originY = 0;
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

    struct Binding {
        enum Kind { None, Click, RightClick, MiddleClick, Key, Pan, Action } kind = None;
        platform::Key key = platform::Key::Unknown;
        std::string action;   // Action: "send:50", "ability:1", "upgrade", "convert:tower", "cancel", "event:x"
    };
    // button slots: platform::PadButton, then the triggers as buttons
    static constexpr int kLT = int(platform::PadButton::Count), kRT = kLT + 1, kSlots = kLT + 2;
    static int slotFromName(const std::string& s);   // -1: unknown
    static Binding parseBinding(const std::string& s);
    static platform::Key keyFromName(const std::string& s);

private:
    float deadZone_ = 0.2f, speed_ = 900, panSpeed_ = 1, zoomSpeed_ = 1;
    std::string snapTargets_ = "structures";
    float snapRadius_ = 70;
    bool flick_ = true, enabled_ = true;
    Binding bindings_[kSlots];
    struct Chord { int mod = -1, button = -1; Binding binding; };   // mod == button: double press
    std::vector<Chord> chords_;
    bool isMod_[kSlots] = {};
    bool swallowed_[kSlots] = {};      // pressed as part of a chord: its own binding stays quiet until released
    float lastPress_[kSlots] = {};     // seconds since the last press (double presses)
    bool trigPrev_[2] = {};            // triggers as buttons: last frame
    float originX_ = 0, originY_ = 0;
    bool aiming_ = false;
    float x_ = -1, y_ = -1;
    bool active_ = false, flicked_ = false;
};

} // namespace aiverse::client
