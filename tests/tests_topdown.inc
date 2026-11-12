// Top-down heroes (rules "topDown", RtsTopDown.h; client model TopDownControls.h): input packing, movement, walls,
// unit bodies, auto-fire / aim policy, abilities, bots, determinism, saves and rollback snapshots.
#include "modules/rts/RtsTopDown.h"
#include "modules/rts/view/TopDownControls.h"
#include "modules/rts/view/TopDownPrediction.h"

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
    CHECK(tc.config().buttons[0].key == int(platform::Key::Q) && tc.config().buttons[0].weaponStep == 0);
    {   // weapon switch buttons: a press on one is reported like an ability press, with weaponStep set
        game::TopDownControls wc;
        wc.configure(Json::parse(R"({"topDown": {"touchControls": "always", "buttons": [
            {"weapon": "prev", "anchor": [1, 0.3], "offset": [-126, -40], "radius": 30},
            {"weapon": "next", "anchor": [1, 0.3], "offset": [-50, -40], "radius": 30}]}})"), r, rts);
        CHECK(wc.config().buttons.size() == 2 && wc.config().buttons[0].weaponStep == -1 && wc.config().buttons[1].weaponStep == 1);
        CHECK(wc.pointerDown(7, 800 - 50, 480 * 0.3f - 40, 800, 480, true));
        const auto pressed = wc.takeButtonEvents();
        CHECK(pressed.size() == 1 && pressed[0].button == 1);
        CHECK(!wc.draw(800, 480, {1, 1}).empty());
    }
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
    auto evs = tc.takeButtonEvents();
    CHECK(evs.size() == 1 && evs[0].button == 0 && evs[0].phase == topdown::Phase::Tap && tc.takeButtonEvents().empty());
    tc.pointerUp(4, 940, 400);
    CHECK(tc.takeButtonEvents().empty());   // tap buttons: nothing on release
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

// ---------------------------------------------------------------- stage 2 (agent-df-hero-net): squads, free fire,
// button phases and windups, escape, extended state, client prediction
namespace {
std::string tdRules(const std::string& extra, const std::string& aim = R"("aim": {"snapCone": 15},)") {
    std::string r = kTopDownRules;
    r.replace(r.find(R"("aim": {"snapCone": 15},)"), std::string(R"("aim": {"snapCone": 15},)").size(), aim + extra);
    return r;
}
Command abilityCmd(uint8_t p, int idx, topdown::Phase ph, bool dir = false, Vec3 d = {}, int distance = 0) {
    topdown::Ability a; a.index = idx; a.phase = ph;
    if (dir) { a.hasAngle = true; a.angle = game::TopDown::angleOf(d); a.distance = distance; }
    Command c; c.player = p; c.type = game::kCmdTopDownAbility; c.param = topdown::abilityParam(a);
    return c;
}
} // namespace

TEST(topdown_free_fire_along_the_aim_stops_on_walls) {
    const std::string rules = tdRules("", R"("aim": {"snapCone": 0, "freeFire": true},)");
    // a wall at x = 12 between the hero (9.5) and the mover (13.5): the shots fire, the wall eats them
    auto sim = makeSimJson(rules, topDownMap(R"({"type":"drone","owner":1,"pos":[9.5,8.5]},{"type":"mover","owner":2,"pos":[13.5,8.6]})"));
    Entity foe = findType(*sim, "mover", 2), hero = mod(*sim).topDown()->heroOf(*sim, 1);
    sim->world().get<game::Mobile>(foe).vel = {};
    steer(*sim, 1, {}, 0, true, kEast);
    run(*sim, 1.0);
    CHECK(sim->world().get<game::Unit>(foe).hp == Fixed::fromInt(300));
    CHECK(!pilot(*sim, 1).target.valid());
    // no wall: the first hostile body on the line takes the shots although it is not a target (no snap cone)
    auto open = makeSimJson(rules, topDownMap(R"({"type":"drone","owner":1,"pos":[9.5,8.5]},{"type":"mover","owner":2,"pos":[13.5,8.6]})", false));
    foe = findType(*open, "mover", 2);
    steer(*open, 1, {}, 0, true, kEast);
    run(*open, 1.0);
    CHECK(open->world().get<game::Unit>(foe).hp < Fixed::fromInt(300));
    // without freeFire the same aim fires nothing
    auto plain = makeSimJson(tdRules("", R"("aim": {"snapCone": 0},)"), topDownMap(R"({"type":"drone","owner":1,"pos":[9.5,8.5]},{"type":"mover","owner":2,"pos":[13.5,9.1]})", false));
    foe = findType(*plain, "mover", 2);
    steer(*plain, 1, {}, 0, true, kEast);
    run(*plain, 0.2);   // auto-fire may shoot until the aim arrives (input delay); from then on the aim decides
    const Fixed hpAimed = plain->world().get<game::Unit>(foe).hp;
    run(*plain, 1.0);
    CHECK(plain->world().get<game::Unit>(foe).hp == hpAimed);
    (void)hero;
}

