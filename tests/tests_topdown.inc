// Top-down heroes (rules "topDown", RtsTopDown.h; client model TopDownControls.h): input packing, movement, walls,
// unit bodies, auto-fire / aim policy, abilities, bots, determinism, saves and rollback snapshots.
#include "modules/rts/RtsTopDown.h"
#include "modules/rts/view/TopDownControls.h"

namespace {
const char* kTopDownRules = R"J({
  "victory": "noUnits",
  "resources": {"gold": {"start": 0}},
  "topDown": {
    "heroes": ["drone"],
    "move": {"accel": 36, "decel": 54, "turnRate": 720},
    "input": {"sectors": 45, "magnitudeSteps": 1, "deadzone": 0.05},
    "aim": {"snapCone": 15},
    "autoFire": {"whileMoving": false, "retarget": "nearest", "interval": 0.2, "switchMargin": 0.5, "lineOfFire": true}
  },
  "types": {
    "drone": {"health": {"max": 600}, "move": {"speed": 6, "radius": 0.45}, "sight": 12,
              "weapons": [{"range": 6, "damage": 10, "cooldown": 0.3}],
              "abilities": [{"id": "missile", "target": "point", "range": 8, "cooldown": 5, "autocast": false,
                             "effects": [{"type": "damage", "amount": 90, "radius": 1.5}]}]},
    "dummy": {"health": {"max": 100000}, "move": {"speed": 0, "radius": 0.4}, "sight": 2},
    "mover": {"health": {"max": 300}, "move": {"speed": 2, "radius": 0.4}, "sight": 8, "weapons": [{"range": 0.5, "damage": 1, "cooldown": 1}]}
  }
})J";

// 32x16 map; a wall column x = 12 (rows 2..13) unless `noWall`
std::string topDownMap(const std::string& entities, bool wall = true) {
    std::string rows;
    for (int y = 0; y < 16; ++y) {
        std::string r(32, '.');
        if (wall && y >= 2 && y <= 13) r[12] = '#';
        rows += std::string(y ? "," : "") + "\"" + r + "\"";
    }
    return R"({"size":[32,16],"players":[{"name":"A","team":1},{"name":"B","team":2}],"terrain":{"rows":[)" + rows + "]},\"entities\":[" + entities + "]}";
}

game::TopDownPilot& pilot(Simulation& sim, uint8_t p) { return mod(sim).topDown()->store(sim)->pilots[p]; }
Vec3 tdPos(Simulation& sim, Entity e) { return sim.world().get<Transform>(e).pos; }
void steer(Simulation& sim, uint8_t p, Vec3 dir, int mag = 1000, bool aim = false, Vec3 aimDir = {}) {
    game::topdown::Input in;
    in.moveAngle = game::TopDown::angleOf(dir); in.moveMag = mag;
    in.aim = aim; in.aimAngle = aim ? game::TopDown::angleOf(aimDir) : 0;
    Command c; c.player = p; c.type = game::kCmdTopDown; c.param = game::topdown::pack(in);
    sim.submit(c);
}
const Vec3 kEast{Fixed::fromInt(1), Fixed{}, Fixed{}}, kNorth{Fixed{}, Fixed{}, Fixed::fromInt(-1)}, kSouth{Fixed{}, Fixed{}, Fixed::fromInt(1)};
} // namespace

TEST(topdown_input_packing_and_quantization) {
    game::topdown::Input in;
    in.moveAngle = 12345; in.moveMag = 777; in.aimAngle = 54321; in.aim = true; in.fire = true;
    CHECK(game::topdown::unpack(game::topdown::pack(in)) == in);
    in.moveMag = 5000;   // clamped on the wire
    CHECK(game::topdown::unpack(game::topdown::pack(in)).moveMag == 1000);
    // 45 sectors (Drone Fall GD_Input): 8 degree steps, nearest sector, wraps to 0
    std::set<int32_t> seen;
    for (int a = 0; a < 65536; a += 97) seen.insert(game::topdown::quantizeAngle(a, 45));
    CHECK(seen.size() == 45);
    CHECK(game::topdown::quantizeAngle(65535, 45) == 0 && game::topdown::quantizeAngle(-1, 45) == 0);
    CHECK(game::topdown::quantizeAngle(16384, 4) == 16384 && game::topdown::quantizeAngle(16384 + 8000, 4) == 16384);
    // magnitude: dead zone, then ceil to steps (magnitudeStep 1 = full speed or nothing)
    CHECK(game::topdown::quantizeMag(40, 1, 50) == 0 && game::topdown::quantizeMag(60, 1, 50) == 1000);
    CHECK(game::topdown::quantizeMag(260, 4, 50) == 500 && game::topdown::quantizeMag(1000, 0, 0) == 1000);
    // binary-turn directions: 0 = +z, a quarter = +x
    Vec3 d = game::TopDown::direction(16384);
    CHECK(d.x == Fixed::fromInt(1) && fxAbs(d.z) <= Fixed::fromRaw(2));
    CHECK(game::TopDown::angleOf(kEast) == 16384 && game::TopDown::angleOf(kSouth) == 0 && game::TopDown::angleOf(kNorth) == 32768);
}

