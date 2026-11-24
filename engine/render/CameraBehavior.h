// LENS camera behaviours shared by every view: the map camera, the follow camera, the chase camera, the
// first-person look, and the ECS camera rigs (CameraRig.h evaluates the same chase / look math).
//
// Bodies (where the camera stands):
//   overhead     - game.json "camera": a free map camera (CameraOverhead, applyOverheadCamera);
//   follow       - keeps a focus point in frame with smoothing, look-ahead, speed zoom and map bounds (CameraFollow);
//   chase        - behind and above a heading (chaseCamera);
//   first person - eyes with mouse look (CameraLook).
// Every block may name a preset (CameraPresets.h): {"preset": "follow-speed", "distance": 24}.
// Presentation only: nothing here reads or writes the simulation.
//
//   "camera": {"projection": "ortho", "pitch": 90, "yaw": 0, "fov": 43,
//              "zoom": 18, "zoomMin": 8, "zoomMax": 40, "pixelsPerCell": 32, "rotate": false}
// pitch/yaw/fov in degrees. Zoom values: visible map cells (vertically) for "ortho", camera distance for
// "perspective". "pixelsPerCell" (ortho) sets the start zoom so one cell is that many framebuffer pixels.
#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include "../data/Json.h"
#include "CameraPresets.h"
#include "Math3D.h"
#include "engine/gameplay/Camera.h"

