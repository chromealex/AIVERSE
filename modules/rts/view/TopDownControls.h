// Top-down hero controls on the client (presentation only; the sim side is modules/rts/RtsTopDown.h).
//
// game.json "topDown" configures a floating or fixed virtual joystick (left), an optional aim stick (right), ability
// buttons, keyboard / mouse and gamepad equivalents and the follow camera. Every frame the host feeds the pointer
// events and the held keys / pad axes; compose() turns them into one camera-relative move vector + aim, quantized
// exactly as the sim re-quantizes it (rules topDown.input), and changed() says when a kCmdTopDown command is due
// (input is sticky in the sim: commands go out on change only). The camera follows the hero with smoothing,
// look-ahead along the stick and map bounds (aiverselib gameplay camera maths). draw() returns the sticks and buttons
// as screen-canvas triangles (ViewCanvas.h). Nothing here touches the simulation.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "ViewCanvas.h"
#include "../RtsTopDown.h"
#include "../../../engine/render/Math3D.h"

namespace aiverse::game {

struct Rules;

struct TopDownStickConfig {
    bool enabled = true;
    bool floating = true;            // floating: appears where the finger lands inside `area`; fixed: at `center`
    float radius = 64;               // logical points: full deflection
    float deadzone = 0.12f;          // fraction of the radius
    float area[4] = {0, 0.3f, 0.5f, 1};   // normalized screen rect x0 y0 x1 y1 that captures the finger
    float center[2] = {0.14f, 0.78f};     // fixed stick centre / idle drawing position (normalized)
};

struct TopDownButtonConfig {
    std::string ability;             // ability id of the hero type
    int abilityIndex = -1;           // resolved against the hero type
    int weaponStep = 0;              // "weapon": "next" (+1) / "prev" (-1): switches the weapon in hand (rules "arsenal")
    int key = 0;                     // platform::Key (0: none)
    int padButton = -1;              // host gamepad button index (-1: none)
    float anchor[2] = {1, 1};        // normalized screen point the offset starts from
    float offset[2] = {-110, -110};  // logical points
    float radius = 34;
};

struct TopDownCameraConfig {
    float pitch = 58, yaw = 0;       // degrees (yaw 0: north up)
    float distance = 20, minDistance = 10, maxDistance = 40;
    float smoothing = 6;             // 1/s: exponential follow response (0: locked to the hero)
    float lookAhead = 2.5f;          // world units ahead along the stick
    float aimLookAhead = 1.5f;       // world units ahead along the aim
    float lookAheadSmoothing = 3;    // 1/s
    bool lockOffsetIfNoInput = true; // keep the look-ahead offset while the stick is released (Drone Fall)
    // speed camera (GTA): the hero's speed (world units / s, from its on-screen motion: on foot or at the wheel)
    // pulls the camera back from `distance` (at speedFrom and slower) to speedDistance (at speedTo and faster) ...
    float speedFrom = 0, speedTo = 0;   // "speedZoom": {"from", "to", "distance", "smoothing"}; to <= from: off
    float speedDistance = 0;
    float speedSmoothing = 1.5f;     // 1/s
    // ... and shifts the focus ahead of the motion: velocity * velocityLookAhead seconds, at most maxLookAhead
    float velocityLookAhead = 0;     // seconds (0: off)
    float maxLookAhead = 0;          // world units for the whole look-ahead offset (0: no limit)
    bool bounds = true;              // keep the focus inside the map
    float margin = 0;                // world units kept from the map edge by the focus
};

// game.json topDown "aimAssist": the stick's aim bends toward the target nearest to it (client side: the sim gets an
// ordinary aim angle), per input source. Absent: off.
struct TopDownAimAssistConfig {
    bool touch = false, pad = false, mouse = false;   // which aim sources it helps (true as the block: touch + pad)
    float cone = 30;                 // degrees either side of the stick that a target may be off
    float range = 0;                 // world units (0: the hero's aim / weapon range)
    float stickiness = 1.4f;         // the locked target stays while inside cone * stickiness
    bool tapFire = true;             // a short tap on the aim stick (touch) shoots at the nearest target
    float tapTime = 0.25f;           // seconds: a press shorter than this with a small deflection is a tap
    float tapFireTime = 0.4f;        // seconds of fire a tap gives
    bool any() const { return touch || pad || mouse; }
};

struct TopDownControlConfig {
    bool enabled = false;
    enum class Touch { Auto, Always, Never } touch = Touch::Auto;   // touch sticks: on touch devices (auto)
    enum class MouseAim { Hold, Always, Off } mouseAim = MouseAim::Hold;   // aim at the cursor while LMB is held
    TopDownStickConfig move, aim;
    std::vector<TopDownButtonConfig> buttons;
    TopDownCameraConfig camera;
    TopDownAimAssistConfig assist;
    float padDeadzone = 0.15f;
};

// game.json "topDown" (absent: enabled = false). Ability ids resolve against `heroAbilities` (missing ids: -1).
TopDownControlConfig parseTopDownControls(const Json& gameJson, const std::vector<std::string>& heroAbilities);

class VirtualStick {
public:
    void configure(const TopDownStickConfig& c) { cfg_ = c; cancel(); }
    // A pointer press: captured when inside the area (or the fixed ring); returns true when taken.
    bool begin(int64_t id, float x, float y, float screenW, float screenH);
    bool move(int64_t id, float x, float y);
    bool end(int64_t id);                 // true when it was this stick's pointer
    void cancel() { active_ = false; pointer_ = -1; vx_ = vy_ = 0; }
    bool active() const { return active_; }
    int64_t pointer() const { return pointer_; }
    float rawDeflection() const;          // |offset| / radius of the held stick (0 when idle), before the dead zone
    // deflection in [-1, 1]^2 (screen axes: +y down), dead zone removed and rescaled; length <= 1
    void value(float& x, float& y) const;
    // where to draw: base centre and knob (logical points)
    void geometry(float screenW, float screenH, float& bx, float& by, float& kx, float& ky) const;
    const TopDownStickConfig& config() const { return cfg_; }

private:
    TopDownStickConfig cfg_;
    bool active_ = false;
    int64_t pointer_ = -1;
    float ox_ = 0, oy_ = 0, vx_ = 0, vy_ = 0;   // origin, raw offset (points)
};

// What the host read this frame besides pointer events.
struct TopDownFrameInput {
    bool up = false, down = false, left = false, right = false;   // keyboard (WASD / arrows)
    float padMoveX = 0, padMoveY = 0, padAimX = 0, padAimY = 0;   // gamepad sticks, screen axes (+y down)
    bool padFire = false, fireKey = false;
    bool mouseAim = false;           // cursor aim valid this frame (host: LMB held / always, cursor over the ground)
    float mouseDirX = 0, mouseDirZ = 0;   // world direction hero -> cursor ground point
    // aim assist (config().assist): what the hero could shoot, world offsets hero -> target (TopDown::aimCandidates)
    struct Target { uint64_t id = 0; float dx = 0, dz = 0; };
    std::vector<Target> targets;
    float time = 0;                  // seconds, monotonic: taps on the aim stick
};

class TopDownControls {
public:
    void configure(const Json& gameJson, const TopDownRules& rules, const Rules& rts);
    bool active() const { return cfg_.enabled && rules_.enabled; }
    const TopDownControlConfig& config() const { return cfg_; }
    bool touchUi(bool touchDevice) const {
        return cfg_.touch == TopDownControlConfig::Touch::Always || (cfg_.touch == TopDownControlConfig::Touch::Auto && touchDevice);
    }
    // pointer events in logical points: true = consumed by a stick / button
    bool pointerDown(int64_t id, float x, float y, float screenW, float screenH, bool touchUi);
    bool pointerMove(int64_t id, float x, float y);
    bool pointerUp(int64_t id, float x, float y);
    void cancelAll();
    void pressAbility(int button) { if (button >= 0 && size_t(button) < cfg_.buttons.size()) presses_.push_back(button); }
    std::vector<int> takeAbilityPresses() { std::vector<int> p; p.swap(presses_); return p; }
    int buttonAt(float x, float y, float screenW, float screenH) const;   // -1: none