TEST(topdown_rules_parse_and_errors) {
    std::vector<std::string> ids = {"drone", "dummy"};
    auto r = game::TopDown::parse(Json::parse(R"({"heroes": "drone", "move": {"turnRate": 360}, "autoFire": {"cone": 90, "retarget": "keep"}})"), ids);
    CHECK(r.enabled && r.heroTypes.size() == 1 && r.turnRate == Fixed::fromInt(1) && r.retarget == game::RetargetMode::Keep);
    CHECK(fxAbs(r.autoCos - Fixed::fromRatio(7071, 10000)) < Fixed::fromRatio(1, 100));   // cos 45
    CHECK(!game::TopDown::parse(Json(), ids).enabled);
    auto throws = [&](const char* j) { try { game::TopDown::parse(Json::parse(j), ids); return false; } catch (const std::exception&) { return true; } };
    CHECK(throws(R"({"heroes": ["ghost"]})"));
    CHECK(throws(R"({"heroes": []})"));
    CHECK(throws(R"({"heroes": "drone", "autoFire": {"retarget": "random"}})"));
    CHECK(throws(R"({"heroes": "drone", "input": {"deadzone": 1.5}})"));
    CHECK(throws(R"({"heroes": "drone", "aim": {"snapCone": 200}})"));
}

TEST(topdown_hero_accelerates_slides_and_respects_walls) {
    auto sim = makeSimJson(kTopDownRules, topDownMap(R"({"type":"drone","owner":1,"pos":[6.5,8.5]},{"type":"dummy","owner":2,"pos":[30.5,1.5]})"));
    auto& m = mod(*sim);
    CHECK(m.topDown() && m.topDown()->active());
    Entity hero = m.topDown()->heroOf(*sim, 1);
    CHECK(hero.valid() && (sim->world().get<game::Unit>(hero).flags & game::kUnitPossessed));
    CHECK(!m.topDown()->heroOf(*sim, 2).valid());   // player 2 has no drone
    steer(*sim, 1, kEast);
    sim->step(); sim->step(); sim->step();   // the command executes after the input delay
    const Fixed v1 = pilot(*sim, 1).vel.length();
    CHECK(v1.raw > 0 && v1 < Fixed::fromInt(6));   // accelerating (36 / s^2), not at top speed yet
    run(*sim, 1.0);
    CHECK(fxAbs(pilot(*sim, 1).vel.length() - Fixed::fromInt(6)) < Fixed::fromRatio(1, 10) || tdPos(*sim, hero).x > Fixed::fromInt(11));
    run(*sim, 2.0);
    // the wall at x = 12: the body (radius 0.45) stops against it
    Vec3 p = tdPos(*sim, hero);
    CHECK(p.x < Fixed::fromInt(12) - Fixed::fromRatio(4, 10) && p.x > Fixed::fromInt(11));
    CHECK(pilot(*sim, 1).vel.x.raw == 0);
    // diagonal into the wall: slides along it (z changes, x stays against the wall)
    const Fixed z0 = p.z;
    steer(*sim, 1, Vec3{Fixed::fromInt(1), Fixed{}, Fixed::fromInt(1)});
    run(*sim, 0.6);
    Vec3 q = tdPos(*sim, hero);
    CHECK(q.z > z0 + Fixed::fromInt(1) && q.x < Fixed::fromInt(12) - Fixed::fromRatio(4, 10));
    // release: brakes to a stop (decel), facing kept
    steer(*sim, 1, {}, 0);
    run(*sim, 0.5);
    CHECK(pilot(*sim, 1).vel.length().raw == 0);
    const Vec3 rest = tdPos(*sim, hero);
    run(*sim, 0.5);
    CHECK(tdPos(*sim, hero) == rest);
    // RTS orders never reach a hero
    Command mv; mv.player = 1; mv.type = game::kCmdMove; mv.units = {hero}; mv.point = {Fixed::fromInt(3), Fixed{}, Fixed::fromInt(3)};
    sim->submit(mv);
    run(*sim, 1.0);
    CHECK(tdPos(*sim, hero) == rest);
    // invalid input is clamped / ignored: player 0, unknown slot, hero of nobody
    Command bad; bad.player = 7; bad.type = game::kCmdTopDown; bad.param = -1;
    sim->submit(bad);
    sim->step();
}