namespace aiverse::render {

constexpr float kCameraDeg = 3.14159265358979f / 180.0f;

// ---- overhead (game.json "camera")
struct CameraOverhead {
    bool present = false, ortho = false, rotate = true;
    float pitch = 0.95f, yaw = 0, fovY = 0.75f;      // radians
    float zoom = -1, zoomMin = -1, zoomMax = -1;    // in the units above, -1 = keep
    float pixelsPerCell = 0;
};

inline CameraOverhead overheadCameraFrom(const Json& manifest) {
    CameraOverhead c;
    const Json j = resolveCameraPreset(manifest["camera"]);
    if (!j.isObject()) return c;
    auto num = [&](const char* k, float def) { return j[k].isNumber() ? float(j[k].fx().toDouble()) : def; };
    c.present = true;
    c.ortho = j.getStr("projection", "perspective") == "ortho";
    c.pitch = std::clamp(num("pitch", 54.4f), 5.0f, 90.0f) * kCameraDeg;
    c.yaw = num("yaw", 0) * kCameraDeg;
    c.fovY = std::clamp(num("fov", 43.0f), 5.0f, 120.0f) * kCameraDeg;
    c.zoom = num("zoom", -1); c.zoomMin = num("zoomMin", -1); c.zoomMax = num("zoomMax", -1);
    c.pixelsPerCell = num("pixelsPerCell", 0);
    c.rotate = j.getBool("rotate", !c.ortho);
    return c;
}

// Converts a zoom value of the block into Camera::distance.
inline float overheadDistanceFor(const CameraOverhead& c, float zoom) {
    return c.ortho ? zoom * 0.5f / std::tan(c.fovY * 0.5f) : zoom;
}

// Applies projection, angles and zoom limits. Sets the zoom when the block gives one (zoom or pixelsPerCell) and
// returns true; otherwise the caller's framing distance stays.
inline bool applyOverheadCamera(const CameraOverhead& c, Camera& cam, float viewHeightPx) {
    if (!c.present) return false;
    cam.ortho = c.ortho;
    cam.pitch = c.pitch;
    cam.yaw = c.yaw;
    cam.fovY = c.fovY;
    if (c.zoomMin > 0) cam.minDistance = overheadDistanceFor(c, c.zoomMin);
    if (c.zoomMax > 0) cam.maxDistance = overheadDistanceFor(c, c.zoomMax);
    float zoom = c.zoom;
    if (c.ortho && c.pixelsPerCell > 0 && viewHeightPx > 0) zoom = viewHeightPx / c.pixelsPerCell;
    if (zoom <= 0) { cam.distance = std::clamp(cam.distance, cam.minDistance, cam.maxDistance); return false; }
    cam.distance = std::clamp(overheadDistanceFor(c, zoom), cam.minDistance, std::max(cam.minDistance, cam.maxDistance));
    return true;
}

// ---- follow (topDown.camera, a map's "camera")
struct CameraFollowSettings {
    float pitch = 58, yaw = 0;       // degrees (yaw 0: north up)
    float distance = 20, minDistance = 10, maxDistance = 40;
    float smoothing = 6;             // 1/s: exponential follow response (0: locked to the focus)
    float lookAhead = 2.5f;          // world units ahead along the move direction
    float aimLookAhead = 1.5f;       // world units ahead along the aim
    float lookAheadSmoothing = 3;    // 1/s
    bool lockOffsetIfNoInput = true; // keep the look-ahead offset while there is no move input
    // speed zoom: the focus speed (world units / s, from its motion between frames) pulls the camera back from
    // `distance` (at speedFrom and slower) to speedDistance (at speedTo and faster) ...
    float speedFrom = 0, speedTo = 0;   // "speedZoom": {"from", "to", "distance", "smoothing"}; to <= from: off
    float speedDistance = 0;
    float speedSmoothing = 1.5f;     // 1/s
    // ... and shifts the focus ahead of the motion: velocity * velocityLookAhead seconds, at most maxLookAhead
    float velocityLookAhead = 0;     // seconds (0: off)
    float maxLookAhead = 0;          // world units for the whole look-ahead offset (0: no limit)
    bool bounds = true;              // keep the focus inside the map
    float margin = 0;                // world units kept from the map edge by the focus
    // lens: vertical field of view in degrees (0: keep the view's); near / far clip planes follow the distance
    float fov = 0;
    enum class Projection { Keep, Perspective, Ortho } projection = Projection::Keep;   // "projection": "ortho"
    bool fogFollow = true;           // with a fov: distance fog as a 43-degree camera framing the same view sees it
};

// Reads the keys of one follow block onto c (absent keys keep their value; "preset" first). finishCameraFollow
// clamps the result.
inline void readCameraFollow(const Json& block, CameraFollowSettings& cam) {
    const Json cj = resolveCameraPreset(block);
    if (!cj.isObject()) return;
    auto num = [&](const char* k, float& v) { v = float(cj[k].toDouble(v)); };
    num("pitch", cam.pitch); num("yaw", cam.yaw); num("distance", cam.distance);
    num("minDistance", cam.minDistance); num("maxDistance", cam.maxDistance);
    num("smoothing", cam.smoothing); num("lookAhead", cam.lookAhead); num("aimLookAhead", cam.aimLookAhead);
    num("lookAheadSmoothing", cam.lookAheadSmoothing); num("margin", cam.margin);
    num("velocityLookAhead", cam.velocityLookAhead); num("maxLookAhead", cam.maxLookAhead);
    num("fov", cam.fov);
    if (const Json& sz = cj["speedZoom"]; sz.isObject()) {
        cam.speedFrom = std::max(0.0f, float(sz["from"].toDouble(0)));
        cam.speedTo = float(sz["to"].toDouble(0));
        cam.speedDistance = float(sz["distance"].toDouble(cam.distance));
        cam.speedSmoothing = std::max(0.0f, float(sz["smoothing"].toDouble(cam.speedSmoothing)));
    }
    cam.lockOffsetIfNoInput = cj.getBool("lockOffsetIfNoInput", cam.lockOffsetIfNoInput);
    cam.bounds = cj.getBool("bounds", cam.bounds);
    cam.fogFollow = cj.getBool("fogFollow", cam.fogFollow);
    if (cj.has("projection")) {
        const std::string pr = cj.getStr("projection");
        cam.projection = pr == "ortho" ? CameraFollowSettings::Projection::Ortho
                       : pr == "perspective" ? CameraFollowSettings::Projection::Perspective
                                             : CameraFollowSettings::Projection::Keep;
    }
}

inline void finishCameraFollow(CameraFollowSettings& cam) {
    cam.velocityLookAhead = std::max(0.0f, cam.velocityLookAhead);
    cam.maxLookAhead = std::max(0.0f, cam.maxLookAhead);
    cam.pitch = std::clamp(cam.pitch, 5.0f, 90.0f);
    cam.yaw = std::remainder(cam.yaw, 360.0f);
    cam.fov = cam.fov > 0 ? std::clamp(cam.fov, 1.0f, 120.0f) : 0.0f;
    cam.minDistance = std::max(1.0f, std::min(cam.minDistance, cam.distance));
    cam.maxDistance = std::max(cam.maxDistance, cam.distance);
}

// Clip planes for a camera at this distance: near grows with it (depth precision far away), far stays past the view.
inline void cameraClipPlanes(Camera& cam) {
    cam.zNear = std::max(0.5f, cam.distance * 0.02f);    // 0.5 up to 25 units (the engine default), 6.4 at 320
    cam.zFar = std::max(1000.0f, cam.distance * 4.0f);
}

// Multiplier for the distance fog density (Renderer3D fog): 1 without a configured fov.
inline float cameraFogScale(const CameraFollowSettings& c) {
    if (!c.fogFollow || c.fov <= 0) return 1;
    // fog over the same ground as a 43-degree lens (the default) at the distance that frames it alike
    const float s = std::tan(c.fov * kCameraDeg * 0.5f) / std::tan(43.0f * kCameraDeg * 0.5f);
    return std::clamp(s, 0.02f, 4.0f);
}

// What the follow body sees this frame. move / aim: world ground directions (length <= 1).
struct CameraFollowInput {
    float3 focus{};
    bool haveFocus = true;
    float moveX = 0, moveZ = 0;
    bool aiming = false;
    float aimX = 0, aimZ = 0;
    bool moveLookAhead = true;   // false: the move input does not point where the focus goes (pedals), velocity only
    float mapW = 0, mapH = 0;    // map bounds (0: none) ...
    float mapX = 0, mapZ = 0;    // ... starting at this corner
    bool focusHeight = false;    // false: the camera aims at the ground plane (y 0); true: at the focus height
    float dt = 0;
};

class CameraFollow {
public:
    void reset() { *this = CameraFollow{}; }
    float speed() const { return speed_; }   // smoothed focus speed, world units / s

