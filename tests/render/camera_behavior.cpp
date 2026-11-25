// LENS camera behaviours: presets, the overhead / follow / chase / first-person bodies and the ECS rig modes that
// run the same chase / look math. CPU only.
#include "engine/render/CameraEdgeScroll.h"
#include "engine/render/CameraRig.h"
#include "engine/sim/CameraSceneAsset.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>
using namespace aiverse;
static void check(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
static bool near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }
static bool near(render::float3 a, render::float3 b, float e = 1e-3f) { return near(a.x, b.x, e) && near(a.y, b.y, e) && near(a.z, b.z, e); }

static void edgeScrollCases() {
    render::EdgeScrollPointer pointer;
    const auto defaults = render::CameraEdgeScroll::fromManifest(Json::object());
    auto sample = [&](const render::CameraEdgeScroll& settings, float x, float y, float dt) {
        return pointer.sample(settings, x, y, 100, 100, dt, true);
    };
    check(sample(defaults, 2, 50, .01f).x == -1, "legacy left edge");
    check(sample(defaults, 3, 50, .01f).x == 0, "legacy strict three pixel boundary");
    const auto custom = render::CameraEdgeScroll::fromManifest(Json::parse(
        R"({"edgeScroll":{"zonePx":16,"delaySeconds":0.25,"speed":2}})"));
    check(sample(custom, 90, 50, .125f).x == 0, "right edge dwell");
    auto corner = sample(custom, 90, 10, .25f);
    check(near(corner.x, 1) && corner.y == 0, "partial activation frame and independent corner dwell");
    corner = sample(custom, 90, 10, .125f);
    check(near(corner.x, 2) && near(corner.y, 2), "custom diagonal speed");
    check(sample(custom, 101, 10, .5f).y == 0, "outside window suppresses both axes");
    check(sample(custom, 90, 10, .125f).x == 0, "reentry requires fresh dwell");
    pointer.sample(custom, 90, 10, 100, 100, .5f, false);
    check(sample(custom, 90, 10, .125f).y == 0, "disabled input clears dwell");
    check(sample({0,0,1}, 0, 0, 1).x == 0, "zero zone disables scrolling");
}

static void designAspectCases() {
    const auto config = render::overheadCameraFrom(Json::parse(R"({"camera":{"designAspect":1.333333}})"));
    render::Camera camera;
    render::applyOverheadCamera(config, camera, 900);
    constexpr float design = 4.f / 3.f, wide = 16.f / 9.f;
    for (const bool ortho : {false, true}) {
        camera.ortho = ortho;
        render::Camera reference = camera;
        reference.designAspect = 0;
        for (const bool zeroToOne : {false, true}) {
            const auto expected = reference.proj(design, zeroToOne);
            check(near(camera.proj(wide, zeroToOne).at(0,0), expected.at(0,0)), "wide fixed design projection");
            check(near(camera.proj(.75f, zeroToOne).at(0,0), expected.at(0,0)), "narrow fixed design projection");
            check(!near(reference.proj(wide, zeroToOne).at(0,0), expected.at(0,0)), "default follows viewport");
        }
        render::float3 hit, expectedHit;
        check(camera.screenToGround(1200, 450, 1600, 900, hit) &&
              reference.screenToGround(900, 450, 1200, 900, expectedHit) &&
              near(hit, expectedHit, .002f), "pointer picks equivalent point of stretched design frame");
    }
    camera.ortho = false;
    check(camera.setFrustumTangents(-.75f, 1.f, -.8f, .9f), "asymmetric XR frustum");
    const auto xr = camera.proj(wide, true);
    camera.designAspect = 0;
    check(near(xr.at(0,0), camera.proj(wide, true).at(0,0)), "explicit XR frustum takes precedence");
}