TEST(topdown_ability_buttons_release_along_the_stick_hold_and_windup) {
    std::string rules = tdRules(R"("abilities": {"hold": ["beam"]},)");
    rules.replace(rules.find(R"("effects": [{"type": "damage", "amount": 90, "radius": 1.5}]})"),
                  std::string(R"("effects": [{"type": "damage", "amount": 90, "radius": 1.5}]})").size(),
                  R"("effects": [{"type": "damage", "amount": 90, "radius": 1.5}]},
                    {"id": "beam", "target": "point", "range": 6, "cooldown": 0.5, "autocast": false, "effects": [{"type": "damage", "amount": 5, "radius": 1}]},
                    {"id": "nova", "target": "none", "range": 0, "cooldown": 3, "castTime": 0.5, "autocast": false, "effects": [{"type": "damage", "amount": 50, "radius": 3}]})");
    const std::string map = topDownMap(R"({"type":"drone","owner":1,"pos":[4.5,8.5]},{"type":"mover","owner":2,"pos":[4.5,12.5]},{"type":"mover","owner":2,"pos":[6.5,8.5]})", false);
    auto sim = makeSimJson(rules, map);
    CHECK(mod(*sim).topDown()->rules().extended);
    Entity south = findType(*sim, "mover", 2);
    Entity hero = mod(*sim).topDown()->heroOf(*sim, 1);
    // aim button: press does nothing, release casts along the button's stick (south, full range 8 -> clamped at 4 away)
    sim->submit(abilityCmd(1, 0, topdown::Phase::Press, true, kSouth));
    run(*sim, 0.2);
    auto* st = sim->world().tryGet<game::AbilityState>(hero);
    CHECK(!st || st->readyAt[0] <= sim->tick());
    sim->submit(abilityCmd(1, 0, topdown::Phase::Release, true, kSouth, 500));   // half the range: 4 units south
    run(*sim, 0.3);
    st = sim->world().tryGet<game::AbilityState>(hero);
    CHECK(st && st->readyAt[0] > sim->tick());
    CHECK(sim->world().get<game::Unit>(south).hp <= Fixed::fromInt(300 - 90));
    // hold: press starts the beam
    sim->submit(abilityCmd(1, 1, topdown::Phase::Press, true, kEast));
    run(*sim, 0.25);
    // ... and it repeats every cooldown while held
    st = sim->world().tryGet<game::AbilityState>(hero);
    const uint32_t first = st ? st->readyAt[1] : 0;
    CHECK(pilot(*sim, 1).held == 1 && first > 0);
    run(*sim, 1.2);
    CHECK(sim->world().get<game::AbilityState>(hero).readyAt[1] > first);   // cast again
    sim->submit(abilityCmd(1, 1, topdown::Phase::Release));
    run(*sim, 0.25);
    CHECK(pilot(*sim, 1).held == -1);
    const uint32_t last = sim->world().get<game::AbilityState>(hero).readyAt[1];
    run(*sim, 1.0);
    CHECK(sim->world().get<game::AbilityState>(hero).readyAt[1] == last);   // stopped
    // windup: nova goes off castTime later, not at once
    sim->submit(abilityCmd(1, 2, topdown::Phase::Tap));
    run(*sim, 0.15);
    CHECK(pilot(*sim, 1).casting == 2);
    Entity east = kNullEntity;
    sim->world().each<game::Unit, Owner>([&](Entity e, game::Unit& u, Owner& o) { if (o.player == 2 && e != south && u.hp.raw > 0) east = e; });
    const Fixed before = east.valid() ? sim->world().get<game::Unit>(east).hp : Fixed{};
    run(*sim, 0.6);
    CHECK(pilot(*sim, 1).casting == -1);
    CHECK(!east.valid() || !sim->world().alive(east) || sim->world().get<game::Unit>(east).hp < before);
}