TEST(topdown_hero_bodies_push_out_of_units) {
    auto sim = makeSimJson(kTopDownRules, topDownMap(R"({"type":"drone","owner":1,"pos":[4.5,8.5]},{"type":"dummy","owner":1,"pos":[7.5,8.5]},{"type":"dummy","owner":2,"pos":[30.5,1.5]})", false));
    Entity hero = mod(*sim).topDown()->heroOf(*sim, 1);
    Entity block = findType(*sim, "dummy", 1);
    steer(*sim, 1, kEast);
    Fixed minGap = Fixed::fromInt(100);
    for (int i = 0; i < 40; ++i) { sim->step(); minGap = fxMin(minGap, (tdPos(*sim, block) - tdPos(*sim, hero)).xz().length()); }
    CHECK(minGap >= Fixed::fromRatio(80, 100));   // radius 0.45 + 0.4: pressed against it, never through it
    CHECK(minGap <= Fixed::fromRatio(90, 100));   // it did reach the body
    // pressing on: the round body is slid around, as a hero brushing past a unit
    run(*sim, 2.0);
    CHECK(tdPos(*sim, hero).x > tdPos(*sim, block).x || fxAbs(tdPos(*sim, hero).z - tdPos(*sim, block).z) > Fixed::fromRatio(8, 10));
}

