// physics::Scene + physics::Character: handles, bodies, queries, events, JSON, controller, input packing.
// `aiverse_physics_scene_tests <scene.json>` runs the tests; add `--bench` for the 500 bodies + 64 characters timing.
#include "engine/physics/Character.h"
#include "engine/physics/Scene.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <iterator>
#include <iostream>
#include <limits>
#include <random>
#include <set>
#include <stdexcept>
#include <string>

using namespace aiverse;
using namespace aiverse::physics;

namespace {
int failures = 0;
std::string current;
void require(bool ok, const std::string& why) {
    if (!ok) throw std::runtime_error(why);
}
void test(const char* name, const std::function<void()>& fn) {
    current = name;
    try {
        fn();
        std::printf("  ok   %s\n", name);
    } catch (const std::exception& e) {
        ++failures;
        std::printf("  FAIL %s: %s\n", name, e.what());
    }
}
std::string fmt(b3Vec3 v) {
    char b[96];
    std::snprintf(b, sizeof b, "(%.4f, %.4f, %.4f)", v.x, v.y, v.z);
    return b;
}
float dist(b3Vec3 a, b3Vec3 b) { float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z; return std::sqrt(x * x + y * y + z * z); }

BodyDesc box(b3Vec3 pos, b3Vec3 size, Motion m = Motion::Dynamic) {
    BodyDesc d;
    d.motion = m;
    d.shape = ShapeKind::Box;
    d.position = pos;
    d.size = size;
    return d;
}
BodyHandle ground(Scene& s, float size = 200) { return s.create(box({0, -0.5f, 0}, {size, 1, size}, Motion::Static)); }

// One host tick: characters first (against the pre-step world), then pushes, proxies, physics.
struct Walker {
    CharacterParams params;
    CharacterState state;
    BodyHandle proxy = kNoBody;
    Pushes pushes;
};
void hostTick(Scene& s, std::vector<Walker*> walkers, const CharacterInput& in) {
    for (auto* w : walkers) {
        stepCharacter(s.world(), w->params, w->state, in, s.dt(), &w->pushes, w->proxy);
        s.applyPushes(w->pushes);
        if (w->proxy) s.moveCharacter(w->proxy, w->state);
    }
    s.step();
}
void settle(Scene& s, Walker& w, int ticks = 30) { for (int i = 0; i < ticks; ++i) hostTick(s, {&w}, {}); }
}