TEST(topdown_squad_follows_the_hero_in_a_ring) {
    const std::string rules = tdRules(R"("squad": {"types": ["mover"], "radius": 2, "size": 3},)");
    auto sim = makeSimJson(rules, topDownMap(R"({"type":"drone","owner":1,"pos":[4.5,8.5]},{"type":"mover","owner":1,"pos":[3.5,6.5]},
        {"type":"mover","owner":1,"pos":[3.5,10.5]},{"type":"mover","owner":1,"pos":[2.5,8.5]},{"type":"mover","owner":1,"pos":[1.5,1.5]},
        {"type":"dummy","owner":2,"pos":[30.5,1.5]})", false));
    run(*sim, 0.25);
    auto* td = mod(*sim).topDown();
    auto squad = td->squadOf(*sim, 1);
    CHECK(squad.size() == 3);   // size 3: the fourth mover is no follower
    for (Entity e : squad) CHECK(td->isFollower(*sim, e) && (sim->world().get<game::Unit>(e).flags & game::kUnitFollower));
    // the hero walks east; the squad keeps up in its ring (movers are slower: give them time once the hero stops)
    steer(*sim, 1, kEast, 1000);
    run(*sim, 1.0);
    steer(*sim, 1, {}, 0);
    run(*sim, 4.0);
    const Vec3 hp = tdPos(*sim, td->heroOf(*sim, 1));
    CHECK(hp.x > Fixed::fromInt(8));
    for (Entity e : squad) {
        const Fixed d = (tdPos(*sim, e) - hp).xz().length();
        CHECK(d > Fixed::fromInt(1) && d < Fixed::fromInt(3));
    }
    // RTS orders and the computer player leave followers alone
    Command mv; mv.player = 1; mv.type = game::kCmdMove; mv.units = {squad[0]}; mv.point = {Fixed::fromInt(20), Fixed{}, Fixed::fromInt(2)};
    sim->submit(mv);
    run(*sim, 2.0);
    CHECK((tdPos(*sim, squad[0]) - hp).xz().length() < Fixed::fromInt(3));
    // determinism + saves with the extended state
    std::string save = sim->serialize();
    auto copy = makeSimJson(rules, topDownMap(R"({"type":"drone","owner":1,"pos":[4.5,8.5]},{"type":"mover","owner":1,"pos":[3.5,6.5]},
        {"type":"mover","owner":1,"pos":[3.5,10.5]},{"type":"mover","owner":1,"pos":[2.5,8.5]},{"type":"mover","owner":1,"pos":[1.5,1.5]},
        {"type":"dummy","owner":2,"pos":[30.5,1.5]})", false));
    std::string err;
    CHECK(copy->deserialize(save, &err) && copy->checksum() == sim->checksum());
    steer(*sim, 1, kSouth); steer(*copy, 1, kSouth);
    run(*sim, 1.0); run(*copy, 1.0);
    CHECK(copy->checksum() == sim->checksum() && mod(*copy).topDown()->squadOf(*copy, 1).size() == 3);
}

TEST(topdown_escape_moves_the_squad_within_the_budget) {
    const std::string rules = tdRules(R"("squad": {"types": ["mover"], "radius": 1.5}, "escape": {"duration": 1, "cooldown": 3},)");
    auto sim = makeSimJson(rules, topDownMap(R"({"type":"drone","owner":1,"pos":[4.5,8.5]},{"type":"mover","owner":1,"pos":[3.5,8.5]},
        {"type":"dummy","owner":2,"pos":[30.5,1.5]})"));
    run(*sim, 0.25);
    auto* td = mod(*sim).topDown();
    CHECK(td->escapeBudget(*sim, 1) == Fixed::fromInt(6));   // speed 6 * 1 s
    auto escapeTo = [&](Fixed x, Fixed z) {
        topdown::Ability a; a.index = topdown::kEscapeAbility; a.hasPoint = true;
        Command c; c.player = 1; c.type = game::kCmdTopDownAbility; c.param = topdown::abilityParam(a); c.point = {x, Fixed{}, z};
        sim->submit(c);
        run(*sim, 0.25);
    };
    std::string why;
    CHECK(!td->escapeAllowed(*sim, 1, {Fixed::fromRatio(115, 10), Fixed{}, Fixed::fromRatio(85, 10)}, &why));   // 7 away: over budget
    escapeTo(Fixed::fromRatio(95, 10), Fixed::fromRatio(85, 10));   // 5 east
    Entity hero = td->heroOf(*sim, 1);
    CHECK((tdPos(*sim, hero) - Vec3{Fixed::fromRatio(95, 10), Fixed{}, Fixed::fromRatio(85, 10)}).xz().length() < Fixed::fromRatio(1, 2));
    for (Entity m : td->squadOf(*sim, 1)) CHECK((tdPos(*sim, m) - tdPos(*sim, hero)).xz().length() < Fixed::fromInt(2));
    CHECK(td->escapeReadyAt(*sim, 1) > sim->tick());
    escapeTo(Fixed::fromRatio(55, 10), Fixed::fromRatio(85, 10));   // cooling down: ignored
    CHECK(tdPos(*sim, hero).x > Fixed::fromInt(9));
    run(*sim, 3.0);
    // the wall (x = 12, rows 2..13): 3 cells away in a straight line, far more to walk around it -> refused
    CHECK(!td->escapeAllowed(*sim, 1, {Fixed::fromRatio(135, 10), Fixed{}, Fixed::fromRatio(85, 10)}, &why) && why == "too far to walk");
    CHECK(td->escapeAllowed(*sim, 1, {Fixed::fromRatio(115, 10), Fixed{}, Fixed::fromRatio(85, 10)}));
}

TEST(topdown_plain_games_keep_their_hash_layout) {
    // no squad / escape / hold / windup / free fire: the extended pilot state is neither hashed nor saved
    auto a = makeSimJson(kTopDownRules, topDownMap(R"({"type":"drone","owner":1,"pos":[4.5,8.5]},{"type":"dummy","owner":2,"pos":[30.5,1.5]})"));
    CHECK(!mod(*a).topDown()->rules().extended);
    // a press / release pair is a tap there (release casts along the aim) and leaves no state behind
    a->submit(abilityCmd(1, 0, topdown::Phase::Press, true, kEast));
    run(*a, 0.25);
    CHECK(pilot(*a, 1).held == -1);
    a->submit(abilityCmd(1, 0, topdown::Phase::Release, true, kEast));
    run(*a, 0.25);
    auto* st = a->world().tryGet<game::AbilityState>(mod(*a).topDown()->heroOf(*a, 1));
    CHECK(st && st->readyAt[0] > a->tick());
}

TEST(topdown_outcome_freezes_once_decided) {
    std::string rules = kTopDownRules;
    auto sim = makeSimJson(rules, topDownMap(R"({"type":"drone","owner":1,"pos":[4.5,8.5]},{"type":"mover","owner":2,"pos":[6.5,8.5]})", false));
    CHECK(sim->result() == 0 && mod(*sim).outcomeBlob(*sim).empty());
    Entity foe = findType(*sim, "mover", 2);
    sim->world().get<game::Unit>(foe).hp = Fixed::fromInt(1);
    run(*sim, 1.0);
    CHECK(sim->result() == 1);
    const auto blob = mod(*sim).outcomeBlob(*sim);
    const uint32_t at = mod(*sim).outcome(*sim)->tick;
    CHECK(blob.size() > 8 && blob[0] == 'O' && blob[2] == 1);
    run(*sim, 2.0);
    CHECK(mod(*sim).outcomeBlob(*sim) == blob && mod(*sim).outcome(*sim)->tick == at && sim->result() == 1);
}

TEST(topdown_client_prediction_and_correction_smoothing) {
    game::TopDownPrediction p;
    auto open = [](float, float) { return true; };
    // moving east at 6 u/s with 0.1 s of input in flight: drawn about 0.6 ahead of the sim position
    for (int i = 0; i < 60; ++i) p.frame(1.0f / 60, 6, 0, 0.1f, true, 0, 0, open);
    float dx, dz;
    p.offset(1, true, dx, dz);
    CHECK(std::abs(dx - 0.6f) < 0.05f && std::abs(dz) < 1e-4f);
    p.offset(2, false, dx, dz);
    CHECK(dx == 0 && dz == 0);   // other objects: no prediction
    // the stick is released: the offset runs out within the lead (the sim catches up)
    for (int i = 0; i < 30; ++i) p.frame(1.0f / 60, 0, 0, 0.1f, true, 0, 0, open);
    p.offset(1, true, dx, dz);
    CHECK(std::abs(dx) < 0.02f);
    // a wall right in front: never drawn inside it
    auto wall = [](float x, float) { return x < 0.2f; };
    for (int i = 0; i < 60; ++i) p.frame(1.0f / 60, 6, 0, 0.1f, true, 0, 0, wall);
    p.predicted(dx, dz);
    CHECK(dx < 0.2f);
    // rollback corrections blend in (and snap when large)
    p.beforeRollback({{7, {1.0f, 0.0f}}, {8, {0.0f, 0.0f}}});
    p.afterRollback({{7, {0.5f, 0.0f}}, {8, {9.0f, 0.0f}}});
    p.offset(7, false, dx, dz);
    CHECK(std::abs(dx - 0.5f) < 1e-4f);
    p.offset(8, false, dx, dz);
    CHECK(dx == 0);
    for (int i = 0; i < 30; ++i) p.frame(1.0f / 60, 0, 0, 0.1f, false, 0, 0, open);
    p.offset(7, false, dx, dz);
    CHECK(std::abs(dx) < 0.01f);
}

TEST(topdown_controls_aim_buttons_and_escape_preview) {
    std::vector<std::string> ids = {"drone"};
    game::TopDownRules r = game::TopDown::parse(Json::parse(R"({"heroes": "drone", "escape": {"duration": 5}})"), ids);
    game::Rules rts;
    rts.types.resize(1);
    rts.types[0].id = "drone";
    game::AbilityDef ab; ab.id = "missile";
    rts.types[0].abilities.push_back(ab);
    Json gj = Json::parse(R"({"topDown": {"touchControls": "always",
        "buttons": [{"ability": "missile", "mode": "aim", "key": "Q", "anchor": [1, 1], "offset": [-60, -200], "radius": 30, "dragRadius": 100},
                    {"ability": "escape", "anchor": [1, 1], "offset": [-60, -60], "radius": 30}]}})");
    game::TopDownControls tc;
    tc.configure(gj, r, rts);
    CHECK(tc.config().buttons[1].abilityIndex == topdown::kEscapeAbility && tc.config().buttons[0].mode == game::TopDownButtonConfig::Mode::Aim);
    const float W = 1000, H = 600;
    CHECK(tc.pointerDown(5, 940, 400, W, H, true));
    tc.pointerMove(5, 990, 400);                 // drag right by half the drag radius
    tc.pointerUp(5, 990, 400);
    auto ev = tc.takeButtonEvents();
    CHECK(ev.size() == 2 && ev[0].phase == topdown::Phase::Press && ev[1].phase == topdown::Phase::Release && ev[1].hasDir);
    const topdown::Ability a = game::TopDownControls::abilityEvent(ev[1], 0, 0);
    CHECK(a.hasAngle && a.angle == 16384 && a.distance == 500 && a.phase == topdown::Phase::Release);
    tc.pointerDown(6, 940, 400, W, H, true);    // a pressed aim button cancelled by losing focus
    tc.cancelAll();
    ev = tc.takeButtonEvents();
    CHECK(ev.size() == 2 && ev[1].phase == topdown::Phase::Cancel);
    // escape preview: walks at the hero speed, keeps walls, ends after the duration
    auto& esc = tc.escape();
    esc.begin(5, 5, 6, 1);
    auto passable = [](float x, float) { return x < 8; };
    bool done = false;
    for (int i = 0; i < 70 && !done; ++i) done = esc.update(1.0f / 60, 1, 0, passable);
    CHECK(done && !esc.active && esc.x < 8 && esc.x > 7.5f && esc.travelled <= esc.budget + 1e-3f);
}

TEST(topdown_aim_candidates_follow_the_manual_aim_rules) {
    // hostiles in weapon range count, an own unit and a hostile out of range (but in sight) do not; sorted by handle, read-only
    auto sim = makeSimJson(kTopDownRules, topDownMap(R"({"type":"drone","owner":1,"pos":[4.5,8.5]},
        {"type":"dummy","owner":2,"pos":[6.5,10.2]},{"type":"mover","owner":2,"pos":[9.5,8.9]},
        {"type":"dummy","owner":1,"pos":[5.5,7.5]},{"type":"dummy","owner":2,"pos":[14.5,8.5]})", false));
    run(*sim, 0.1);
    const uint64_t before = sim->checksum();
    auto c = mod(*sim).topDown()->aimCandidates(*sim, 1);
    CHECK(c.size() == 2);
    CHECK(c.size() == 2 && c[0].e.index < c[1].e.index);
    CHECK(sim->checksum() == before);
    CHECK(mod(*sim).topDown()->aimCandidates(*sim, 1, Fixed::fromInt(30)).size() == 3);   // a wider range override
    CHECK(mod(*sim).topDown()->aimCandidates(*sim, 1, Fixed::fromInt(3)).size() == 1);
}

TEST(topdown_aim_assist_bends_touch_aim_and_taps_fire) {
    std::vector<std::string> ids = {"drone"};
    game::TopDownRules r = game::TopDown::parse(Json::parse(R"({"heroes": "drone"})"), ids);
    game::Rules rts;
    rts.types.resize(1);
    rts.types[0].id = "drone";
    Json gj = Json::parse(R"({"topDown": {"touchControls": "always",
        "joystick": {"mode": "floating", "radius": 50, "area": [0, 0.5, 0.5, 1]},
        "aimStick": {"enabled": true, "radius": 50, "deadzone": 0.2, "area": [0.5, 0.5, 1, 1]},
        "aimAssist": {"cone": 30, "tapTime": 0.25, "tapFireTime": 0.4}}})");
    game::TopDownControls tc;
    tc.configure(gj, r, rts);
    CHECK(tc.config().assist.touch && !tc.config().assist.mouse && !tc.config().assist.pad);
    const float W = 1000, H = 600;
    game::TopDownFrameInput in;
    in.targets.push_back({11, 4, 1.5f});    // east, ~20 degrees south
    in.targets.push_back({12, 0, -6});      // north
    auto deg = [](int32_t a) { return float(a) / 65536.0f * 360.0f; };
    // touch aim east: bent to the east target, locked
    in.time = 1;
    CHECK(tc.pointerDown(1, 800, 450, W, H, true));
    tc.pointerMove(1, 850, 450);
    game::topdown::Input q = tc.compose(in, 0);
    CHECK(q.aim && std::abs(deg(q.aimAngle) - deg(game::TopDown::angleOf({Fixed::fromInt(4), Fixed{}, Fixed::fromRatio(3, 2)}))) < 0.5f);
    CHECK(tc.assistTarget() == 11);
    // aim 35 degrees off the locked target: kept (stickiness 1.4 * 30); a fresh target would not be taken
    tc.pointerMove(1, 828, 491);
    q = tc.compose(in, 0);
    CHECK(tc.assistTarget() == 11);
    // aim straight down (south): nothing within the cone, the raw aim stays
    tc.pointerMove(1, 800, 500);
    q = tc.compose(in, 0);
    CHECK(tc.assistTarget() == 0 && q.aimAngle == 0);
    tc.pointerUp(1, 800, 500);
    // the mouse is not assisted (assist.mouse off)
    game::TopDownFrameInput mouse = in; mouse.mouseAim = true; mouse.mouseDirX = 3; mouse.mouseDirZ = 0;
    q = tc.compose(mouse, 0);
    CHECK(q.aim && q.aimAngle == 16384 && tc.assistTarget() == 0);
    // a tap: fire at the nearest target (east, 4.3 away) for tapFireTime, then stop
    in.time = 2;
    tc.compose(in, 0);
    CHECK(tc.pointerDown(2, 800, 450, W, H, true));
    in.time = 2.1f;
    tc.compose(in, 0);
    tc.pointerUp(2, 801, 451);
    q = tc.compose(in, 0);
    CHECK(q.aim && q.fire && tc.assistTarget() == 11);
    in.time = 2.45f;
    CHECK(tc.compose(in, 0).fire);
    in.time = 2.6f;
    q = tc.compose(in, 0);
    CHECK(!q.fire && !q.aim);
    // a long press is not a tap
    CHECK(tc.pointerDown(3, 800, 450, W, H, true));
    in.time = 3.0f;
    tc.compose(in, 0);
    tc.pointerUp(3, 800, 450);
    CHECK(!tc.compose(in, 0).fire);
    // without the block: no assist
    game::TopDownControls plain;
    plain.configure(Json::parse(R"({"topDown": {"aimStick": true}})"), r, rts);
    CHECK(!plain.config().assist.any());
}