    // The input of this frame, camera-relative (cameraYaw radians, RTSCamera convention), quantized like the sim.
    topdown::Input compose(const TopDownFrameInput& in, float cameraYaw);
    // true once per change: the host sends kCmdTopDown with topdown::pack(q) and calls sent(q)
    bool changed(const topdown::Input& q) const { return !haveSent_ || !(q == sent_); }
    void sent(const topdown::Input& q) { sent_ = q; haveSent_ = true; }
    void resetSent() { haveSent_ = false; }

    // aim assist: the target the aim is bent to / a tap shoots at (0: none), for a lock marker
    uint64_t assistTarget() const { return lockId_; }

    // Follow camera: heroPos / moving direction from the presentation (interpolated) state.
    void updateCamera(render::RTSCamera& cam, render::float3 heroPos, bool haveHero, float dt, float mapW, float mapH);
    void snapCamera(render::RTSCamera& cam, render::float3 heroPos, float mapW, float mapH);
    // false: the stick does not point where the hero goes (pedals of a vehicle), so the stick look-ahead is off and
    // only the velocity look-ahead moves the focus. The host sets it every frame before updateCamera.
    void setStickLookAhead(bool on) { stickLook_ = on; }
    float heroSpeed() const { return speed_; }   // smoothed, world units / s (speed camera)

    // Screen-canvas triangles of the sticks and buttons; ready[i] in [0, 1] = ability button i charge (1 ready).
    std::vector<ViewDrawBatch> draw(float screenW, float screenH, const std::vector<float>& ready) const;
    // A lock marker (aim assist) around a screen point; `radius` in logical points, `time` animates it.
    static ViewDrawBatch drawLock(float x, float y, float radius, float time);

private:
    void buttonCenter(size_t i, float screenW, float screenH, float& x, float& y) const;
    TopDownControlConfig cfg_;
    TopDownRules rules_;
    VirtualStick move_, aim_;
    std::vector<int64_t> buttonPointers_;   // per button: pointer holding it (-1 none)
    std::vector<int> presses_;
    topdown::Input sent_;
    bool haveSent_ = false;
    float moveDirX_ = 0, moveDirZ_ = 0, aimDirX_ = 0, aimDirZ_ = 0;   // last composed world directions (camera look-ahead)
    bool aiming_ = false;
    float offX_ = 0, offZ_ = 0;      // current look-ahead offset
    bool camInit_ = false;
    bool stickLook_ = true;
    float lastX_ = 0, lastZ_ = 0, velX_ = 0, velZ_ = 0, speed_ = 0;   // speed camera: hero motion between frames
    float vOffX_ = 0, vOffZ_ = 0, zoomAdd_ = 0, baseZoom_ = 0, appliedZoom_ = 0;
    bool haveLast_ = false;
    // aim assist
    float clock_ = 0, aimDownAt_ = 0, aimMaxDefl_ = 0, tapUntil_ = 0;
    bool tapPending_ = false;
    uint64_t lockId_ = 0, tapId_ = 0;
};

} // namespace aiverse::game