int main(int argc, char** argv) {
    std::string scenePath;
    bool bench = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--bench") bench = true;
        else scenePath = a;
    }

    test("handles: create, destroy, never reused while referenced", [] {
        Scene s({.maxBodies = 600});
        auto a = s.create(box({0, 1, 0}, {1, 1, 1}));
        require(a != kNoBody && s.valid(a) && s.bodyCount() == 1, "create");
        require(Scene::handleOf(s.native(a)) == a, "handleOf");
        require(s.destroy(a) && !s.valid(a) && !s.destroy(a) && s.bodyCount() == 0, "destroy");
        require(!s.setLinearVelocity(a, {1, 0, 0}) && s.position(a).y == 0, "dead handle is inert");
        std::set<BodyHandle> seen{a};
        for (int round = 0; round < 6; ++round) {
            std::vector<BodyHandle> hs;
            for (int i = 0; i < 300; ++i) {
                auto h = s.create(box({float(i), 1, 0}, {0.5f, 0.5f, 0.5f}));
                require(h != kNoBody, "create in round");
                require(seen.insert(h).second, "handle reused");
                hs.push_back(h);
            }
            for (auto h : hs) require(s.destroy(h), "destroy in round");
        }
        require(s.bodyCount() == 0, "all destroyed");
        Scene small({.maxBodies = 3});
        for (int i = 0; i < 3; ++i) require(small.create(box({0, float(i), 0}, {1, 1, 1})) != kNoBody, "within limit");
        require(small.create(box({0, 5, 0}, {1, 1, 1})) == kNoBody, "limit enforced");
        auto bad = box({std::numeric_limits<float>::quiet_NaN(), 0, 0}, {1, 1, 1});
        require(s.create(bad) == kNoBody, "NaN position rejected");
        require(s.create(box({0, 0, 0}, {0, 1, 1})) == kNoBody, "zero size rejected");
        BodyDesc mesh;
        mesh.shape = ShapeKind::Mesh;
        mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 0, 1}};
        mesh.indices = {0, 2, 1};
        require(s.create(mesh) == kNoBody, "dynamic mesh rejected");
        mesh.motion = Motion::Static;
        require(s.create(mesh) != kNoBody, "static mesh");
        mesh.indices = {0, 2, 7};
        require(s.create(mesh) == kNoBody, "bad mesh index rejected");
        auto u = s.create(box({0, 3, 0}, {1, 1, 1}));
        require(s.setUserData(u, 77) && s.userData(u) == 77, "user data");
    });

    test("bodies fall, rest on the ground and sleep", [] {
        Scene s;
        ground(s);
        auto b = s.create(box({0, 5, 0}, {1, 1, 1}));
        BodyDesc sd;
        sd.shape = ShapeKind::Sphere;
        sd.radius = 0.5f;
        sd.position = {3, 8, 0};
        auto sp = s.create(sd);
        BodyDesc cd;
        cd.shape = ShapeKind::Capsule;
        cd.radius = 0.3f;
        cd.halfHeight = 0.5f;
        cd.position = {-3, 4, 0};
        cd.rotation = b3MakeQuatFromAxisAngle({0, 0, 1}, 1.5707963f);
        auto cap = s.create(cd);
        for (int i = 0; i < 600; ++i) s.step();
        require(std::abs(s.position(b).y - 0.5f) < 0.03f, "box rest height " + fmt(s.position(b)));
        require(std::abs(s.position(sp).y - 0.5f) < 0.03f, "sphere rest height " + fmt(s.position(sp)));
        require(std::abs(s.position(cap).y - 0.3f) < 0.05f, "capsule rest height " + fmt(s.position(cap)));
        require(!s.awake(b), "box asleep");
        require(s.applyImpulse(b, {0, 500 * 1000.0f / 1000, 0}) && s.awake(b), "impulse wakes");
        float y = s.position(b).y;
        s.step();
        require(s.position(b).y > y, "impulse lifts");
        require(!s.applyImpulse(s.find("nothing"), {0, 1, 0}), "impulse on missing");
        auto k = s.create(box({0, 3, 5}, {1, 1, 1}, Motion::Kinematic));
        require(s.moveKinematic(k, {1, 3, 5}, b3Quat_identity), "kinematic target");
        s.step();
        require(dist(s.position(k), {1, 3, 5}) < 1e-3f, "kinematic reached target " + fmt(s.position(k)));
    });

    test("raycast and overlap return the expected handles", [] {
        Scene s;
        auto g = ground(s);
        auto a = s.create(box({0, 2, 0}, {1, 1, 1}, Motion::Static));
        auto zone = [&] { auto d = box({5, 1, 0}, {2, 2, 2}, Motion::Static); d.sensor = true; return s.create(d); }();
        auto hit = s.raycast({0, 10, 0}, {0, -20, 0});
        require(hit.body == a && std::abs(hit.point.y - 2.5f) < 1e-3f && hit.normal.y > 0.99f, "closest hits box");
        auto all = s.raycastAll({0, 10, 0}, {0, -20, 0});
        require(all.size() == 2 && all[0].body == a && all[1].body == g, "all hits, ordered");
        require(s.raycast({0, 10, 0}, {0, -20, 0}, {.ignore = a}).body == g, "ignore filter");
        require(s.raycast({0, 10, 0}, {0, -20, 0}, {.mask = kCategoryDynamic}).body == kNoBody, "mask filter");
        require(s.raycast({5, 10, 0}, {0, -20, 0}).body == g, "sensor skipped by rays");
        require(s.raycast({5, 10, 0}, {0, -20, 0}, {.sensors = true}).body == zone, "sensor on request");
        require(s.raycast({0, 10, 0}, {0, 0, 0}).body == kNoBody, "zero ray");
        auto o = s.overlapSphere({0, 2, 0}, 0.2f);
        require(o.size() == 1 && o[0] == a, "sphere overlap");
        o = s.overlapBox({0, 1, 0}, {0.6f, 1.2f, 0.6f}, b3Quat_identity);
        require(o.size() == 2 && std::count(o.begin(), o.end(), a) && std::count(o.begin(), o.end(), g), "box overlap");
        o = s.overlapCapsule({-3, 1.5f, 0}, {5, 1.5f, 0}, 0.2f);
        require(o.size() == 1 && o[0] == a, "capsule overlap (sensor skipped)");
        o = s.overlapCapsule({-3, 1.5f, 0}, {5, 1.5f, 0}, 0.2f, {.sensors = true});
        require(o.size() == 2, "capsule overlap with sensor");
        require(s.overlapSphere({0, 20, 0}, 0.5f).empty(), "empty overlap");
    });

    test("contact and sensor events", [] {
        Scene s;
        auto g = ground(s);
        auto b = s.create(box({0, 3, 0}, {1, 1, 1}));
        auto zd = box({4, 1, 4}, {2, 2, 2}, Motion::Static);
        zd.sensor = true;
        auto zone = s.create(zd);
        BodyDesc sd;
        sd.shape = ShapeKind::Sphere;
        sd.radius = 0.3f;
        sd.position = {4, 5, 4};
        auto ball = s.create(sd);
        bool boxHit = false, ballIn = false;
        for (int i = 0; i < 120; ++i) {
            s.step();
            for (auto& e : s.contactBegins())
                if ((e.a == b && e.b == g) || (e.a == g && e.b == b)) boxHit = boxHit || std::abs(e.point.y) < 0.1f;
            for (auto& e : s.sensorEvents()) if (e.sensor == zone && e.visitor == ball && e.begin) ballIn = true;
        }
        require(boxHit, "box/ground contact begin with a point on the ground");
        require(ballIn, "sensor begin for the ball");
        // A character proxy is seen by sensors but has no contacts with bodies.
        Walker w;
        w.state.position = {0, 0, 4.7f};
        w.proxy = s.createCharacter(w.params, w.state.position);
        bool walkerIn = false, walkerOut = false;
        for (int i = 0; i < 240; ++i) {
            CharacterInput in;
            in.yaw = -1.5707963f; // face +X
            in.moveZ = i < 60 ? 1.0f : (i < 120 ? 0.0f : -1.0f);
            hostTick(s, {&w}, in);
            for (auto& e : s.sensorEvents()) if (e.sensor == zone && e.visitor == w.proxy) (e.begin ? walkerIn : walkerOut) = true;
            for (auto& e : s.contactBegins()) require(e.a != w.proxy && e.b != w.proxy, "proxy must not touch bodies");
        }
        require(walkerIn && walkerOut, "sensor sees the character proxy enter and leave");
    });

    test("JSON scene: full, static-only copy and readable errors", [&] {
        require(!scenePath.empty(), "scene path argument required");
        auto json = Json::parseFile(scenePath);
        auto full = Scene::fromJson(json);
        auto stat = Scene::staticCopy(json);
        size_t statics = 0;
        for (auto& b : json["bodies"].items()) statics += b.getStr("motion", "dynamic") == "static";
        require(full->bodyCount() == json["bodies"].size() && stat->bodyCount() == statics && statics > 0, "body counts");
        require(stat->find("ground") != kNoBody && stat->find("ball") == kNoBody, "static copy contents");
        require(full->tickRate() == 60, "tick rate");
        auto ray = stat->raycast({10, 10, 7}, {0, -20, 0});
        require(ray.body == stat->find("ground"), "static copy collides");
        auto mesh = Json::parse(R"({"bodies":[{"id":"m","motion":"static","shape":"mesh",
            "vertices":[[-5,0,-5],[5,0,-5],[5,0,5],[-5,0,5]],"indices":[0,2,1,0,3,2]}]})");
        auto ms = Scene::fromJson(mesh);
        require(ms->raycast({1, 3, 1}, {0, -6, 0}).body == ms->find("m"), "mesh ray");
        struct Bad { const char* text; const char* message; };
        for (auto bad : {Bad{R"({"tickRate":60.5,"bodies":[]})", "integers"},
                         Bad{R"({"bodies":[{"id":"a","size":[0,1,1]}]})", "body 'a': box size"},
                         Bad{R"({"bodies":[{"id":"a"},{"id":"a"}]})", "body 'a': duplicate id"},
                         Bad{R"({"bodies":[{"id":"a","shape":"cone"}]})", "unsupported shape 'cone'"},
                         Bad{R"({"bodies":[{"id":"m","shape":"mesh","vertices":[[0,0,0],[1,0,0],[0,0,1]],"indices":[0,1,2]}]})", "mesh shape requires static"},
                         Bad{R"({"bodies":[{"id":"m","motion":"static","shape":"mesh","vertices":[[0,0,0],[1,0,0],[0,0,1]],"indices":[0,1,9]}]})", "index 2 out of range"},
                         Bad{R"({"bodies":[{"id":"a","motion":"floating"}]})", "unknown motion 'floating'"},
                         Bad{R"({"bodies":[],"joints":[{"a":"x","b":"y"}]})", "joints[0]"}}) {
            std::string error;
            try { Scene::fromJson(Json::parse(bad.text)); } catch (const JsonError& e) { error = e.what(); }
            require(error.find(bad.message) != std::string::npos, std::string("error for ") + bad.text + ": '" + error + "'");
        }
        Scene s;
        try { s.load(Json::parse(R"({"bodies":[{"id":"ok"},{"id":"bad","radius":-1,"shape":"sphere"}]})")); } catch (const JsonError&) {}
        require(s.bodyCount() == 0 && s.find("ok") == kNoBody, "failed load leaves nothing behind");
    });

    test("character walks at the expected speed, runs, turns and stops", [] {
        Scene s;
        ground(s);
        Walker w;
        settle(s, w);
        require(w.state.grounded && std::abs(w.state.position.y) < 0.01f, "grounded at rest " + fmt(w.state.position));
        CharacterInput in;
        in.moveZ = 1;
        for (int i = 0; i < 60; ++i) hostTick(s, {&w}, in);
        b3Vec3 a = w.state.position;
        for (int i = 0; i < 60; ++i) hostTick(s, {&w}, in);
        float speed = dist(a, w.state.position);
        require(std::abs(speed - w.params.walkSpeed) < 0.02f * w.params.walkSpeed, "walk speed " + std::to_string(speed));
        require(w.state.position.z < -4 && std::abs(w.state.position.x) < 1e-3f, "forward is -Z " + fmt(w.state.position));
        require(std::abs(w.state.position.y) < 0.01f && w.state.grounded, "stays on the ground");
        in.run = true;
        for (int i = 0; i < 30; ++i) hostTick(s, {&w}, in);
        a = w.state.position;
        for (int i = 0; i < 60; ++i) hostTick(s, {&w}, in);
        require(std::abs(dist(a, w.state.position) - w.params.runSpeed) < 0.02f * w.params.runSpeed, "run speed");
        in = {};
        in.moveZ = 1;
        in.yaw = 1.5707963f; // turned left: forward is -X
        for (int i = 0; i < 60; ++i) hostTick(s, {&w}, in);
        require(std::abs(w.state.velocity.x + w.params.walkSpeed) < 0.05f && std::abs(w.state.velocity.z) < 0.05f, "yaw turns the walk " + fmt(w.state.velocity));
        float brake = w.params.walkSpeed * w.params.walkSpeed / (2 * w.params.acceleration);
        a = w.state.position;
        for (int i = 0; i < 60; ++i) hostTick(s, {&w}, {});
        require(std::abs(w.state.velocity.x) < 1e-4f && std::abs(w.state.velocity.z) < 1e-4f, "stops");
        require(std::abs(dist(a, w.state.position) - brake) < 0.1f, "braking distance " + std::to_string(dist(a, w.state.position)));
    });

    test("character jumps to the expected height and lands", [] {
        Scene s;
        ground(s);
        Walker w;
        settle(s, w);
        CharacterInput jump;
        jump.jump = true;
        hostTick(s, {&w}, jump);
        require(!w.state.grounded, "airborne after the jump");
        float top = 0;
        int landed = -1;
        for (int i = 0; i < 120 && landed < 0; ++i) {
            hostTick(s, {&w}, {});
            top = std::max(top, w.state.position.y);
            if (w.state.grounded) landed = i;
        }
        float expected = w.params.jumpSpeed * w.params.jumpSpeed / (2 * w.params.gravity);
        require(std::abs(top - expected) < 0.06f * expected, "apex " + std::to_string(top) + " vs " + std::to_string(expected));
        require(landed > 0 && std::abs(w.state.position.y) < 0.01f && w.state.velocity.y == 0, "landed " + fmt(w.state.position));
        // Head against a ceiling: the jump stops under it.
        s.create(box({0, 1.8f + 0.4f + 0.5f, 0}, {4, 1, 4}, Motion::Static)); // ceiling bottom at 2.2
        hostTick(s, {&w}, jump);
        top = 0;
        for (int i = 0; i < 90; ++i) { hostTick(s, {&w}, {}); top = std::max(top, w.state.position.y); }
        require(top < 0.45f && top > 0.3f && w.state.grounded, "ceiling stops the jump at " + std::to_string(top));
    });

    test("character climbs a step up to stepHeight, not higher", [] {
        for (float h : {0.1f, 0.3f, 0.5f}) {
            Scene s;
            ground(s);
            s.create(box({0, h / 2, -4}, {4, h, 4}, Motion::Static)); // front face at z = -2
            Walker w;
            settle(s, w);
            CharacterInput in;
            in.moveZ = 1;
            for (int i = 0; i < 90; ++i) hostTick(s, {&w}, in);
            bool climbs = h <= w.params.stepHeight;
            if (climbs) require(std::abs(w.state.position.y - h) < 0.02f && w.state.position.z < -3 && w.state.grounded,
                                "climbs " + std::to_string(h) + ": " + fmt(w.state.position));
            else require(std::abs(w.state.position.y) < 0.02f && w.state.position.z > -2 + w.params.radius - 0.05f,
                         "blocked by " + std::to_string(h) + ": " + fmt(w.state.position));
            if (!climbs) continue;
            // Walking back off the ledge snaps down to the ground instead of flying off it.
            in.moveZ = -1;
            int air = 0;
            for (int i = 0; i < 120; ++i) { hostTick(s, {&w}, in); air += !w.state.grounded; }
            require(std::abs(w.state.position.y) < 0.02f && w.state.position.z > -1 && air <= 2, "steps down from " + std::to_string(h) + ": " + fmt(w.state.position) + " air " + std::to_string(air));
        }
        // stepHeight above the radius: the ledge is met by the side of the capsule, not its cap.
        for (float h : {0.4f, 0.6f}) {
            Scene s;
            ground(s);
            s.create(box({0, h / 2, -4}, {4, h, 4}, Motion::Static));
            Walker w;
            w.params.radius = 0.3f;
            w.params.stepHeight = 0.45f;
            settle(s, w);
            CharacterInput in;
            in.moveZ = 1;
            for (int i = 0; i < 90; ++i) hostTick(s, {&w}, in);
            bool climbs = h <= w.params.stepHeight;
            require(climbs ? std::abs(w.state.position.y - h) < 0.02f : std::abs(w.state.position.y) < 0.02f,
                    "tall step " + std::to_string(h) + ": " + fmt(w.state.position));
        }
    });

    test("character slides down steep slopes, stands on walkable ones", [] {
        for (float deg : {30.0f, 65.0f}) {
            Scene s;
            ground(s);
            float a = deg * 3.14159265f / 180;
            auto d = box({0, 6, 0}, {12, 1, 6}, Motion::Static);
            d.rotation = b3MakeQuatFromAxisAngle({0, 0, 1}, a); // rises towards +X
            s.create(d);
            Walker w;
            w.state.position = {0, 12, 0};
            int touched = -1;
            b3Vec3 p0{};
            for (int i = 0; i < 240; ++i) {
                hostTick(s, {&w}, {});
                if (touched < 0 && w.state.velocity.x != 0) { touched = i; p0 = w.state.position; }
                if (deg < 50 && w.state.grounded && touched < 0) { touched = i; p0 = w.state.position; }
            }
            b3Vec3 p1 = w.state.position;
            if (deg < 50) {
                require(w.state.grounded && touched > 0 && dist(p0, p1) < 0.01f && p1.y > 6, "rests on " + std::to_string(deg) + " deg: " + fmt(p0) + " -> " + fmt(p1));
                CharacterInput up;
                up.moveZ = 1;
                up.yaw = -1.5707963f; // walk +X, uphill
                for (int i = 0; i < 30; ++i) hostTick(s, {&w}, up);
                require(w.state.grounded && w.state.position.y > p1.y + 0.5f, "walks up a walkable slope " + fmt(w.state.position));
            } else {
                require(touched > 0 && p0.y > 5 && p1.x < -4 && std::abs(p1.y) < 0.02f && w.state.grounded,
                        "slides down " + std::to_string(deg) + " deg: " + fmt(p0) + " -> " + fmt(p1));
            }
        }
        // Walking into a steep slope does not climb it.
        Scene s;
        ground(s);
        auto d = box({-4, 0, 0}, {6, 1, 6}, Motion::Static);
        d.rotation = b3MakeQuatFromAxisAngle({0, 0, 1}, -70 * 3.14159265f / 180);
        s.create(d);
        Walker w;
        settle(s, w);
        CharacterInput in;
        in.moveZ = 1;
        in.yaw = 1.5707963f;
        float top = 0;
        for (int i = 0; i < 180; ++i) { hostTick(s, {&w}, in); top = std::max(top, w.state.position.y); }
        require(top < 0.2f, "climbed a 70 deg slope to " + std::to_string(top));
    });

    test("walls block the character, no tunnelling at high speed", [] {
        Scene s;
        ground(s);
        s.create(box({0, 1.5f, -3}, {10, 3, 0.1f}, Motion::Static)); // thin wall, near face z = -2.95
        Walker w;
        w.params.walkSpeed = 90;
        w.params.acceleration = 100000;
        require(validParams(w.params), "fast params valid");
        w.params.maxSpeed = 100;
        settle(s, w);
        CharacterInput in;
        in.moveZ = 1;
        for (int i = 0; i < 60; ++i) {
            hostTick(s, {&w}, in);
            require(w.state.position.z > -2.95f + w.params.radius - 0.02f, "tunnelled at tick " + std::to_string(i) + ": " + fmt(w.state.position));
        }
        require(w.state.position.z < -2.95f + w.params.radius + 0.05f, "reached the wall " + fmt(w.state.position));
        require(std::abs(w.state.velocity.z) < 1e-3f, "velocity into the wall clipped");
        // Diagonal: slides along the wall instead of stopping.
        w.params.walkSpeed = 4;
        in.moveX = 1;
        float x0 = w.state.position.x;
        for (int i = 0; i < 60; ++i) hostTick(s, {&w}, in);
        require(w.state.position.x > x0 + 2, "slides along the wall");
    });

    test("character rides a moving kinematic platform", [] {
        Scene s;
        ground(s);
        auto plat = s.create(box({0, 1.75f, 0}, {4, 0.5f, 4}, Motion::Kinematic)); // top at 2
        Walker w;
        w.state.position = {0, 2.5f, 0};
        settle(s, w, 60);
        require(w.state.grounded && w.state.ground == plat && std::abs(w.state.position.y - 2) < 0.02f, "on the platform " + fmt(w.state.position));
        s.setLinearVelocity(plat, {2, 0.5f, 0});
        b3Vec3 c0 = w.state.position, p0 = s.position(plat);
        for (int i = 0; i < 60; ++i) hostTick(s, {&w}, {});
        b3Vec3 moved{w.state.position.x - c0.x, w.state.position.y - c0.y, w.state.position.z - c0.z};
        b3Vec3 pm{s.position(plat).x - p0.x, s.position(plat).y - p0.y, s.position(plat).z - p0.z};
        require(dist(moved, pm) < 0.05f && w.state.ground == plat, "carried " + fmt(moved) + " vs platform " + fmt(pm));
        // Jumping off keeps the platform's velocity.
        CharacterInput jump;
        jump.jump = true;
        hostTick(s, {&w}, jump);
        require(std::abs(w.state.velocity.x - 2) < 0.05f, "jump inherits platform velocity " + fmt(w.state.velocity));
    });

    test("a box dropped on a character's head does not press it through the ground", [] {
        Scene s;
        ground(s, 40);
        Walker w;
        w.proxy = s.createCharacter(w.params, {0, 0, 0});
        settle(s, w);
        // The proxy has no contacts with bodies: the box sinks into the capsule and rests on the ground there.
        s.create(box({0, 3, 0}, {0.8f, 0.8f, 0.8f}));
        for (int i = 0; i < 240; ++i) {
            hostTick(s, {&w}, {});
            require(w.state.position.y > -0.02f, "pressed into the ground at tick " + std::to_string(i) + ": " + fmt(w.state.position));
        }
        CharacterInput in;
        in.moveZ = 1;
        for (int i = 0; i < 60; ++i) hostTick(s, {&w}, in);
        require(w.state.position.z < -2 && std::abs(w.state.position.y) < 0.02f, "walks out from under it " + fmt(w.state.position));
    });

    test("character pushes dynamic boxes with a bounded impulse", [] {
        Scene s;
        ground(s);
        auto light = box({0, 0.5f, -2}, {1, 1, 1});
        light.density = 40;
        auto lb = s.create(light);
        auto heavy = box({3, 0.5f, -2}, {1, 1, 1});
        heavy.density = 3000;
        auto hb = s.create(heavy);
        Walker a, b;
        a.state.position = {0, 0, 0};
        b.state.position = {3, 0, 0};
        a.proxy = s.createCharacter(a.params, a.state.position);
        b.proxy = s.createCharacter(b.params, b.state.position);
        CharacterInput in;
        in.moveZ = 1;
        bool reported = false;
        for (int i = 0; i < 120; ++i) {
            hostTick(s, {&a, &b}, in);
            for (auto& p : a.pushes) reported = reported || (p.body == lb && p.maxImpulse == a.params.pushImpulseLimit && p.direction.z < -0.9f);
        }
        require(reported, "push reported");
        require(s.position(lb).z < -4, "light box pushed to " + fmt(s.position(lb)));
        require(a.state.position.z < -2.5f, "character follows the box " + fmt(a.state.position));
        require(s.position(hb).z > -2.3f && b.state.position.z > -1.5f + 0.05f - 0.1f, "heavy box barely moves " + fmt(s.position(hb)));
    });

    test("characters collide with each other but not with their own proxy", [] {
        Scene s;
        ground(s);
        Walker a, b;
        a.state.position = {0, 0, 0};
        b.state.position = {0, 0, -3};
        a.proxy = s.createCharacter(a.params, a.state.position);
        b.proxy = s.createCharacter(b.params, b.state.position);
        settle(s, a);
        CharacterInput in;
        in.moveZ = 1;
        for (int i = 0; i < 90; ++i) {
            stepCharacter(s.world(), a.params, a.state, in, s.dt(), nullptr, a.proxy);
            s.moveCharacter(a.proxy, a.state);
            s.step();
        }
        require(a.state.position.z > -3 + 2 * a.params.radius - 0.05f && a.state.position.z < -1.5f, "stopped by the other character " + fmt(a.state.position));
        auto hits = s.raycast({0, 1, 5}, {0, 0, -20});
        require(hits.body == a.proxy, "rays hit character proxies");
        require(s.raycast({0, 1, 5}, {0, 0, -20}, {.ignore = a.proxy}).body == b.proxy, "ray ignores the shooter");
    });

    test("characters spawned inside each other are pushed apart", [] {
        Scene s;
        ground(s);
        // Partly overlapping, and three at exactly the same point (no separating direction to start from).
        Walker a, b, c, d, e;
        a.state.position = {0, 0, 0};
        b.state.position = {0.2f, 0, 0.1f};
        c.state.position = d.state.position = e.state.position = {5, 0, 5};
        std::vector<Walker*> all{&a, &b, &c, &d, &e};
        for (auto* w : all) w->proxy = s.createCharacter(w->params, w->state.position);
        for (int i = 0; i < 90; ++i) hostTick(s, all, {});
        auto apart = [](const Walker& x, const Walker& y) {
            float dx = x.state.position.x - y.state.position.x, dz = x.state.position.z - y.state.position.z;
            return std::sqrt(dx * dx + dz * dz);
        };
        float minGap = 2 * a.params.radius - 0.02f;
        require(apart(a, b) >= minGap, "a / b " + std::to_string(apart(a, b)));
        require(apart(c, d) >= minGap && apart(c, e) >= minGap && apart(d, e) >= minGap,
                "c / d / e " + std::to_string(apart(c, d)) + " " + std::to_string(apart(c, e)) + " " + std::to_string(apart(d, e)));
        for (auto* w : all) require(w->state.grounded && std::abs(w->state.position.y) < 0.02f, "still on the ground " + fmt(w->state.position));
        // Pushed apart gently, not launched: they stay near where they spawned.
        require(dist(a.state.position, {0, 0, 0}) < 1 && dist(c.state.position, {5, 0, 5}) < 1, "moved little " + fmt(a.state.position) + fmt(c.state.position));
    });

    test("a rotating kinematic platform carries the rider around and turns its yaw", [] {
        Scene s;
        ground(s);
        auto plat = s.create(box({0, 1.75f, 0}, {8, 0.5f, 8}, Motion::Kinematic)); // top at 2
        Walker w;
        w.state.position = {2, 2.2f, 0};
        settle(s, w, 60);
        require(w.state.ground == plat, "on the platform");
        s.setAngularVelocity(plat, {0, 1, 0});
        float yaw = 0;
        for (int i = 0; i < 90; ++i) {
            CharacterInput in;
            in.yaw = yaw;
            hostTick(s, {&w}, in);
            yaw += w.state.groundTurn;   // the game folds the turn into its look yaw (next input)
        }
        float t = 90 * s.dt();           // 1.5 rad turned
        b3Vec3 want{2 * std::cos(t), 2, -2 * std::sin(t)};
        float r = std::sqrt(w.state.position.x * w.state.position.x + w.state.position.z * w.state.position.z);
        require(std::abs(r - 2) < 0.03f, "radius kept " + std::to_string(r));
        require(dist(w.state.position, want) < 0.08f, "carried around to " + fmt(w.state.position) + " want " + fmt(want));
        require(std::abs(yaw - t) < 0.03f && std::abs(w.state.yaw - t) < 0.03f, "yaw turned " + std::to_string(yaw) + " / " + std::to_string(w.state.yaw));
    });

    test("sensor end events for destroyed bodies and sensors", [] {
        Scene s;
        ground(s);
        auto zd = box({0, 1, 0}, {4, 2, 4}, Motion::Static);
        zd.sensor = true;
        auto zone = s.create(zd);
        auto ball = [&](b3Vec3 p) { BodyDesc d; d.shape = ShapeKind::Sphere; d.radius = 0.3f; d.position = p; return s.create(d); };
        auto a = ball({0, 1, 0}), b = ball({1, 1, 1});
        Walker w;
        w.proxy = s.createCharacter(w.params, {-1, 0, -1});
        int begins = 0;
        for (int i = 0; i < 10; ++i) { s.step(); for (auto& e : s.sensorEvents()) begins += e.begin && e.sensor == zone; }
        require(begins == 3, "three visitors inside, got " + std::to_string(begins));
        auto ends = [&](BodyHandle sensor, BodyHandle visitor) {
            int n = 0;
            for (auto& e : s.sensorEvents()) n += !e.begin && e.sensor == sensor && e.visitor == visitor;
            return n;
        };
        s.destroy(a);
        s.step();
        require(ends(zone, a) == 1, "end for the destroyed visitor, once");
        for (int i = 0; i < 5; ++i) { s.step(); require(ends(zone, a) == 0, "no repeat"); }
        // A destroyed sensor ends the overlaps of everything inside it.
        s.destroy(zone);
        s.step();
        require(ends(zone, b) == 1 && ends(zone, w.proxy) == 1, "ends for the visitors of a destroyed sensor");
        s.step();
        require(s.sensorEvents().empty(), "nothing more");
    });

    test("prediction parity: full scene vs static-only copy", [] {
        auto json = Json::parse(R"({"tickRate":60,"bodies":[
            {"id":"ground","motion":"static","position":[0,-0.5,0],"size":[80,1,80]},
            {"id":"ramp","motion":"static","position":[-6,0.8,-6],"size":[6,0.4,4],"axis":[0,0,1],"angle":-0.35},
            {"id":"step","motion":"static","position":[4,0.15,-6],"size":[3,0.3,3]},
            {"id":"wall","motion":"static","position":[0,1.5,-12],"size":[20,3,0.4]},
            {"id":"crate1","position":[25,3,25]},{"id":"crate2","position":[26,5,25]},
            {"id":"ball","shape":"sphere","radius":0.5,"position":[24,8,24]}]})");
        auto full = Scene::fromJson(json);
        auto copy = Scene::staticCopy(json);
        Walker server, client;
        server.proxy = full->createCharacter(server.params, server.state.position);
        float worst = 0;
        std::mt19937 rng(7);
        CharacterInput in;
        // Visit the ramp, the step and the wall, then wander randomly.
        const float targets[][2] = {{4, -6}, {-6, -6}, {0, -11}, {-6, -6}, {4, -6}, {0, 0}};
        size_t next = 0;
        float top = 0;
        for (int t = 0; t < 1500; ++t) {
            if (next < std::size(targets)) {
                float dx = targets[next][0] - server.state.position.x, dz = targets[next][1] - server.state.position.z;
                in = {};
                in.moveZ = 1;
                in.yaw = std::atan2(-dx, -dz);
                in.run = next % 2 == 1;
                if (dx * dx + dz * dz < 0.25f || t > 150 * int(next + 1)) ++next;
            } else if (t % 20 == 0) {
                in.moveX = std::uniform_real_distribution<float>(-1, 1)(rng);
                in.moveZ = std::uniform_real_distribution<float>(-0.3f, 1)(rng);
                in.yaw = std::uniform_real_distribution<float>(-3.14f, 3.14f)(rng);
                in.run = t % 60 == 0;
            }
            in.jump = t % 97 == 0;
            CharacterInput q = in.quantized();
            stepCharacter(full->world(), server.params, server.state, q, full->dt(), nullptr, server.proxy);
            full->moveCharacter(server.proxy, server.state);
            full->step();
            stepCharacter(copy->world(), client.params, client.state, q, copy->dt());
            worst = std::max(worst, dist(server.state.position, client.state.position));
            top = std::max(top, server.state.position.y);
            require(server.state.grounded == client.state.grounded || worst < 1e-3f, "grounded flag differs at " + std::to_string(t));
        }
        require(worst < 1e-3f, "positions diverged by " + std::to_string(worst));
        require(top > 0.25f, "the walk climbed the ramp / step");
        std::printf("       worst divergence %.2e m over 1500 ticks (highest point %.2f m)\n", worst, top);
    });

    test("input pack / unpack round trip and fuzz", [] {
        CharacterInput in;
        in.moveX = 0.5f;
        in.moveZ = -1;
        in.yaw = 2.0f;
        in.jump = true;
        auto b = in.pack();
        require(b.size() == CharacterInput::kPackedSize, "packed size");
        auto out = CharacterInput::unpack(b);
        require(std::abs(out.moveX - 0.5f) < 0.01f && out.moveZ == -1 && std::abs(out.yaw - 2.0f) < 1e-3f && out.jump && !out.run, "round trip");
        require(out.pack() == b, "pack is stable after quantization");
        CharacterInput wrap;
        wrap.yaw = -1.0f;
        require(std::abs(CharacterInput::unpack(wrap.pack()).yaw - (6.2831853f - 1)) < 1e-3f, "negative yaw wraps");
        CharacterInput nan;
        nan.moveX = std::numeric_limits<float>::quiet_NaN();
        nan.yaw = std::numeric_limits<float>::infinity();
        auto n = CharacterInput::unpack(nan.pack());
        require(n.moveX == 0 && n.yaw == 0, "non-finite input packs neutral");
        require(CharacterInput::unpack(nullptr, 5).moveZ == 0 && CharacterInput::unpack(b.data(), 3).jump == false, "short input is neutral");
        std::mt19937 rng(11);
        Scene s;
        ground(s);
        Walker w;
        for (int i = 0; i < 20000; ++i) {
            std::vector<uint8_t> bytes(rng() % 9);
            for (auto& x : bytes) x = uint8_t(rng());
            auto f = CharacterInput::unpack(bytes);
            require(std::isfinite(f.moveX) && std::abs(f.moveX) <= 1 && std::isfinite(f.moveZ) && std::abs(f.moveZ) <= 1 &&
                    std::isfinite(f.yaw) && f.yaw >= 0 && f.yaw < 6.2832f, "fuzzed input in range");
            if (i % 50 == 0) {
                stepCharacter(s.world(), w.params, w.state, f, s.dt());
                require(std::isfinite(w.state.position.x) && std::isfinite(w.state.position.y) && std::isfinite(w.state.position.z), "fuzzed step finite");
            }
        }
        // Hostile state and parameters never move the character or crash.
        CharacterState st;
        st.velocity = {std::numeric_limits<float>::quiet_NaN(), 0, 0};
        stepCharacter(s.world(), w.params, st, {}, s.dt());
        require(std::isfinite(st.velocity.x), "NaN velocity reset");
        st.position = {1e30f, 0, 0};
        stepCharacter(s.world(), w.params, st, {}, s.dt());
        require(st.position.x == 1e30f && st.velocity.x == 0, "huge position refused");
        CharacterParams bad;
        bad.radius = -1;
        CharacterState ok;
        stepCharacter(s.world(), bad, ok, {}, s.dt());
        stepCharacter(s.world(), w.params, ok, {}, 10.0f);
        require(ok.position.y == 0, "invalid params / dt ignored");
    });

    test("oriented character wall ground, jump and zero gravity", []{
        Scene s;
        const auto wall=s.create(box({-.5f,0,0},{1,30,30},Motion::Static));
        require(wall!=kNoBody,"wall");
        CharacterParams p;
        CharacterState st;st.position={0,0,0};
        const auto q=b3MakeQuatFromAxisAngle({0,0,1},-1.57079632679f); // local +Y -> +X
        const auto proxy=s.createCharacter(p,st.position);
        require(proxy!=kNoBody&&s.moveCharacterOriented(proxy,st,q,true),"oriented proxy");
        for(int i=0;i<5;++i){
            require(stepCharacterOriented(s.world(),p,st,{},s.dt(),q,true,nullptr,proxy),"wall step");
            require(s.moveCharacterOriented(proxy,st,q),"proxy target");s.step();
        }
        require(st.grounded&&st.ground==wall,"wall is ground under -X gravity");
        CharacterInput in;in.jump=true;
        require(stepCharacterOriented(s.world(),p,st,in,s.dt(),q,true,nullptr,proxy),"wall jump");
        require(st.velocity.x>0&&st.position.x>0,"jump away from wall");
        p.gravity=0;st.position={0,0,0};st.velocity={};
        require(stepCharacterOriented(s.world(),p,st,in,s.dt(),q,false,nullptr,proxy),"zero-g contact");
        require(!st.grounded&&st.ground==kNoBody&&std::abs(st.velocity.x)<.001f,"no zero-g ground jump");
        require(!stepCharacterOriented(s.world(),p,st,{},s.dt(),{0,0,0,0},true),"invalid rotation rejected");
    });
    test("oriented identity preserves legacy step", []{
        Scene s;ground(s);CharacterParams p;CharacterState a,b;
        CharacterInput in;in.moveZ=1;
        stepCharacter(s.world(),p,a,in,s.dt());
        require(stepCharacterOriented(s.world(),p,b,in,s.dt(),b3Quat_identity,true),"identity step");
        require(dist(a.position,b.position)<1e-6f&&dist(a.velocity,b.velocity)<1e-6f&&a.grounded==b.grounded,"legacy parity");
    });

    if (bench) {
        Scene s({.workers = 1, .maxBodies = 2048});
        ground(s, 400);
        std::mt19937 rng(3);
        for (int i = 0; i < 500; ++i) {
            auto d = box({float(i % 20) * 1.6f - 16, 0.5f + float(i / 100) * 1.2f, float((i / 20) % 5) * 1.6f - 4}, {1, 1, 1});
            if (i % 3 == 1) { d.shape = ShapeKind::Sphere; d.radius = 0.5f; }
            d.density = 60;
            s.create(d);
        }
        std::vector<Walker> walkers(64);
        std::vector<Walker*> ptrs;
        for (int i = 0; i < 64; ++i) {
            float a = float(i) / 64 * 6.2831853f;
            walkers[i].state.position = {std::cos(a) * 14, 0, std::sin(a) * 14};
            walkers[i].proxy = s.createCharacter(walkers[i].params, walkers[i].state.position);
            ptrs.push_back(&walkers[i]);
        }
        using clock = std::chrono::steady_clock;
        double charMs = 0, stepMs = 0;
        const int ticks = 600;
        for (int t = 0; t < ticks; ++t) {
            auto t0 = clock::now();
            for (int i = 0; i < 64; ++i) {
                auto& w = walkers[i];
                CharacterInput in;
                in.moveZ = 1;
                in.yaw = float(i) + float(t) * 0.01f; // wander through the pile
                in.jump = (t + i) % 120 == 0;
                stepCharacter(s.world(), w.params, w.state, in, s.dt(), &w.pushes, w.proxy);
                s.applyPushes(w.pushes);
                s.moveCharacter(w.proxy, w.state);
            }
            auto t1 = clock::now();
            s.step();
            auto t2 = clock::now();
            charMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
            stepMs += std::chrono::duration<double, std::milli>(t2 - t1).count();
        }
        int awake = 0;
        for (auto& m : s.moved()) awake += !m.fellAsleep;
        std::printf("bench: 500 bodies + 64 characters, 1 worker, %d ticks: characters %.3f ms, physics step %.3f ms, total %.3f ms per tick (%d bodies moved in the last tick)\n",
                    ticks, charMs / ticks, stepMs / ticks, (charMs + stepMs) / ticks, awake);
    }

    if (failures) { std::printf("physics scene: %d test(s) failed\n", failures); return 1; }
    std::printf("physics scene: all tests passed\n");
    return 0;
}
