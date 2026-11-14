// Driving (rules "driving", RtsDriving.h): getting in / out, pedals from the hero's stick, walls, crash damage,
// running people over, car against car, wrecks, determinism, saves and rollback snapshots.
#include "modules/rts/RtsDriving.h"

namespace {
const char* kDrivingRules = R"J({
  "victory": "noUnits",
  "resources": {"gold": {"start": 0}},
  "topDown": {"heroes": ["guy"], "move": {"accel": 40, "decel": 60}, "autoFire": false},
  "driving": {
    "metersPerCell": 4, "enterRange": 1, "exitSpeed": 3, "controls": "tank", "wreckDamage": 30,
    "crash": {"minSpeed": 4, "damage": 5, "restitution": 0.2},
    "runOver": {"minSpeed": 3, "damage": 20},
    "vehicles": {"sedan": {"length": 1.1, "width": 0.5, "wheeled": {"massMilli": 1200000}}}
  },
  "types": {
    "guy":   {"health": {"max": 100}, "move": {"speed": 4, "radius": 0.3}, "sight": 10},
    "sedan": {"health": {"max": 400}, "move": {"speed": 0, "radius": 0.5}, "sight": 2},
    "ped":   {"health": {"max": 40}, "move": {"speed": 1, "radius": 0.3}, "sight": 4},
    "dummy": {"health": {"max": 100000}, "move": {"speed": 0, "radius": 0.4}, "sight": 2}
  }
})J";

void enterKey(Simulation& sim, uint8_t p) { Command c; c.player = p; c.type = game::kCmdTopDownEnter; sim.submit(c); }
game::Driving& drv(Simulation& sim) { return *mod(sim).driving(); }
Entity firstOf(Simulation& sim, const char* id) {
    Entity out = kNullEntity;
    auto& types = mod(sim).rules().types;
    sim.world().each<game::Unit, Transform>([&](Entity e, game::Unit& u, Transform&) {
        if (types[u.type].id == id && (!out.valid() || e.index < out.index)) out = e;
    });
    return out;
}
const char* kDriveStart = R"({"type":"guy","owner":1,"pos":[4.5,7.4]},{"type":"sedan","owner":0,"pos":[4.5,8.5]},{"type":"dummy","owner":2,"pos":[30.5,1.5]})";
} // namespace

TEST(driving_rules_parse_and_errors) {
    std::vector<std::string> ids = {"guy", "sedan"};
    auto r = game::Driving::parse(Json::parse(R"({"controls": "stick", "vehicles": {"sedan": {"length": 1.2, "width": 0.6}}})"), ids);
    CHECK(r.enabled && r.controls == game::DriveControls::Stick && r.vehicles.size() == 1 && r.vehicles[0].type == 1);
    CHECK(!game::Driving::parse(Json(), ids).enabled);
    auto fails = [&](const char* json, const char* what) {
        try { game::Driving::parse(Json::parse(json), ids); } catch (const std::exception& e) { return std::string(e.what()).find(what) != std::string::npos; }
        return false;
    };
    CHECK(fails(R"({"vehicles": {}})", "driving.vehicles: list at least one"));
    CHECK(fails(R"({"vehicles": {"bus": {}}})", "unknown type 'bus'"));
    CHECK(fails(R"({"vehicles": {"sedan": {"length": 0.4, "width": 0.6}}})", "width must not exceed length"));
    CHECK(fails(R"({"controls": "wheel", "vehicles": {"sedan": {}}})", "'tank' or 'stick'"));
    CHECK(fails(R"({"metersPerCell": 0, "vehicles": {"sedan": {}}})", "metersPerCell: must be positive"));
    // the block needs a hero to drive
    bool threw = false;
    try { makeSimJson(R"({"driving": {"vehicles": {"u": {}}}, "types": {"u": {"health": {"max": 10}, "move": {"speed": 0}}}})", topDownMap("")); }
    catch (const std::exception& e) { threw = std::string(e.what()).find("needs the 'topDown' block") != std::string::npos; }
    CHECK(threw);
}

