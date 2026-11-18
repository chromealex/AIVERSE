// Planet maps in the client: orbit camera input (mouse, keys, touch) on top of the regular RTS input.
// Flat maps never reach this file (GameClient::planet() is false without a map `world` block).
//   drag middle button / one-finger drag on empty ground: rotate over the surface (spin the globe)
//   wheel / pinch: zoom surface -> planet -> system;  Q / E (no selection) or two-finger twist: turn around
//   the local up;  arrows / WASD (no selection) / screen edges: move over the surface;  Tab: focus the next
//   body;  PageUp / PageDown: camera pitch;  Space: back to the base
#include "GameClient.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace aiverse::client {

using platform::Event;
using platform::EventType;
using platform::Key;

bool GameClient::objectScreen(const SceneObject& o, float lift, float2& sp) const {
    const float W = platform::windowWidth(), H = platform::windowHeight();
    if (!planet()) return cam_.worldToScreen({o.pos.x, lift, o.pos.z}, W, H, sp);
    return cam_.worldToScreen(view_.objectViewPos(o, lift), W, H, sp);
}

// Start over the player's base on the start body. AIVERSE_PLANET_CAM="distance[,pitch deg[,body id]]"
// overrides the start view (screenshots of the planet / system zoom levels).
Vec3 GameClient::simPoint(float3 g) {
    auto q = [](float v) { return Fixed::fromInt(int64_t(std::lround(double(v) * 256.0))) / 256; };
    return {q(g.x), q(g.y), q(g.z)};
}

void GameClient::fitPlanetCamera() {
    PlanetCamera& pc = view_.planetCamera();
    const PlanetWorld& W = view_.planets().world();
    int body = W.start;
    float3 dir{0, 0, 0};
    // own structure first, else any own unit (spectators: player 1)
    const uint8_t me = spectator_ ? 1 : human_;
    for (int pass = 0; pass < 2 && dot(dir, dir) == 0; ++pass)
        for (auto& o : scene_.objects)
            if (o.owner == me && o.body >= 0 && (pass == 1 || rules_.types[o.type].structure)) { body = o.body; dir = o.pos; break; }
    float dist = -1, pitchDeg = -1;
    char id[64] = {};
    if (const char* e = std::getenv("AIVERSE_PLANET_CAM")) {
        std::sscanf(e, "%f,%f,%63s", &dist, &pitchDeg, id);
        if (id[0] && W.find(id) >= 0) { // look at the first unit on that body (or the face 2 centre)
            body = W.find(id); dir = {0, 0, 0};
            int bestScore = -1;   // own units first, orbit units preferred
            for (auto& o : scene_.objects) {
                if (o.body != body || o.isProjectile || !o.visible) continue;
                const int score = (o.owner == me ? 2 : 0) + ((rules_.types[o.type].layer & kLayerOrbit) ? 1 : 0);
                if (score > bestScore) { bestScore = score; dir = o.pos; }
            }
        }
    }
    if (std::getenv("AIVERSE_PLANET_CAM_FIGHT")) // testing: look at a ground unit that is fighting
        for (auto& o : scene_.objects)
            if (o.body >= 0 && o.hasTarget && !o.isProjectile && !(rules_.types[o.type].layer & kLayerOrbit)) { body = o.body; dir = o.pos; break; }
    if (const char* e = std::getenv("AIVERSE_PLANET_CAM_SUN")) { // testing: turn the view towards the lit side (0..1)
        const int star = W.starOf(body);
        if (star >= 0 && star != body && dot(dir, dir) > 0) {
            const float3 sun = normalize(W.center(star, scene_.seconds) - W.center(body, scene_.seconds));
            const float t = std::clamp(float(std::atof(e)), -1.0f, 1.0f);   // < 0: towards the night side
            dir = normalize(normalize(dir) * (1.0f - std::fabs(t)) + sun * t) * length(dir);
        }
    }
    pc.reset(W, body, view_.planets().surface(body), scene_.seconds, dir);
    if (dist > 0) pc.distance = dist;
    if (pitchDeg > 0) pc.pitch = pitchDeg * 3.14159265f / 180.0f;
}

void GameClient::planetCameraKeys(double dt) {
    PlanetCamera& pc = view_.planetCamera();
    const float W=platform::windowWidth(),H=platform::windowHeight();
    const bool edges=!platform::isTouchDevice() &&
        std::string(platform::name())!="web" &&
        mouseX_>=0&&mouseY_>=0&&mouseX_<=W&&mouseY_<=H;
    const bool noSel=selection_.empty();
    if(ui_.textInputFocused())return;
    const float speed=std::max(pc.distance,6.0f)*0.9f*float(dt)*
                       (1.0f-0.6f*pc.planetBlend());
    const auto& controls=inputShortcuts_.system();
    const auto* arrow=controls.state("CameraPan");
    const auto* idle=controls.state("IdleCameraPan");
    if(arrow||idle){
        const float x=(arrow?arrow->value.x:0.0f)+(noSel&&idle?idle->value.x:0.0f)+
                      float(edges&&mouseX_>W-3)-float(edges&&mouseX_<3);
        const float y=(arrow?arrow->value.y:0.0f)+(noSel&&idle?idle->value.y:0.0f)+
                      float(edges&&mouseY_<3)-float(edges&&mouseY_>H-3);
        pc.move(std::clamp(x,-1.0f,1.0f)*speed,
                std::clamp(y,-1.0f,1.0f)*speed);
    } else {
        // Old complete project assets without semantic camera bindings.
        auto key=[&](Key k){return keys_[int(k)];};
        if(key(Key::Up)||(noSel&&key(Key::W))||(edges&&mouseY_<3))pc.move(0,speed);
        if(key(Key::Down)||(noSel&&key(Key::S))||(edges&&mouseY_>H-3))pc.move(0,-speed);
        if(key(Key::Right)||(noSel&&key(Key::D))||(edges&&mouseX_>W-3))pc.move(speed,0);
        if(key(Key::Left)||(noSel&&key(Key::A))||(edges&&mouseX_<3))pc.move(-speed,0);
    }
    if(const auto* rotation=controls.state("PlanetRotate")){
        if(noSel)pc.rotate(rotation->value.x*float(dt)*1.6f);
    }else{
        if(noSel&&keys_[int(Key::Q)])pc.rotate(float(dt)*1.6f);
        if(noSel&&keys_[int(Key::E)])pc.rotate(-float(dt)*1.6f);
    }
    if(const auto* pitch=controls.state("PlanetPitch"))
        pc.pitch=std::clamp(pc.pitch+pitch->value.x*float(dt),0.25f,1.55f);
    else{
        if(keys_[int(Key::PageUp)])pc.pitch=std::min(pc.pitch+float(dt),1.55f);
        if(keys_[int(Key::PageDown)])pc.pitch=std::max(pc.pitch-float(dt),0.25f);
    }
}