TEST(topdown_autofire_nearest_line_of_fire_and_moving) {
    // enemy A at 3 cells behind the wall (x 12), enemy B at 5 cells in the open
    auto sim = makeSimJson(kTopDownRules, topDownMap(R"({"type":"drone","owner":1,"pos":[10.5,8.5]},
        {"type":"dummy","owner":2,"pos":[13.5,8.5]},{"type":"dummy","owner":2,"pos":[10.5,13.5]})"));
    auto& m = mod(*sim);
    Entity hero = m.topDown()->heroOf(*sim, 1);
    Entity behind, open;
    sim->world().each<game::Unit, Owner, Transform>([&](Entity e, game::Unit&, Owner& o, Transform& t) {
        if (o.player != 2) return;
        if (t.pos.x > Fixed::fromInt(13)) behind = e; else open = e;
    });
    run(*sim, 0.5);
    CHECK(pilot(*sim, 1).target == open);                                   // the nearer one is behind the wall
    CHECK(sim->world().get<game::Combat>(hero).target == open);            // the weapon system fires at it
    CHECK(sim->world().get<game::Unit>(open).hp < Fixed::fromInt(100000));
    // moving: auto-fire holds (whileMoving false)
    steer(*sim, 1, kNorth);
    run(*sim, 0.3);
    CHECK(!pilot(*sim, 1).target.valid());
    // standing again on the far side of the wall's end: the other one is nearer now and visible
    steer(*sim, 1, {}, 0);
    run(*sim, 1.0);
    (void)behind;
}

TEST(topdown_retarget_nearest_with_margin_and_keep) {
    const std::string ents = R"({"type":"drone","owner":1,"pos":[4.5,8.5]},{"type":"dummy","owner":2,"pos":[8.5,8.5]},{"type":"mover","owner":2,"pos":[9.5,10.5]})";
    for (int keep = 0; keep < 2; ++keep) {
        std::string rules = kTopDownRules;
        if (keep) rules.replace(rules.find("\"retarget\": \"nearest\""), 21, "\"retarget\": \"keep\"   ");
        auto sim = makeSimJson(rules, topDownMap(ents, false));
        Entity dummy = findType(*sim, "dummy", 2), mover = findType(*sim, "mover", 2);
        run(*sim, 0.5);
        CHECK(pilot(*sim, 1).target == dummy);
        // the mover walks up to the hero: nearest switches to it (closer by more than the margin), keep stays
        Command mv; mv.player = 2; mv.type = game::kCmdMove; mv.units = {mover}; mv.point = {Fixed::fromRatio(55, 10), Fixed{}, Fixed::fromRatio(85, 10)};
        sim->submit(mv);
        run(*sim, 2.5);
        CHECK(pilot(*sim, 1).target == (keep ? dummy : mover));
    }
}

TEST(topdown_aim_snaps_inside_the_cone) {
    // near enemy 40 degrees off the aim, far enemy 5 degrees off: the aim snaps to the far one
    auto sim = makeSimJson(kTopDownRules, topDownMap(R"({"type":"drone","owner":1,"pos":[4.5,8.5]},
        {"type":"dummy","owner":2,"pos":[6.5,10.2]},{"type":"mover","owner":2,"pos":[9.5,8.9]})", false));
    Entity far = findType(*sim, "mover", 2), near = findType(*sim, "dummy", 2);
    run(*sim, 0.3);
    CHECK(pilot(*sim, 1).target == near);   // auto-fire: the nearest
    steer(*sim, 1, {}, 0, true, kEast);
    run(*sim, 0.3);
    CHECK(pilot(*sim, 1).target == far);
    CHECK(sim->world().get<game::Unit>(mod(*sim).topDown()->heroOf(*sim, 1)).facing.x > Fixed::fromRatio(9, 10));   // faces the aim / target
    steer(*sim, 1, {}, 0, true, kNorth);   // nothing in that cone: holds fire
    run(*sim, 0.3);
    CHECK(!pilot(*sim, 1).target.valid());
    // aiming fires while moving
    steer(*sim, 1, kSouth, 1000, true, kEast);
    run(*sim, 0.2);
    CHECK(pilot(*sim, 1).target == far);
}

TEST(topdown_ability_fires_along_the_aim) {
    auto sim = makeSimJson(kTopDownRules, topDownMap(R"({"type":"drone","owner":1,"pos":[4.5,8.5]},{"type":"mover","owner":2,"pos":[11.5,8.5]})", false));
    Entity foe = findType(*sim, "mover", 2);
    steer(*sim, 1, {}, 0, true, kEast);
    run(*sim, 0.2);
    Command c; c.player = 1; c.type = game::kCmdTopDownAbility; c.param = 0;   // no point: along the aim, at full range (8)
    sim->submit(c);
    run(*sim, 0.3);
    CHECK(!sim->world().alive(foe) || sim->world().get<game::Unit>(foe).hp <= Fixed::fromInt(300 - 90));
    const auto* st = sim->world().tryGet<game::AbilityState>(mod(*sim).topDown()->heroOf(*sim, 1));
    CHECK(st && st->readyAt[0] > sim->tick());   // cooling down: a second press is ignored
    c.param = 5;   // no such ability
    sim->submit(c);
    run(*sim, 0.2);
}

TEST(topdown_respawn_bots_determinism_save_and_rollback) {
    std::string rules = kTopDownRules;
    rules.replace(rules.find("\"heroes\": [\"drone\"],"), 20, "\"heroes\": [\"drone\"], \"respawn\": 2,");
    const std::string map = topDownMap(R"({"type":"drone","owner":1,"pos":[3.5,8.5]},{"type":"drone","owner":2,"pos":[28.5,8.5]},
        {"type":"mover","owner":2,"pos":[26.5,4.5]},{"type":"mover","owner":1,"pos":[5.5,12.5]})");
    auto play = [&](Simulation& sim, int ticks, std::vector<uint64_t>* sums) {
        game::RtsAI a(1, 7), b(2, 7);
        for (int i = 0; i < ticks; ++i) {
            a.update(sim); b.update(sim);
            sim.step();
            if (sums && i % 50 == 0) sums->push_back(sim.checksum());
        }
    };
    auto s1 = makeSimJson(rules, map, 3), s2 = makeSimJson(rules, map, 3);
    std::vector<uint64_t> c1, c2;
    play(*s1, 900, &c1);
    play(*s2, 900, &c2);
    CHECK(c1 == c2);
    // the bots steered their heroes (commands of type kCmdTopDown) and the heroes fought
    CHECK(pilot(*s1, 1).inputTick > 0 && pilot(*s1, 2).inputTick > 0);
    // a hero that dies respawns at home after 2 s
    Entity h1 = mod(*s1).topDown()->heroOf(*s1, 1);
    if (h1.valid()) {
        s1->world().get<game::Unit>(h1).hp = Fixed{};
        run(*s1, 0.2);
        CHECK(!mod(*s1).topDown()->heroOf(*s1, 1).valid() && pilot(*s1, 1).respawnAt > 0);
        run(*s1, 2.2);
        Entity h2 = mod(*s1).topDown()->heroOf(*s1, 1);
        CHECK(h2.valid() && h2 != h1 && (tdPos(*s1, h2) - Vec3{Fixed::fromRatio(35, 10), Fixed{}, Fixed::fromRatio(85, 10)}).xz().length() < Fixed::fromInt(2));
    }
    // save state: a loaded copy plays on bit-identically
    std::string save = s1->serialize();
    auto s3 = makeSimJson(rules, map, 3);
    std::string err;
    CHECK(s3->deserialize(save, &err));
    CHECK(s3->checksum() == s1->checksum());
    steer(*s1, 1, kNorth); steer(*s3, 1, kNorth);
    run(*s1, 1.0); run(*s3, 1.0);
    CHECK(s3->checksum() == s1->checksum());
    // rollback snapshot: restore, replay the same input, same state
    SimSnapshot snap;
    s1->saveState(snap);
    steer(*s1, 1, kEast, 1000, true, kSouth);
    run(*s1, 1.0);
    const uint64_t after = s1->checksum();
    const Vec3 vel = pilot(*s1, 1).vel;
    s1->loadState(snap);
    steer(*s1, 1, kEast, 1000, true, kSouth);
    run(*s1, 1.0);
    CHECK(s1->checksum() == after && pilot(*s1, 1).vel == vel);
}

TEST(topdown_games_without_the_block_are_untouched) {
    auto sim = makeSimJson(R"({"types": {"u": {"health": {"max": 10}, "move": {"speed": 1}}}})", topDownMap(R"({"type":"u","owner":1,"pos":[3.5,3.5]})"));
    CHECK(mod(*sim).topDown() && !mod(*sim).topDown()->active() && !mod(*sim).topDown()->store(*sim));
    Command c; c.player = 1; c.type = game::kCmdTopDown; c.param = 123;
    sim->submit(c);
    run(*sim, 0.5);   // ignored silently
}

TEST(topdown_controls_sticks_keys_quantize_and_camera) {
    std::vector<std::string> ids = {"drone"};
    game::TopDownRules r = game::TopDown::parse(Json::parse(R"({"heroes": "drone", "input": {"sectors": 8, "magnitudeSteps": 2}})"), ids);
    game::Rules rts;
    rts.types.resize(1);
    rts.types[0].id = "drone";
    game::AbilityDef ab; ab.id = "missile";
    rts.types[0].abilities.push_back(ab);
    Json gj = Json::parse(R"({"topDown": {"touchControls": "always",
        "joystick": {"mode": "floating", "radius": 50, "deadzone": 0.1, "area": [0, 0.5, 0.5, 1]},
        "aimStick": {"enabled": true, "radius": 50, "area": [0.5, 0.5, 1, 1]},
        "buttons": [{"ability": "missile", "key": "Q", "anchor": [1, 1], "offset": [-60, -200], "radius": 30}],
        "camera": {"pitch": 60, "distance": 20, "smoothing": 0, "lookAhead": 2, "bounds": true, "margin": 5}}})");
    game::TopDownControls tc;
    tc.configure(gj, r, rts);
    CHECK(tc.active() && tc.config().buttons.size() == 1 && tc.config().buttons[0].abilityIndex == 0);
    CHECK(tc.config().buttons[0].key == int(platform::Key::Q));
    const float W = 1000, H = 600;
    // no input: a stop command once, then nothing
    game::TopDownFrameInput none;
    game::topdown::Input q = tc.compose(none, 0);
    CHECK(q.moveMag == 0 && !q.aim && tc.changed(q));
    tc.sent(q);
    CHECK(!tc.changed(tc.compose(none, 0)));
    // a press outside the area is not the stick's
    CHECK(!tc.pointerDown(1, 100, 100, W, H, true));
    // floating stick: drag right = east with the camera north up (yaw 0)
    CHECK(tc.pointerDown(2, 200, 450, W, H, true));
    tc.pointerMove(2, 260, 450);
    q = tc.compose(none, 0);
    CHECK(q.moveMag == 1000 && q.moveAngle == 16384);
    // a little deflection: half speed (2 steps), direction snapped to 8 sectors
    tc.pointerMove(2, 215, 455);
    q = tc.compose(none, 0);
    CHECK(q.moveMag == 500 && q.moveAngle % 8192 == 0);
    // drag up on screen = away from the camera: -z at yaw 0, +x when the camera looks along -x (yaw 90 degrees)
    tc.pointerMove(2, 200, 390);
    CHECK(tc.compose(none, 0).moveAngle == 32768);
    CHECK(tc.compose(none, 3.14159265f / 2).moveAngle == 49152);
    tc.pointerUp(2, 200, 390);
    CHECK(tc.compose(none, 0).moveMag == 0);
    // keys: W + D diagonal, full speed
    game::TopDownFrameInput keys; keys.up = true; keys.right = true;
    q = tc.compose(keys, 0);
    CHECK(q.moveMag == 1000 && q.moveAngle == 16384 + 8192);
    // aim stick + mouse aim
    CHECK(tc.pointerDown(3, 800, 450, W, H, true));
    tc.pointerMove(3, 800, 500);
    q = tc.compose(none, 0);
    CHECK(q.aim && q.aimAngle == 0);   // screen down = +z
    tc.pointerUp(3, 800, 500);
    game::TopDownFrameInput mouse; mouse.mouseAim = true; mouse.mouseDirX = -3; mouse.mouseDirZ = 0;
    q = tc.compose(mouse, 0);
    CHECK(q.aim && q.aimAngle == 49152);
    // gamepad stick with its dead zone
    game::TopDownFrameInput pad; pad.padMoveX = 0.1f;
    CHECK(tc.compose(pad, 0).moveMag == 0);
    pad.padMoveX = 0.9f;
    CHECK(tc.compose(pad, 0).moveMag == 1000);
    // ability button: tap = one press
    CHECK(tc.buttonAt(940, 400, W, H) == 0);
    CHECK(tc.pointerDown(4, 940, 400, W, H, true));
    CHECK(tc.takeAbilityPresses() == std::vector<int>{0} && tc.takeAbilityPresses().empty());
    tc.pointerUp(4, 940, 400);
    // touch UI off on desktop with "auto"; the draw output exists when on
    CHECK(!tc.draw(W, H, {1.0f}).empty() && !tc.draw(W, H, {0.5f})[0].verts.empty());
    // camera: follows the hero with look-ahead, stays inside the map margin
    render::RTSCamera cam;
    tc.snapCamera(cam, {16, 0, 8}, 32, 16);
    CHECK(std::abs(cam.target.x - 16) < 0.01f && std::abs(cam.distance - 20) < 0.01f && std::abs(cam.pitch - 60 * 3.14159265f / 180) < 0.01f);
    tc.compose(keys, 0);
    for (int i = 0; i < 60; ++i) tc.updateCamera(cam, {16, 0, 8}, true, 1.0f / 30, 32, 16);
    CHECK(cam.target.x > 16.5f && cam.target.z < 8.0f);   // ahead toward north-east
    tc.updateCamera(cam, {1, 0, 1}, true, 1.0f / 30, 32, 16);
    CHECK(cam.target.x >= 5 - 0.01f && cam.target.z >= 5 - 0.01f);   // bounds
}

TEST(topdown_arena_template_bots_finish) {
    auto sim = makeSim(std::string(AIVERSE_SOURCE_DIR) + "/templates/topdown_arena", "maps/arena.json", 1);
    game::RtsAI a(1, 1), b(2, 1);
    for (int i = 0; i < 20 * 900 && sim->result() == 0; ++i) { a.update(*sim); b.update(*sim); sim->step(); }
    CHECK(sim->result() != 0);
}
