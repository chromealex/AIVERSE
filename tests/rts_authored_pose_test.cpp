// RTS runtime applies the Studio-authored map Transform (components.Transform):
// Y rotation -> unit facing, height / X-Z tilt / per-axis scale -> AuthoredPose,
// persisted and checksummed; RtsView turns it into the same world frame Studio shows.
#include "engine/data/JsonCompose.h"
#include "modules/rts/RtsModule.h"
#include "modules/rts/view/RtsView.h"
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
using namespace aiverse;
namespace g = aiverse::game;
static void check(bool yes, const std::string& what) { if (!yes) throw std::runtime_error(what); }
static Json baseMap(const std::string& base) { return Json::parseFile(base + "maps/duel.json"); }
static std::unique_ptr<Simulation> make(const Json& map, const std::string& base) {
    Json rules = readComposedJson(base + "rules.json", fileReader());
    auto sim = std::make_unique<Simulation>(SimConfig{}, std::make_unique<g::RtsModule>());
    sim->setup(rules, map);
    return sim;
}
// Buildings snap to their footprint at spawn (RtsModule::spawn), so the map
// position is only close to the entity's: take the nearest one within a cell.
static Entity findAt(Simulation& sim, float x, float z) {
    Entity found;
    double best = 1.0;
    sim.world().each<Transform>([&](Entity e, Transform& t) {
        const double dx = t.pos.x.toDouble() - x, dz = t.pos.z.toDouble() - z, d = std::sqrt(dx * dx + dz * dz);
        if (d < best) { best = d; found = e; }
    });
    return found;
}
// aiverse_runtime (renderer, platform) supplies main() and calls aiverseMain, like tests/deferred_loading.cpp.
int aiverseMain(int, char**) {
    const std::string base = std::string(AIVERSE_SOURCE_DIR) + "/templates/mushrooms/";
    // Plain map: no pose component registered, save format unchanged (v9).
    {
        auto plain = make(baseMap(base), base);
        const std::string bytes = plain->serialize();
        check(bytes.size() > 8 && uint8_t(bytes[4]) == 9, "map without Transform changed the save version");
    }
    Json map = baseMap(base);
    auto& entities = map.member("entities");
    Json& e0 = entities.at(1);   // a neutral village at [9.6, 18.2]
    e0.set("components", Json::parse(R"({"Transform":{"y":2.5,"rotation":[10,90,-20],"scale":[2,1,0.5]}})"));
    Json& e1 = entities.at(2);   // [9.6, 9.8]: yaw only -> facing, no pose
    e1.set("components", Json::parse(R"({"Transform":{"yaw":90}})"));
    auto sim = make(map, base), peer = make(map, base);
    check(sim->checksum() == peer->checksum(), "authored pose not deterministic");
    const Entity posed = findAt(*sim, 9.6f, 18.2f), yawOnly = findAt(*sim, 9.6f, 9.8f);
    check(posed.valid() && yawOnly.valid(), "authored entities not spawned");
    const auto* pose = sim->world().tryGet<g::AuthoredPose>(posed);
    check(pose && pose->hasY && pose->y == Fixed::fromRatio(5, 2), "Transform Y not applied");
    check(pose->rotX == Fixed::fromInt(10) && pose->rotZ == Fixed::fromInt(-20), "Transform X/Z rotation not applied");
    check(pose->scale.x == Fixed::fromInt(2) && pose->scale.y == Fixed::fromInt(1) && pose->scale.z == Fixed::fromRatio(1, 2), "Transform scale not applied");
    check(!sim->world().tryGet<g::AuthoredPose>(yawOnly), "yaw-only Transform must not add a pose");
    // Transform yaw 90 (counter-clockwise) == map facing 270 (west): forward -X.
    for (Entity e : {posed, yawOnly}) {
        const Vec3 f = sim->world().get<g::Unit>(e).facing;
        check(f.x.toDouble() < -0.999 && std::abs(f.z.toDouble()) < 1e-3, "Transform yaw did not set the unit facing");
    }
    for (int i = 0; i < 6; ++i) { sim->step(); peer->step(); check(sim->checksum() == peer->checksum(), "authored pose replay diverged"); }
    const std::string bytes = sim->serialize();
    check(!bytes.empty(), "save with AuthoredPose failed");
    auto loaded = make(map, base); std::string error;
    check(loaded->deserialize(bytes, &error), "reload: " + error);
    check(loaded->checksum() == sim->checksum(), "reloaded checksum mismatch");
    check(loaded->world().tryGet<g::AuthoredPose>(posed) && loaded->world().get<g::AuthoredPose>(posed).scale.x == Fixed::fromInt(2), "pose lost on reload");
    // Bad values are rejected at setup, like Studio does.
    Json bad = baseMap(base);
    bad.member("entities").at(1).set("components", Json::parse(R"({"Transform":{"scale":0}})"));
    bool threw = false;
    try { make(bad, base); } catch (const std::exception&) { threw = true; }
    check(threw, "invalid Transform scale accepted");
    // View frame: matches Studio R = T * Ry(yaw) * Rx * Rz * S on top of the RTS +X-forward model.
    g::SceneObject o; o.posed = true; o.poseRotX = 10; o.poseRotZ = -20; o.poseScale = {2, 1, .5f};
    const float yawDeg = 90, kDeg = 0.017453292f;
    const render::float3 p{3, 1, 4};
    const render::mat4 rts = g::authoredPoseFrame(o, yawDeg * kDeg + 1.5707963f, p) *
        render::mat4::translate(p) * render::mat4::rotateY(yawDeg * kDeg + 1.5707963f);
    const render::mat4 studio = render::mat4::translate(p) * render::mat4::rotateY(yawDeg * kDeg) *
        render::mat4::rotateX(10 * kDeg) * render::mat4::rotateZ(-20 * kDeg) * render::mat4::scale({2, 1, .5f}) *
        render::mat4::rotateY(1.5707963f);
    for (int i = 0; i < 16; ++i) check(std::abs(rts.m[i] - studio.m[i]) < 1e-4f, "view pose frame differs from Studio");
    g::SceneObject plain;
    const render::mat4 id = g::authoredPoseFrame(plain, 1.0f, p), ident{};
    for (int i = 0; i < 16; ++i) check(id.m[i] == ident.m[i], "unposed object frame not identity");
    std::cout << "PASS RTS authored Transform: facing, Y, X/Z tilt, scale, save/reload, view frame\n";
    return 0;
}