TEST(driving_enter_drive_crash_into_a_wall_and_get_out) {
    auto sim = makeSimJson(kDrivingRules, topDownMap(kDriveStart));
    auto& w = sim->world();
    Entity hero = mod(*sim).topDown()->heroOf(*sim, 1), car = firstOf(*sim, "sedan");
    CHECK(hero.valid() && car.valid() && drv(*sim).active() && drv(*sim).car(*sim, car) && !drv(*sim).isDriving(*sim, hero));
    CHECK(w.get<game::Unit>(car).flags & game::kUnitPossessed);
    // a parked car stays where it is
    const Vec3 parked = tdPos(*sim, car);
    run(*sim, 1.0);
    CHECK(tdPos(*sim, car) == parked);
    enterKey(*sim, 1);
    run(*sim, 0.3);
    CHECK(drv(*sim).isDriving(*sim, hero) && (w.get<game::Unit>(hero).flags & game::kUnitInCargo));
    CHECK(drv(*sim).carOf(*sim, hero)->e == car && drv(*sim).carOf(*sim, hero)->player == 1);
    // stick up = throttle: the car (facing east) pulls away and takes the hero along
    steer(*sim, 1, kNorth);
    run(*sim, 2.0);
    CHECK(drv(*sim).speed(*sim, car) > Fixed::fromInt(3));
    CHECK(tdPos(*sim, car).x > parked.x + Fixed::fromInt(1) && tdPos(*sim, hero) == tdPos(*sim, car));
    CHECK(fxAbs(tdPos(*sim, car).z - parked.z) < Fixed::fromRatio(1, 10));
    // too fast to get out
    enterKey(*sim, 1);
    run(*sim, 0.2);
    CHECK(drv(*sim).isDriving(*sim, hero));
    // the wall at x = 12 stops it and dents it
    run(*sim, 6.0);
    const Vec3 p = tdPos(*sim, car);
    CHECK(p.x < Fixed::fromInt(12) - Fixed::fromRatio(1, 2) && p.x > Fixed::fromInt(10));   // nose (0.55 from the centre) at the wall
    CHECK(w.get<game::Unit>(car).hp < Fixed::fromInt(400) && w.get<game::Unit>(car).hp.raw > 0);
    // release, get out next to the driver's door (north of a car that faces east), walk away
    steer(*sim, 1, {}, 0);
    run(*sim, 1.0);
    enterKey(*sim, 1);
    run(*sim, 0.3);
    CHECK(!drv(*sim).isDriving(*sim, hero) && !(w.get<game::Unit>(hero).flags & game::kUnitInCargo));
    const Vec3 out = tdPos(*sim, hero);
    CHECK(out.z < tdPos(*sim, car).z - Fixed::fromRatio(4, 10) && fxAbs(out.x - tdPos(*sim, car).x) < Fixed::fromRatio(3, 10));
    steer(*sim, 1, kNorth);
    run(*sim, 1.0);
    CHECK(tdPos(*sim, hero).z < out.z - Fixed::fromInt(1));
    CHECK(fxAbs(tdPos(*sim, car).x - p.x) < Fixed::fromRatio(2, 10));   // the empty car stays (handbrake)
}

TEST(driving_steering_turns_the_way_the_screen_shows) {
    auto sim = makeSimJson(kDrivingRules, topDownMap(kDriveStart, false));
    Entity car = firstOf(*sim, "sedan");
    enterKey(*sim, 1);
    run(*sim, 0.3);
    steer(*sim, 1, kNorth);
    run(*sim, 2.0);
    // facing east, stick up + right: a right turn on screen bends the path south (+z)
    steer(*sim, 1, Vec3{Fixed::fromInt(1), Fixed{}, Fixed::fromInt(-1)});
    run(*sim, 1.5);
    CHECK(tdPos(*sim, car).z > Fixed::fromRatio(87, 10));
    CHECK(sim->world().get<game::Unit>(car).facing.z > Fixed::fromRatio(1, 10));
}

