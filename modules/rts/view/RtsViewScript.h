// View scripts (rules/map "viewScripts"): Lua for presentation on the local machine — floats allowed,
// read-only game queries (the same `game` table as simulation scripts, without actions), and requests
// the host turns into UI notices, particle effects and sounds. Never writes into the simulation.
// docs/scripting.md § View scripts.
#pragma once
#include <functional>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "../../../engine/data/Json.h"
#include "../RtsScript.h"
#include "../../../engine/render/PaintLayer.h"
#include "../../../engine/sim/Command.h"
#include "../../../engine/render/Math3D.h"
#include "ViewCanvas.h"
#include "ScriptSound.h"

namespace rts { class Simulation; }

namespace rts::game {

class RtsModule;

struct ViewTargeting {
    std::string ability;
    float x=0, z=0;
    bool active=false, overWorld=false;
    uint64_t caster=0; // Packed entity ID; zero for player abilities.
};

// view.camera() / view.set_camera{...}: the host's camera in the units of game.json "camera" — zoom = visible
// view-plane height in world units (ortho) or camera distance (perspective); pitch / yaw in degrees.
struct ViewCamera {
    float x = 0, z = 0, zoom = 0, pitch = 0, yaw = 0;
    int width = 0, height = 0;   // viewport in framebuffer pixels (read-only)
    bool ortho = false;          // read-only
    // view.to_screen: the host camera and the window size in points (the space of view.draw); view.set_camera
    // updates this copy at once, so scripts after the camera script project with the frame's final camera.
    render::RTSCamera view;
    float screenW = 0, screenH = 0;
};
// A set_camera request: fields left out of the call stay NaN (= keep the host's value).
struct ViewCameraRequest { float x, z, zoom, pitch, yaw; };

struct ViewRequest {
    enum Kind : uint8_t { Notice, Effect, Sound } kind = Notice;
    std::string text;          // notice text / effect or sound name
    float x = 0, z = 0, y = 0.5f;
    bool positioned = false;
};

// In-match UI from view scripts (G2): ui.set / ui.open / ui.close become requests for the host's data
// model "script" and documents ui/hud/<name>.rml (client: ClientScriptUi.cpp).
struct UiRequest {
    enum Kind : uint8_t { Set, Open, Close } kind = Set;
    std::string key;           // Set: model key; Open / Close: document name
    Json value;                // Set: the value; Open: params
};

// Cost of the view scripts in VM instructions (C functions they call are not counted). Every handler call has its own
// budget (5,000,000); a call over it is aborted and skipped, a handler over it 10 calls in a row or failing is disabled.
struct ViewScriptProfile {
    struct Handler {
        std::string name;                       // "file:line" of the function
        int64_t calls = 0, total = 0, max = 0;  // instructions: all calls, the most expensive call
        int64_t last = 0;                       // instructions in the last update (+ ui events after it)
        int overruns = 0, streak = 0;           // calls over budget: in all, in a row now
        bool disabled = false;
    };
    int64_t budget = 0;
    int64_t frames = 0, frameTotal = 0, frameMax = 0, frameLast = 0;   // update(): all handlers of a frame
    std::vector<Handler> handlers;              // in order of first call
};
class ViewScripts {
public:
    ViewScripts();
    ~ViewScripts();
    // rules must carry "$scriptSources" (embedScripts). False when the game has no view scripts.
    bool load(Simulation& sim, const Json& rules, const Json& map, uint8_t localPlayer);
    // Collects the simulation's events every tick (a read-only system, like the fx bridge).
    void attach(Simulation& sim);
    void detach();
    // Per frame: dispatches the collected events and on_frame(dt).
    void update(Simulation& sim, float dt);
    void setTargeting(const ViewTargeting& target);
    // The Lua `sound` table's host (voice handles, music; ScriptSound.h); null = silent no-ops.
    void setSound(ScriptSound* sound);
    void setCamera(const ViewCamera& camera);
    // Host screen state for view.screen(): "playing" (default), "paused", "over".
    void setScreen(const std::string& screen);
    // The last view.set_camera of this update (fields NaN = unchanged); false when there was none.
    bool takeCameraRequest(ViewCameraRequest& out);
    std::vector<ViewRequest> takeRequests();
    // view.send(id, args): script commands (kCmdScript) for the host to issue as the local player
    std::vector<Command> takeCommands();
    std::vector<UiRequest> takeUiRequests();
    // view.draw(...) of the last update (cleared when the next update starts)
    std::vector<ViewDrawBatch> takeDraws();
    // view.draw_ground(...) of the last update (world-space triangles, ViewCanvas.h)
    std::vector<ViewGroundBatch> takeGroundDraws();
    // From the UI: data-event-click="event('name', ...)" -> ui.on("name", fn(args)) handlers;
    // send('id', {...}) -> a script command like view.send (false: unknown id / bad args, see log)
    void uiEvent(Simulation& sim, const std::string& name, const Json& args);
    bool send(Simulation& sim, const std::string& id, const Json& args);
    ViewScriptProfile profile() const;
    const std::vector<ScriptLogEntry>& log() const { return log_; }
    // Called for every new log line (script errors, game.log) when set; testing hosts print them (RTS_SCRIPT_LOG).
    std::function<void(const ScriptLogEntry&)> onLog;
    bool active() const;
    std::shared_ptr<render::PaintLayers> paintLayers() const;

    struct Impl;
private:
    std::unique_ptr<Impl> impl_;
    std::vector<ScriptLogEntry> log_;
};

} // namespace rts::game