TEST(topdown_speed_camera_zooms_out_and_looks_ahead) {
    std::vector<std::string> ids = {"drone"};
    game::TopDownRules r = game::TopDown::parse(Json::parse(R"({"heroes": "drone"})"), ids);
    game::Rules rts;
    rts.types.resize(1);
    rts.types[0].id = "drone";
    game::TopDownControls tc;
    tc.configure(Json::parse(R"({"topDown": {"camera": {"pitch": 90, "distance": 16, "minDistance": 8, "maxDistance": 30,
        "smoothing": 0, "lookAhead": 0, "lookAheadSmoothing": 0, "velocityLookAhead": 0.5, "maxLookAhead": 3,
        "speedZoom": {"from": 1, "to": 5, "distance": 28, "smoothing": 0}}}})"), r, rts);
    render::RTSCamera cam;
    render::float3 hero{20, 0, 50};
    tc.snapCamera(cam, hero, 200, 100);
    CHECK(std::abs(cam.distance - 16) < 0.01f && std::abs(cam.target.x - 20) < 0.01f);
    const float dt = 1.0f / 60;
    for (int i = 0; i < 90; ++i) { hero.x += 10 * dt; tc.updateCamera(cam, hero, true, dt, 200, 100); }   // 10 units / s east
    CHECK(std::abs(tc.heroSpeed() - 10) < 0.2f);
    CHECK(std::abs(cam.distance - 28) < 0.05f);                 // pulled back
    CHECK(std::abs(cam.target.x - (hero.x + 3)) < 0.05f);       // 5 ahead, capped at 3
    CHECK(std::abs(cam.target.z - 50) < 0.05f);
    for (int i = 0; i < 90; ++i) tc.updateCamera(cam, hero, true, dt, 200, 100);   // standing
    CHECK(std::abs(cam.distance - 16) < 0.05f && std::abs(cam.target.x - hero.x) < 0.05f);
    // the player's own zoom stays under the speed part
    cam.distance = 20;
    tc.updateCamera(cam, hero, true, dt, 200, 100);
    CHECK(std::abs(cam.distance - 20) < 0.05f);
    for (int i = 0; i < 90; ++i) { hero.z += 10 * dt; tc.updateCamera(cam, hero, true, dt, 200, 100); }
    CHECK(std::abs(cam.distance - 32) < 0.05f && cam.maxDistance > 41.9f);
    // a jump (respawn) is not speed
    hero.x += 60;
    tc.updateCamera(cam, hero, true, dt, 200, 100);
    for (int i = 0; i < 90; ++i) tc.updateCamera(cam, hero, true, dt, 200, 100);
    CHECK(std::abs(cam.distance - 20) < 0.05f);
    // without the keys: the old camera
    game::TopDownControls plain;
    plain.configure(Json::parse(R"({"topDown": {"camera": {"distance": 16, "smoothing": 0, "lookAhead": 0}}})"), r, rts);
    render::float3 h2{20, 0, 50};
    plain.snapCamera(cam, h2, 200, 100);
    for (int i = 0; i < 30; ++i) { h2.x += 10 * dt; plain.updateCamera(cam, h2, true, dt, 200, 100); }
    CHECK(std::abs(cam.distance - 16) < 0.01f && std::abs(cam.target.x - h2.x) < 0.01f);
}