TEST(driving_runs_people_over_and_shoves_parked_cars) {
    auto sim = makeSimJson(kDrivingRules, topDownMap(R"({"type":"guy","owner":1,"pos":[4.5,7.4]},{"type":"sedan","owner":0,"pos":[4.5,8.5]},
        {"type":"ped","owner":2,"pos":[11.5,8.5]},{"type":"sedan","owner":0,"pos":[20.5,8.5]},{"type":"dummy","owner":2,"pos":[30.5,1.5]})", false));
    auto& w = sim->world();
    Entity car = firstOf(*sim, "sedan"), ped = firstOf(*sim, "ped");
    Entity other = kNullEntity;
    w.each<game::Unit, Transform>([&](Entity e, game::Unit& u, Transform&) { if (mod(*sim).rules().types[u.type].id == "sedan" && e != car) other = e; });
    CHECK(ped.valid() && other.valid());
    const Vec3 otherStart = tdPos(*sim, other);
    enterKey(*sim, 1);
    run(*sim, 0.3);
    steer(*sim, 1, kNorth);
    run(*sim, 4.0);
    CHECK(!w.alive(ped) || w.get<game::Unit>(ped).hp.raw <= 0);      // run over at speed
    run(*sim, 4.0);
    CHECK(tdPos(*sim, other).x > otherStart.x + Fixed::fromRatio(1, 4));   // the parked car was hit and pushed
    CHECK(w.get<game::Unit>(other).hp < Fixed::fromInt(400) && w.get<game::Unit>(car).hp < Fixed::fromInt(400));
    CHECK(tdPos(*sim, car).x < tdPos(*sim, other).x);                  // no tunnelling through it
}

TEST(driving_immobile_units_are_solid) {
    // a dummy (speed 0) on the road: the car stops against it instead of pushing it or running it over
    auto sim = makeSimJson(kDrivingRules, topDownMap(R"({"type":"guy","owner":1,"pos":[4.5,7.4]},{"type":"sedan","owner":0,"pos":[4.5,8.5]},
        {"type":"dummy","owner":2,"pos":[10.5,8.5]})", false));
    Entity car = firstOf(*sim, "sedan"), post = firstOf(*sim, "dummy");
    const Vec3 at = tdPos(*sim, post);
    enterKey(*sim, 1);
    run(*sim, 0.3);
    steer(*sim, 1, kNorth);
    run(*sim, 6.0);
    CHECK(tdPos(*sim, post) == at && sim->world().get<game::Unit>(post).hp == Fixed::fromInt(100000));
    CHECK(tdPos(*sim, car).x < at.x - Fixed::fromRatio(9, 10) && tdPos(*sim, car).x > Fixed::fromInt(8));
    CHECK(sim->world().get<game::Unit>(car).hp < Fixed::fromInt(400));
}

TEST(driving_autopilot_drives_a_car_nobody_sits_in) {
    auto sim = makeSimJson(kDrivingRules, topDownMap(kDriveStart, false));
    Entity hero = mod(*sim).topDown()->heroOf(*sim, 1), car = firstOf(*sim, "sedan");
    run(*sim, 0.5);
    const Vec3 parked = tdPos(*sim, car);
    aiverse::lib::genre::VehicleInput in;
    in.throttle = Fixed::fromInt(1);
    CHECK(drv(*sim).setAutopilot(*sim, car, in) && !drv(*sim).setAutopilot(*sim, hero, in));
    in.throttle = Fixed::fromInt(5);
    CHECK(!drv(*sim).setAutopilot(*sim, car, in));   // out of range: refused, the old input stays
    run(*sim, 2.0);
    CHECK(tdPos(*sim, car).x > parked.x + Fixed::fromInt(1) && drv(*sim).speed(*sim, car) > Fixed::fromInt(3));
    // saved and restored with the autopilot on: both copies drive on identically
    std::string save = sim->serialize();
    auto copy = makeSimJson(kDrivingRules, topDownMap(kDriveStart, false));
    std::string err;
    CHECK(copy->deserialize(save, &err) && copy->checksum() == sim->checksum());
    run(*sim, 1.0); run(*copy, 1.0);
    CHECK(copy->checksum() == sim->checksum());
    // parked again: it brakes to a stop and stays
    CHECK(drv(*sim).clearAutopilot(*sim, car));
    run(*sim, 6.0);
    const Vec3 rest = tdPos(*sim, car);
    run(*sim, 1.0);
    CHECK(tdPos(*sim, car) == rest && drv(*sim).speed(*sim, car).raw == 0);
}

TEST(driving_decks_tunnel_through_a_wall_and_bridge_over_a_parked_car) {
    aiverse::lib::genre::VehicleInput go;
    go.throttle = Fixed::fromRatio(1, 2);
    // the map has a wall at x = 12; a tunnel crosses it
    const std::string cars = kDriveStart;
    auto withDecks = [&](const std::string& entities, const char* decks, bool wall) {
        std::string map = topDownMap(entities, wall);
        map.pop_back();
        return map + ",\"decks\":" + decks + "}";
    };
    const char* tunnel = R"([{"id":"tunnel","width":1.2,"path":[[9.5,8.5,0],[11.5,8.5,-4],[13.5,8.5,-4],[15.5,8.5,0]]}])";
    {
        auto blocked = makeSimJson(kDrivingRules, topDownMap(cars));
        Entity car = firstOf(*blocked, "sedan");
        CHECK(drv(*blocked).setAutopilot(*blocked, car, go));
        run(*blocked, 6.0);
        CHECK(tdPos(*blocked, car).x < Fixed::fromInt(12) && drv(*blocked).deckOf(*blocked, car) == -1);
    }
    auto sim = makeSimJson(kDrivingRules, withDecks(cars, tunnel, true));
    auto& d = drv(*sim);
    Entity hero = mod(*sim).topDown()->heroOf(*sim, 1), car = firstOf(*sim, "sedan");
    CHECK(d.deckIndex(*sim, "tunnel") == 0 && d.deckIndex(*sim, "nope") == -1 && d.deckOf(*sim, car) == -1);
    CHECK(d.setAutopilot(*sim, car, go));
    bool inside = false, low = false;
    Fixed zMin = Fixed::fromInt(100), zMax{};
    std::string save;
    uint64_t sumInside = 0;
    for (int i = 0; i < 20 * 8 && tdPos(*sim, car).x < Fixed::fromInt(18); ++i) {
        sim->step();
        if (d.deckOf(*sim, car) == 0) {
            inside = true;
            low = low || d.height(*sim, car) <= Fixed::fromInt(-4);
            zMin = fxMin(zMin, tdPos(*sim, car).z); zMax = fxMax(zMax, tdPos(*sim, car).z);
            if (save.empty() && tdPos(*sim, car).x > Fixed::fromInt(12)) { save = sim->serialize(); sumInside = sim->checksum(); }
        }
    }
    CHECK(inside && low && tdPos(*sim, car).x >= Fixed::fromInt(18));        // it came out on the other side
    CHECK(d.deckOf(*sim, car) == -1 && d.height(*sim, car).raw == 0);
    CHECK(zMin > Fixed::fromInt(8) && zMax < Fixed::fromInt(9));
    // a save taken in the middle of the tunnel: the copy is on the deck too and drives out
    CHECK(!save.empty());
    auto copy = makeSimJson(kDrivingRules, withDecks(cars, tunnel, true));
    std::string err;
    CHECK(copy->deserialize(save, &err) && copy->checksum() == sumInside);
    CHECK(drv(*copy).deckOf(*copy, firstOf(*copy, "sedan")) == 0);
    run(*copy, 4.0);
    CHECK(tdPos(*copy, firstOf(*copy, "sedan")).x > Fixed::fromInt(16));
    (void)hero;

    // the tunnel walls: steering inside does not take the car through the map wall
    {
        auto s2 = makeSimJson(kDrivingRules, withDecks(cars, tunnel, true));
        Entity c2 = firstOf(*s2, "sedan");
        aiverse::lib::genre::VehicleInput turn = go;
        turn.steer = Fixed::fromInt(1);
        drv(*s2).setAutopilot(*s2, c2, go);
        for (int i = 0; i < 20 * 10; ++i) {
            s2->step();
            if (drv(*s2).deckOf(*s2, c2) == 0) drv(*s2).setAutopilot(*s2, c2, turn);
            const Vec3 p = tdPos(*s2, c2);
            if (p.x > Fixed::fromInt(11) && p.x < Fixed::fromInt(14)) CHECK(p.z > Fixed::fromInt(8) && p.z < Fixed::fromInt(9));
        }
    }

    // a bridge over open ground: the car on it passes over a parked car and a hero cannot get into a car up there
    const char* bridge = R"([{"id":"bridge","width":1.2,"path":[[8.5,8.5,0],[10.5,8.5,5],[16.5,8.5,5],[18.5,8.5,0]]}])";
    const std::string two = std::string(kDriveStart) + R"(,{"type":"sedan","owner":0,"pos":[13.5,8.5],"facing":0},{"type":"dummy","owner":2,"pos":[15.5,8.5]})";
    auto over = makeSimJson(kDrivingRules, withDecks(two, bridge, false));
    Entity mover = firstOf(*over, "sedan"), parked = kNullEntity;
    over->world().each<game::Unit, Transform>([&](Entity e, game::Unit& u, Transform& t) {
        if (mod(*over).rules().types[u.type].id == "sedan" && t.pos.x > Fixed::fromInt(13)) parked = e;
    });
    CHECK(parked.valid() && parked != mover);
    const Vec3 parkedAt = tdPos(*over, parked);
    drv(*over).setAutopilot(*over, mover, go);
    bool up = false;
    for (int i = 0; i < 20 * 10 && tdPos(*over, mover).x < Fixed::fromInt(21); ++i) {
        over->step();
        up = up || drv(*over).height(*over, mover) == Fixed::fromInt(5);
    }
    CHECK(up && tdPos(*over, mover).x >= Fixed::fromInt(21) && tdPos(*over, parked) == parkedAt);
    CHECK(over->world().get<game::Unit>(parked).hp == Fixed::fromInt(400) && over->world().get<game::Unit>(mover).hp == Fixed::fromInt(400));
    CHECK(drv(*over).deckOf(*over, parked) == -1);

    // bad maps are refused with a readable message
    auto bad = [&](const char* decks, const char* what) {
        try { makeSimJson(kDrivingRules, withDecks(cars, decks, false)); CHECK(false); }
        catch (const std::exception& e) { CHECK(std::string(e.what()).find(what) != std::string::npos); }
    };
    bad(R"([{"id":"a","path":[[1,1]]}])", "at least two points");
    bad(R"([{"id":"a","path":[[1,1],[1,1]]}])", "same place");
    bad(R"([{"id":"a","width":0,"path":[[1,1],[3,1]]}])", "width");
    bad(R"({"a":1})", "expected a list");
}

TEST(driving_locked_cars_stay_shut) {
    auto sim = makeSimJson(kDrivingRules, topDownMap(kDriveStart));
    Entity hero = mod(*sim).topDown()->heroOf(*sim, 1), car = firstOf(*sim, "sedan");
    run(*sim, 0.2);
    CHECK(drv(*sim).enterTarget(*sim, hero) == car && !drv(*sim).locked(*sim, car));
    CHECK(drv(*sim).setLocked(*sim, car, true) && !drv(*sim).setLocked(*sim, hero, true) && drv(*sim).locked(*sim, car));
    std::string save = sim->serialize(), err;
    auto copy = makeSimJson(kDrivingRules, topDownMap(kDriveStart));
    CHECK(copy->deserialize(save, &err) && copy->checksum() == sim->checksum() && drv(*copy).locked(*copy, firstOf(*copy, "sedan")));
    enterKey(*sim, 1);
    run(*sim, 0.3);
    CHECK(!drv(*sim).isDriving(*sim, hero) && drv(*sim).enterTarget(*sim, hero) == car);   // the door is locked
    drv(*sim).setLocked(*sim, car, false);
    enterKey(*sim, 1);
    run(*sim, 0.3);
    CHECK(drv(*sim).isDriving(*sim, hero) && !drv(*sim).enterTarget(*sim, hero).valid());
}

TEST(driving_wreck_throws_the_driver_out) {
    auto sim = makeSimJson(kDrivingRules, topDownMap(kDriveStart));
    auto& w = sim->world();
    Entity hero = mod(*sim).topDown()->heroOf(*sim, 1), car = firstOf(*sim, "sedan");
    enterKey(*sim, 1);
    run(*sim, 0.3);
    CHECK(drv(*sim).isDriving(*sim, hero));
    w.get<game::Unit>(car).hp = Fixed{};
    run(*sim, 0.3);
    CHECK(!drv(*sim).isDriving(*sim, hero) && !drv(*sim).car(*sim, car));
    CHECK(w.alive(hero) && !(w.get<game::Unit>(hero).flags & game::kUnitInCargo));
    CHECK(w.get<game::Unit>(hero).hp == Fixed::fromInt(70));           // wreckDamage 30
}

TEST(driving_determinism_save_and_rollback) {
    const std::string map = topDownMap(R"({"type":"guy","owner":1,"pos":[4.5,7.4]},{"type":"sedan","owner":0,"pos":[4.5,8.5]},
        {"type":"ped","owner":2,"pos":[9.5,8.7]},{"type":"sedan","owner":0,"pos":[8.5,4.5]},{"type":"dummy","owner":2,"pos":[30.5,1.5]})");
    auto play = [&](Simulation& sim, std::vector<uint64_t>* sums) {
        enterKey(sim, 1);
        for (int i = 0; i < 400; ++i) {
            if (i == 10) steer(sim, 1, kNorth);
            if (i == 90) steer(sim, 1, Vec3{Fixed::fromInt(-1), Fixed{}, Fixed::fromInt(-1)}, 1000, false, {});
            if (i == 200) steer(sim, 1, kSouth);
            if (i == 320) steer(sim, 1, {}, 0);
            sim.step();
            if (sums && i % 40 == 0) sums->push_back(sim.checksum());
        }
    };
    auto s1 = makeSimJson(kDrivingRules, map, 3), s2 = makeSimJson(kDrivingRules, map, 3);
    std::vector<uint64_t> c1, c2;
    play(*s1, &c1);
    play(*s2, &c2);
    CHECK(c1 == c2 && c1.front() != c1.back());
    std::string save = s1->serialize();
    auto s3 = makeSimJson(kDrivingRules, map, 3);
    std::string err;
    CHECK(s3->deserialize(save, &err));
    CHECK(s3->checksum() == s1->checksum());
    steer(*s1, 1, kNorth); steer(*s3, 1, kNorth);
    run(*s1, 1.5); run(*s3, 1.5);
    CHECK(s3->checksum() == s1->checksum());
    SimSnapshot snap;
    s1->saveState(snap);
    steer(*s1, 1, kEast);
    run(*s1, 1.0);
    const uint64_t after = s1->checksum();
    s1->loadState(snap);
    steer(*s1, 1, kEast);
    run(*s1, 1.0);
    CHECK(s1->checksum() == after);
}

TEST(driving_games_without_the_block_are_untouched) {
    auto sim = makeSimJson(kTopDownRules, topDownMap(R"({"type":"drone","owner":1,"pos":[6.5,8.5]},{"type":"dummy","owner":2,"pos":[30.5,1.5]})"));
    CHECK(mod(*sim).driving() && !mod(*sim).driving()->active() && !mod(*sim).driving()->store(*sim));
    enterKey(*sim, 1);   // ignored
    run(*sim, 0.2);
    CHECK(sim->world().alive(mod(*sim).topDown()->heroOf(*sim, 1)));
}

TEST(driving_steer_procedural_turns_front_wheels) {
    // "steer": the node turns about its axis by the host's steering angle, clamped to the limit; listed before the
    // wheel's spin the wheel rolls in its turned frame
    render::ProcAnim steer;
    steer.channel = render::ProcAnim::Rotate; steer.axis = {0, 1, 0}; steer.special = render::ProcAnim::Steer; steer.amp = 1; steer.limit = 0.5f; steer.node = 0;
    std::vector<render::ProcAnim> mods{steer};
    std::vector<render::ProcRuntime> rt;
    render::Pose pose(1);
    render::mat4 xf = render::mat4::identity();
    render::ProcInputs in;
    in.dt = 0.016f; in.steer = 0.3f;
    render::applyProcedural(mods, in, rt, pose, xf);
    CHECK(std::abs(pose[0].r.y - std::sin(0.15f)) < 1e-3f && std::abs(pose[0].r.w - std::cos(0.15f)) < 1e-3f);
    pose[0] = {};
    in.steer = -2;   // past the limit
    render::applyProcedural(mods, in, rt, pose, xf);
    CHECK(std::abs(pose[0].r.y + std::sin(0.25f)) < 1e-3f);
}