// Returns true when the event was consumed by the camera.
bool GameClient::planetCameraEvent(const Event& e, bool uiWants,
                                   const std::vector<std::string>& actions) {
    PlanetCamera& pc = view_.planetCamera();
    const float H = platform::windowHeight();
    switch (e.type) {
    case EventType::MouseDown:
        if (uiWants || e.button != 2) return false;
        downX_ = e.x; downY_ = e.y; panning_ = true;
        return true;
    case EventType::MouseMove:
        if (!panning_) return false;
        pc.pan(e.x - downX_, e.y - downY_, H);
        downX_ = e.x; downY_ = e.y;
        hover_ = kNullEntity;
        return true;
    case EventType::MouseUp:
        if (e.button != 2 || !panning_) return false;
        panning_ = false;
        return true;
    case EventType::MouseWheel: {
        if (uiWants) return true;
        const auto* wheel=inputShortcuts_.system().state("CameraZoom");
        const float value=wheel?wheel->value.x:e.wheelY;
        if(value!=0.0f)pc.zoom(std::pow(0.88f,value));
        return true;
    }
    case EventType::KeyDown: {
        if (uiWants) return false;
        const auto fired=[&](const char* name){
            return std::find(actions.begin(),actions.end(),name)!=actions.end();
        };
        if (fired("PlanetNextBody") ||
            (!inputShortcuts_.system().context("RTS.Planet") && e.key==Key::Tab)) {
            // Next planet with a surface; skip stars. Rebinding is supported.
            const auto& W = view_.planets().world();
            for (size_t k = 1; k <= W.bodies.size(); ++k) {
                const int b = int((size_t(pc.body()) + k) % W.bodies.size());
                if (W.bodies[size_t(b)].surface()) { pc.focus(b); break; }
            }
            return true;
        }
        if(fired("FocusCamera") ||
           (!inputShortcuts_.system().state("FocusCamera") && e.key==Key::Space)){
            fitCamera();return true;
        }
        return false;
    }
    case EventType::TouchMove: {
        if (!touches_.count(e.touchId)) return false;
        if (touches_.size() == 1 && !sendDrag_) {
            const auto prev = touches_[e.touchId];
            touches_[e.touchId] = {e.x, e.y};
            if (std::hypot(e.x - downX_, e.y - downY_) > 12) touchMoved_ = true;
            mouseX_ = e.x; mouseY_ = e.y;
            if(touchMoved_) {
                const auto* drag=inputShortcuts_.system().state("CameraTouchDrag");
                pc.pan(drag?drag->value.x:e.x-prev.first,
                       drag?drag->value.y:e.y-prev.second,H);
            }
            return true;
        }
        if (touches_.size() == 2) {
            auto it = touches_.begin();
            const auto a0 = it->second, b0 = std::next(it)->second;
            touches_[e.touchId] = {e.x, e.y};
            it = touches_.begin();
            const auto a = it->second, b = std::next(it)->second;
            const float d=std::hypot(a.first-b.first,a.second-b.second);
            const auto& input=inputShortcuts_.system();
            const auto* pinch=input.state("CameraPinch");
            const float delta=pinch?pinch->value.x:d-pinchDist_;
            if(pinchDist_>1&&d>1)
                pc.zoom(pinchDist_/std::max(1.0f,pinchDist_+delta));
            pinchDist_=d;
            const auto* twist=input.state("CameraTwist");
            if(twist)pc.rotate(twist->value.x);
            else {
                const float start=std::atan2(b0.second-a0.second,b0.first-a0.first);
                const float end=std::atan2(b.second-a.second,b.first-a.first);
                float da=end-start;
                if(da>3.14159f)da-=6.28318f;
                if(da< -3.14159f)da+=6.28318f;
                pc.rotate(da);
            }
            const float cx=(a.first+b.first)*0.5f,cy=(a.second+b.second)*0.5f;
            const auto* pan=input.state("CameraTouchPan");
            pc.pan(pan?pan->value.x:cx-downX_,
                   pan?pan->value.y:cy-downY_,H);
            downX_=cx;downY_=cy;
            touchMoved_ = true;
            return true;
        }
        return false;
    }
    default: return false;
    }
}

} // namespace aiverse::client