    void update(const CameraFollowSettings& c, const CameraFollowInput& in, Camera& cam) {
        namespace gc = lib::gameplay::camera;
        constexpr float kPi = 3.14159265358979f;
        const float dt = std::max(0.0f, in.dt);
        gc::Limits lim;
        if (c.bounds && in.mapW > 0 && in.mapH > 0) {
            lim.minX = in.mapX + std::min(c.margin, in.mapW * 0.5f); lim.maxX = in.mapX + std::max(in.mapW - c.margin, in.mapW * 0.5f);
            lim.minZ = in.mapZ + std::min(c.margin, in.mapH * 0.5f); lim.maxZ = in.mapZ + std::max(in.mapH - c.margin, in.mapH * 0.5f);
        }
        const float3 f = in.focus;
        // speed: the focus velocity from its motion between frames (a jump = respawn / teleport: no speed)
        const bool speedCam = c.speedTo > c.speedFrom || c.velocityLookAhead > 0;
        if (speedCam && in.haveFocus && in.dt > 0) {
            if (haveLast_) {
                float vx = (f.x - lastX_) / dt, vz = (f.z - lastZ_) / dt;
                if (std::hypot(f.x - lastX_, f.z - lastZ_) > 8) vx = vz = 0;
                const float k = 1 - std::exp(-10 * dt);   // frames are jittery: a short low-pass
                velX_ += (vx - velX_) * k; velZ_ += (vz - velZ_) * k;
            }
            lastX_ = f.x; lastZ_ = f.z; haveLast_ = true;
        } else if (!in.haveFocus || !speedCam) { haveLast_ = false; velX_ = velZ_ = 0; }
        speed_ = std::hypot(velX_, velZ_);
        if (c.speedTo > c.speedFrom) {
            float t = std::clamp((speed_ - c.speedFrom) / (c.speedTo - c.speedFrom), 0.0f, 1.0f);
            t = t * t * (3 - 2 * t);
            const float want = (c.speedDistance - c.distance) * t;
            const float k = c.speedSmoothing > 0 ? 1 - std::exp(-c.speedSmoothing * dt) : 1;
            zoomAdd_ += (want - zoomAdd_) * (init_ ? k : 1);
        } else zoomAdd_ = 0;
        const float addLo = std::min(0.0f, c.speedDistance - c.distance), addHi = std::max(0.0f, c.speedDistance - c.distance);
        lim.minZoom = std::max(1.0f, c.minDistance + (c.speedTo > c.speedFrom ? addLo : 0));
        lim.maxZoom = c.maxDistance + (c.speedTo > c.speedFrom ? addHi : 0);
        lim.minPitch = 5; lim.maxPitch = 90;
        // look-ahead: eases toward the move (and the aim); no input keeps the offset (lockOffsetIfNoInput)
        const bool look = in.moveLookAhead;
        const bool input = (look && (in.moveX != 0 || in.moveZ != 0)) || in.aiming;
        if (input || !c.lockOffsetIfNoInput || !look) {
            const float mx = look ? in.moveX : 0, mz = look ? in.moveZ : 0;
            const float tx = mx * c.lookAhead + (in.aiming ? in.aimX * c.aimLookAhead : 0);
            const float tz = mz * c.lookAhead + (in.aiming ? in.aimZ * c.aimLookAhead : 0);
            const float t = c.lookAheadSmoothing > 0 ? 1 - std::exp(-c.lookAheadSmoothing * dt) : 1;
            offX_ += (tx - offX_) * t; offZ_ += (tz - offZ_) * t;
        }
        gc::Pose current{cam.target.x, cam.target.z, cam.distance, cam.yaw * 180 / kPi, cam.pitch * 180 / kPi};
        gc::Pose target = current;
        if (c.velocityLookAhead > 0) {   // ahead of the motion (never locked: it follows the speed down to zero)
            const float t = c.lookAheadSmoothing > 0 ? 1 - std::exp(-c.lookAheadSmoothing * dt) : 1;
            vOffX_ += (velX_ * c.velocityLookAhead - vOffX_) * t; vOffZ_ += (velZ_ * c.velocityLookAhead - vOffZ_) * t;
        } else vOffX_ = vOffZ_ = 0;
        float ox = offX_ + vOffX_, oz = offZ_ + vOffZ_;
        if (const float ol = std::hypot(ox, oz); c.maxLookAhead > 0 && ol > c.maxLookAhead) { ox *= c.maxLookAhead / ol; oz *= c.maxLookAhead / ol; }
        if (in.haveFocus) { target.x = f.x + ox; target.z = f.z + oz; }
        target.yaw = c.yaw; target.pitch = c.pitch;
        // the player's own zoom (wheel / pinch) is what the camera has now minus the speed part; the speed part on top
        baseZoom_ = init_ ? std::clamp(baseZoom_ + (cam.distance - appliedZoom_), c.minDistance, c.maxDistance) : c.distance;
        target.zoom = baseZoom_ + zoomAdd_;
        if (!init_ || c.smoothing <= 0) { gc::constrain(target, lim); current = target; init_ = in.haveFocus; }
        else if (!gc::advance(current, target, lim, dt, c.smoothing)) { gc::constrain(target, lim); current = target; }
        cam.target = {current.x, in.focusHeight && in.haveFocus ? f.y : (in.focusHeight ? cam.target.y : 0.0f), current.z};
        cam.distance = appliedZoom_ = current.zoom;
        cam.minDistance = c.minDistance + zoomAdd_; cam.maxDistance = c.maxDistance + zoomAdd_;
        cam.yaw = current.yaw * kPi / 180;
        cam.pitch = current.pitch * kPi / 180;
        if (c.fov > 0) cam.fovY = c.fov * kCameraDeg;
        if (c.projection != CameraFollowSettings::Projection::Keep) cam.ortho = c.projection == CameraFollowSettings::Projection::Ortho;
        cameraClipPlanes(cam);
    }

private:
    float offX_ = 0, offZ_ = 0;      // look-ahead along the input
    bool init_ = false;
    float lastX_ = 0, lastZ_ = 0, velX_ = 0, velZ_ = 0, speed_ = 0;
    float vOffX_ = 0, vOffZ_ = 0, zoomAdd_ = 0, baseZoom_ = 0, appliedZoom_ = 0;
    bool haveLast_ = false;
};

// ---- chase: behind and above a heading. `pos` in local render space, `yawTurns` the heading in turns
// (0 faces +Z, a quarter turn faces +X).
struct CameraChaseSettings {
    float distance = 11, height = 1.4f, pitch = 0.26f, lead = 2.5f;   // pitch radians; lead: target ahead of pos
    float fovY = 1.05f, zNear = 0.1f, zFar = 1500;
};
inline CameraChaseSettings cameraChaseFrom(const Json& block) {
    CameraChaseSettings s;
    const Json j = resolveCameraPreset(block);
    if (!j.isObject()) return s;
    auto num = [&](const char* k, float v) { return float(j[k].toDouble(v)); };
    s.distance = std::max(0.1f, num("distance", s.distance));
    s.height = num("height", s.height);
    s.pitch = std::clamp(num("pitch", s.pitch / kCameraDeg), -80.0f, 89.0f) * kCameraDeg;
    s.lead = num("lead", s.lead);
    s.fovY = std::clamp(num("fov", s.fovY / kCameraDeg), 5.0f, 120.0f) * kCameraDeg;
    s.zNear = std::max(0.001f, num("near", s.zNear));
    s.zFar = std::max(s.zNear * 2, num("far", s.zFar));
    return s;
}
inline Camera chaseCamera(float3 pos, float yawTurns, const CameraChaseSettings& s = {}) {
    constexpr float tau = 6.28318530718f;
    const float3 forward{std::sin(yawTurns * tau), 0, std::cos(yawTurns * tau)};
    Camera c;
    c.target = pos + forward * s.lead + float3{0, s.height, 0};
    c.distance = s.distance; c.pitch = s.pitch; c.yaw = yawTurns * tau + tau * 0.5f;
    c.fovY = s.fovY; c.zNear = s.zNear; c.zFar = s.zFar;
    return c;
}
// Shortest-arc interpolation of a heading in turns (between two snapshots).
inline float lerpTurns(float a, float b, float t) { float d = std::fmod(b - a + 1.5f, 1.0f) - 0.5f; return a + d * t; }

// ---- first person: eyes with mouse look (yaw / pitch in turns)
struct CameraLookSettings {
    float sensitivity = 0.0004f;     // turns per pixel
    float aimSensitivity = 0.00015f; // while aiming down sights
    float pitchLimit = 0.248f;       // turns either side of the horizon
    float fovY = 1.22f, zNear = 0.03f, zFar = 1000;
    bool invertY = false;
};
inline CameraLookSettings cameraLookFrom(const Json& block) {
    CameraLookSettings s;
    const Json j = resolveCameraPreset(block);
    if (!j.isObject()) return s;
    auto num = [&](const char* k, float v) { return float(j[k].toDouble(v)); };
    s.sensitivity = std::max(0.0f, num("sensitivity", s.sensitivity));
    s.aimSensitivity = std::max(0.0f, num("aimSensitivity", s.aimSensitivity));
    s.pitchLimit = std::clamp(num("pitchLimit", s.pitchLimit * 360), 1.0f, 89.9f) / 360;
    s.fovY = std::clamp(num("fov", s.fovY / kCameraDeg), 5.0f, 120.0f) * kCameraDeg;
    s.zNear = std::max(0.001f, num("near", s.zNear));
    s.zFar = std::max(s.zNear * 2, num("far", s.zFar));
    s.invertY = j.getBool("invertY", s.invertY);
    return s;
}
struct CameraLook {
    float yaw = 0, pitch = 0;        // turns
    CameraLookSettings settings{};
    void motion(float dx, float dy, bool aiming = false) {
        const float sens = aiming ? settings.aimSensitivity : settings.sensitivity;
        if (!std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(sens) || sens < 0) return;
        // lookAt uses right = cross(forward, up): facing +Z, screen-right is -X.
        yaw = std::fmod(yaw - dx * sens, 1.0f); if (yaw < 0) yaw += 1.0f;
        pitch = std::clamp(pitch + (settings.invertY ? dy : -dy) * sens, -settings.pitchLimit, settings.pitchLimit);
    }
    Camera camera(float3 eye) const {
        constexpr float tau = 6.28318530718f;
        const float cp = std::cos(pitch * tau);
        const float3 forward{std::sin(yaw * tau) * cp, std::sin(pitch * tau), std::cos(yaw * tau) * cp};
        Camera result; result.distance = 1; result.pitch = -pitch * tau; result.yaw = yaw * tau + tau * 0.5f;
        result.target = eye + forward; result.fovY = settings.fovY; result.zNear = settings.zNear; result.zFar = settings.zFar;
        return result;
    }
};

} // namespace aiverse::render