int main() {
    designAspectCases();
    edgeScrollCases();
    // presets: every one parses and the block's own keys win
    check(render::cameraPresetNames().size() >= 8, "preset table");
    std::string error;
    Json block = render::resolveCameraPreset(Json::parse(R"({"preset": "follow-speed", "distance": 24})"), &error);
    check(error.empty() && block["distance"].toDouble() == 24 && block["speedZoom"].isObject(), "preset + own keys");
    render::resolveCameraPreset(Json::parse(R"({"preset": "nope"})"), &error);
    check(error.find("nope") != std::string::npos, "unknown preset reported");

    // overhead: a preset gives the same camera as its keys written out
    const auto pixel = render::overheadCameraFrom(Json::parse(R"({"camera": {"preset": "overhead-pixel"}})"));
    const auto spelled = render::overheadCameraFrom(Json::parse(R"({"camera": {"projection": "ortho", "pitch": 90, "yaw": 0, "pixelsPerCell": 32, "rotate": false}})"));
    check(pixel.present && pixel.ortho && !pixel.rotate && near(pixel.pitch, spelled.pitch) && pixel.pixelsPerCell == 32, "overhead preset");
    render::Camera cam;
    check(render::applyOverheadCamera(pixel, cam, 640) && cam.ortho, "pixel-perfect zoom applied");
    check(near(cam.distance, render::overheadDistanceFor(pixel, 20)), "640 px / 32 px per cell = 20 cells");

    // follow: preset values land in the settings; the body keeps the focus inside the map
    render::CameraFollowSettings fs;
    render::readCameraFollow(Json::parse(R"({"preset": "follow-speed", "smoothing": 0})"), fs);
    render::finishCameraFollow(fs);
    check(fs.pitch == 90 && fs.speedTo > fs.speedFrom && fs.velocityLookAhead > 0 && fs.smoothing == 0, "follow-speed preset");
    render::CameraFollow follow;
    render::CameraFollowInput in; in.focus = {50, 0, 50}; in.mapW = 100; in.mapH = 100; in.dt = 0;
    render::Camera fc;
    follow.update(fs, in, fc);
    check(near(fc.target.x, 50) && near(fc.target.z, 50) && near(fc.distance, fs.distance), "snap to the focus");
    // driving east fast: speed zoom pulls back and the velocity look-ahead leads the motion
    for (int i = 1; i <= 120; ++i) { in.focus = {50 + i * 0.3f, 0, 50}; in.dt = 1.f / 60; follow.update(fs, in, fc); }
    check(follow.speed() > 15 && fc.distance > fs.distance + 2 && fc.target.x > in.focus.x + 1, "speed zoom + lead");
    in.focus = {-30, 0, 50}; in.dt = 0; follow.reset(); follow.update(fs, in, fc);
    check(fc.target.x >= 0, "focus kept inside the map");

    // chase: the defaults are the old fixed camera; a block reads in degrees
    const auto chase = render::chaseCamera({1, 0, 2}, 0.25f);
    check(near(chase.distance, 11) && near(chase.pitch, 0.26f) && near(chase.target, {3.5f, 1.4f, 2}), "chase defaults");
    const auto low = render::cameraChaseFrom(Json::parse(R"({"preset": "chase-low"})"));
    check(near(low.distance, 7) && near(low.pitch, 8 * render::kCameraDeg), "chase-low preset");

    // first person: sensitivity and limits from data
    render::CameraLook look{0, 0, render::cameraLookFrom(Json::parse(R"({"preset": "first-person", "sensitivity": 0.001})"))};
    look.motion(0, -10000);
    check(near(look.pitch, look.settings.pitchLimit), "pitch limit");
    look = {0, 0, look.settings}; look.motion(10, 0); const float hip = look.yaw;
    look = {0, 0, look.settings}; look.motion(10, 0, true);
    check(std::fabs(look.yaw - 1) < std::fabs(hip - 1), "aiming turns slower");

    // ECS: Chase / FirstPerson rig modes follow the target's heading with the same math
    World world;
    auto car = world.create(), view = world.create();
    LocalTransform at{}; at.position = {10_fx, 0_fx, 5_fx};
    const float s = std::sin(3.14159265f / 4);   // facing +X: a quarter turn about Y
    at.rotation = {0_fx, Fixed::fromRaw(int64_t(s * 65536)), 0_fx, Fixed::fromRaw(int64_t(s * 65536))};
    world.add<WorldTransform>(car, hierarchy_detail::root(at));
    world.add<WorldTransform>(view, hierarchy_detail::root({}));
    world.add<CameraComponent>(view, CameraComponent{});
    CameraRigComponent rig; rig.mode = CameraRigMode::Chase; rig.target = car; rig.smoothing = 0_fx;
    rig.distance = 11_fx; rig.pitch = Fixed::fromRatio(26, 100); rig.offset = {0_fx, Fixed::fromRatio(14, 10), Fixed::fromRatio(5, 2)};
    world.add<CameraRigComponent>(view, rig);
    render::EcsCameraView snap;
    check(render::resolveEcsCamera(world, view, snap), "ecs camera");
    render::CameraRigEvaluator rigs;
    check(rigs.apply(world, snap, 0, 0.016f), "chase rig");
    const auto want = render::chaseCamera({10, 0, 5}, 0.25f);
    check(near(snap.camera.eye(), want.eye(), 0.02f), "ecs chase = view chase");
    auto& r = world.get<CameraRigComponent>(view);
    r.mode = CameraRigMode::FirstPerson; r.offset = {0_fx, Fixed::fromRatio(17, 10), 0_fx}; r.yaw = 0_fx; r.pitch = 0_fx;
    rigs.reset();
    check(rigs.apply(world, snap, 0, 0.016f), "first-person rig");
    check(near(snap.camera.eye(), {10, 1.7f, 5}, 0.02f), "eye at the target");
    check(near(snap.camera.rotation.rotate({0, 0, -1}), {1, 0, 0}, 0.02f), "looks along the target's heading");
    // ECS follow body: a Follow rig with CameraFollowComponent frames its target like the game follow camera
    {
        World w;
        auto hero = w.create(), cam = w.create();
        LocalTransform hp{}; hp.position = {50_fx, 0_fx, 40_fx};
        w.add<WorldTransform>(hero, hierarchy_detail::root(hp));
        w.add<WorldTransform>(cam, hierarchy_detail::root({}));
        w.add<CameraComponent>(cam, CameraComponent{});
        CameraRigComponent fr; fr.mode = CameraRigMode::Follow; fr.target = hero;
        w.add<CameraRigComponent>(cam, fr);
        CameraFollowComponent follow;
        std::string ferr;
        check(cameraFollowFromJson(Json::parse(R"({"preset": "follow-speed", "smoothing": 0, "bounds": [0, 0, 100, 80]})"), follow, &ferr), "follow preset block");
        check(follow.pitch == 90_fx && follow.bounds && follow.velocityLookAhead > 0_fx, "follow preset values");
        w.add<CameraFollowComponent>(cam, follow);
        render::EcsCameraView fv;
        check(render::resolveEcsCamera(w, cam, fv), "follow camera");
        render::CameraRigEvaluator fe;
        check(fe.apply(w, fv, 0, 0.016f), "follow rig");
        check(near(fv.camera.eye().x, 50, 0.05f) && near(fv.camera.eye().z, 40, 0.05f) && fv.camera.eye().y > 10, "straight down over the hero");
        // running east: the camera leads the motion
        for (int i = 1; i <= 90; ++i) {
            w.get<WorldTransform>(hero) = hierarchy_detail::root(LocalTransform{{Fixed::fromInt(50 + i / 3), 0_fx, 40_fx}, {}, {1_fx, 1_fx, 1_fx}});
            check(fe.apply(w, fv, i / 60.0, 1.f / 60), "follow step");
        }
        check(fv.camera.eye().x > 80.5f, "velocity lead ahead of the hero");
        // pushed past the bounds: the focus stays inside
        w.get<WorldTransform>(hero) = hierarchy_detail::root(LocalTransform{{500_fx, 0_fx, 40_fx}, {}, {1_fx, 1_fx, 1_fx}});
        fe.reset();
        check(fe.apply(w, fv, 2, 0.016f) && fv.camera.eye().x <= 100.5f, "follow bounds");
        // cameras.json round trip keeps the follow block
        const Json saved = cameraSceneToJson(w);
        check(saved["cameras"][size_t(0)]["follow"]["bounds"].size() == 4, "follow block saved");
        check(!cameraFollowFromJson(Json::parse(R"({"preset": "nope"})"), follow, &ferr) && !ferr.empty(), "unknown follow preset");
    }
    std::puts("camera_behavior: OK");
    return 0;
}
