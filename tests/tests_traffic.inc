// City traffic (rules "traffic", RtsTraffic.h, libraries/include/aiverselib/genre/Traffic.h): the road network from
// tile characters, signal phases, traffic cars that keep their lanes and stop on red, pedestrians who cross on walk,
// hijacking a traffic car, determinism, saves and rollback snapshots.
#include "modules/rts/RtsTraffic.h"

namespace {
namespace tl = lib::genre;
// 28 x 28 cells: roads (4 wide) along rows / columns 0-3, 12-15, 24-27; sidewalk rings around two by two blocks
std::vector<std::string> trafficRows() {
    std::vector<std::string> rows;
    for (int z = 0; z < 28; ++z) {
        std::string r;
        for (int x = 0; x < 28; ++x) {
            const int bx = x % 12, bz = z % 12;
            const bool road = x >= 24 || z >= 24 || bx < 4 || bz < 4;
            const bool ring = bx == 4 || bx == 11 || bz == 4 || bz == 11;
            r += road ? 'a' : ring ? 'p' : 'l';
        }
        rows.push_back(r);
    }
    return rows;
}
std::vector<uint8_t> trafficMask() {
    std::vector<uint8_t> m;
    for (auto& r : trafficRows()) for (char c : r) m.push_back(c == 'a');
    return m;
}
// island: a 2 x 2 monument (blocked, not road) in the middle of the central junction
std::string trafficMap(const std::string& entities, bool island = false) {
    std::string terrain, tiles;
    auto rows = trafficRows();
    for (size_t z = 0; z < rows.size(); ++z) {
        std::string ground(28, '.');
        if (island && (z == 13 || z == 14)) { rows[z][13] = rows[z][14] = 'l'; ground[13] = ground[14] = '#'; }
        terrain += std::string(z ? "," : "") + "\"" + ground + "\"";
        tiles += std::string(z ? "," : "") + "\"" + rows[z] + "\"";
    }
    return R"({"size":[28,28],"players":[{"name":"A","team":1},{"name":"City","team":2}],"terrain":{"rows":[)" + terrain +
           R"(]},"tiles":{"rows":[)" + tiles + "]},\"entities\":[" + entities + "]}";
}
const char* kTrafficRules = R"J({
  "victory": "noUnits",
  "resources": {"gold": {"start": 0}},
  "topDown": {"heroes": ["guy"], "move": {"accel": 40, "decel": 60}, "autoFire": false},
  "driving": {"metersPerCell": 4, "enterRange": 1.5, "vehicles": {"sedan": {"length": 1.1, "width": 0.45, "wheeled": {"massMilli": 1200000}}}},
  "traffic": {
    "roads": {"tiles": "a"},
    "cars": {"types": ["sedan"], "max": 6, "speed": [7, 9], "spawnMin": 4, "spawnMax": 40, "despawn": 60},
    "signals": {"green": 6, "yellow": 2, "allRed": 1, "walk": 4, "lights": {"green": "lamp_g", "yellow": "lamp_y", "red": "lamp_r"}},
    "pedestrians": {"walk": "p", "types": {"ped": 1}, "max": 10, "owner": 2, "spawnMin": 2, "spawnMax": 40, "despawn": 60,
                    "route": [6, 20], "pause": [0.5, 1], "driver": "ped"}
  },
  "types": {
    "guy":   {"health": {"max": 100}, "move": {"speed": 4, "radius": 0.2}, "sight": 10},
    "sedan": {"health": {"max": 400}, "move": {"speed": 0, "radius": 0.5}, "sight": 2},
    "ped":   {"health": {"max": 40}, "move": {"speed": 0.5, "radius": 0.1}, "sight": 4},
    "shelter": {"health": {"max": 1000}, "invulnerable": true, "move": {"speed": 0, "radius": 0.1}, "sight": 0},
    "dummy": {"health": {"max": 100000}, "invulnerable": true, "move": {"speed": 0, "radius": 0.3}, "sight": 2},
    "lamp_g": {"health": {"max": 1000}, "invulnerable": true, "move": {"speed": 0, "radius": 0.05}, "sight": 0},
    "lamp_y": {"health": {"max": 1000}, "invulnerable": true, "move": {"speed": 0, "radius": 0.05}, "sight": 0},
    "lamp_r": {"health": {"max": 1000}, "invulnerable": true, "move": {"speed": 0, "radius": 0.05}, "sight": 0}
  }
})J";
const char* kTrafficHero = R"({"type":"guy","owner":1,"pos":[8.5,4.5]},{"type":"dummy","owner":2,"pos":[8.5,8.5]})";
game::Traffic& trf(Simulation& sim) { return *mod(sim).traffic(); }
game::TrafficStore& tstore(Simulation& sim) { return *trf(sim).store(sim); }
// the traffic the strip crosses is held (the walk window of the other axis, without the start margin)
bool walkNow(Simulation& sim, int cell) {
    auto& t = *mod(sim).traffic();
    const int strip = t.stripAt(cell);
    if (strip < 0) return true;
    const auto& n = t.net();
    const auto& p = n.pieces[size_t(n.pieceAt[size_t(cell)])];
    const int along = p.axis == tl::TrafficAxis::X ? cell % n.w : cell / n.w;
    const int j = p.junction[along == p.along0 ? 0 : 1];
    if (j < 0 || !n.junctions[size_t(j)].signal) return true;
    return tl::trafficWalk(t.rules().timing, t.offset(j), sim.tick(), p.axis);
}
int junctionOfCell(const tl::TrafficNet& n, Vec3 p) {
    const int x = int(p.x.floorInt()), z = int(p.z.floorInt());
    if (x < 0 || z < 0 || x >= n.w || z >= n.h) return -1;
    return n.junctionAt[size_t(z * n.w + x)];
}
} // namespace

TEST(traffic_network_lanes_and_signal_phases) {
    const auto n = tl::buildTrafficNet(28, 28, trafficMask(), {});
    CHECK(n.junctions.size() == 9 && n.pieces.size() == 12);
    int signals = 0, four = 0;
    for (auto& j : n.junctions) { signals += j.signal; four += j.arms() == 4; }
    CHECK(signals == 5 && four == 1);   // corners have two arms: no signal
    for (auto& p : n.pieces) CHECK(p.lanes == 2 && p.width() == 4 && p.length() == 8);
    // the piece along x between the middle junction and the east one: rows 12-15, columns 16-23
    const int pi = n.pieceAt[size_t(13 * 28 + 18)];
    CHECK(pi >= 0);
    const auto& p = n.pieces[size_t(pi)];
    CHECK(p.axis == tl::TrafficAxis::X && p.along0 == 16 && p.along1 == 23 && p.junction[0] >= 0 && p.junction[1] >= 0);
    CHECK(tl::trafficLaneAcross(n, p, 1, 0) == Fixed::fromRatio(29, 2));    // eastbound keeps right: +z of the centre line
    CHECK(tl::trafficLaneAcross(n, p, -1, 1) == Fixed::fromRatio(25, 2));   // westbound outer lane
    tl::TrafficNetConfig lh; lh.leftHand = true; lh.parkingLanes = 1;
    const auto m = tl::buildTrafficNet(28, 28, trafficMask(), lh);
    CHECK(m.pieces[size_t(pi)].lanes == 1 && tl::trafficLaneAcross(m, m.pieces[size_t(pi)], 1, 0) == Fixed::fromRatio(27, 2));
    // phases: X green, yellow, all red, then Z; walking across a road while the other axis is green
    tl::TrafficSignalTiming t; t.green = 10; t.yellow = 3; t.allRed = 2; t.walk = 6;
    CHECK(tl::trafficLight(t, 0, 0, tl::TrafficAxis::X) == tl::TrafficLight::Green && tl::trafficLight(t, 0, 0, tl::TrafficAxis::Z) == tl::TrafficLight::Red);
    CHECK(tl::trafficLight(t, 0, 11, tl::TrafficAxis::X) == tl::TrafficLight::Yellow && tl::trafficLight(t, 0, 14, tl::TrafficAxis::X) == tl::TrafficLight::Red);
    CHECK(tl::trafficLight(t, 0, 16, tl::TrafficAxis::Z) == tl::TrafficLight::Green && tl::trafficLight(t, 0, 16, tl::TrafficAxis::X) == tl::TrafficLight::Red);
    CHECK(tl::trafficLight(t, 15, 1, tl::TrafficAxis::Z) == tl::TrafficLight::Green);   // the offset shifts the cycle
    CHECK(tl::trafficWalk(t, 0, 2, tl::TrafficAxis::Z) && !tl::trafficWalk(t, 0, 8, tl::TrafficAxis::Z) && !tl::trafficWalk(t, 0, 2, tl::TrafficAxis::X));
    CHECK(tl::trafficUntilGreen(t, 0, 0, tl::TrafficAxis::Z) == 15 && tl::trafficUntilGreen(t, 0, 3, tl::TrafficAxis::X) == 0);
    // pure pursuit pulls a car beside its lane back onto it
    const Vec2 tgt = tl::trafficPursuit({Fixed::fromInt(2), Fixed::fromInt(1)}, {{Fixed{}, Fixed{}}, {Fixed::fromInt(10), Fixed{}}}, Fixed::fromInt(1));
    CHECK(fxAbs(tgt.x - Fixed::fromInt(3)) < Fixed::fromRatio(1, 100) && tgt.y == Fixed{});
}

TEST(traffic_rules_parse_and_errors) {
    std::vector<std::string> ids = {"guy", "sedan", "ped"};
    auto r = game::Traffic::parse(Json::parse(R"({"roads": {"tiles": "a", "side": "left"}, "cars": {"types": {"sedan": 3}}, "signals": {"green": 5}})"), ids, 20);
    CHECK(r.enabled && r.net.leftHand && r.cars.size() == 1 && r.cars[0].weight == 3 && r.timing.green == 100);
    CHECK(!game::Traffic::parse(Json(), ids, 20).enabled);
    CHECK(!game::Traffic::parse(Json::parse(R"({"enabled": false})"), ids, 20).enabled);
    auto fails = [&](const char* json, const char* what) {
        try { game::Traffic::parse(Json::parse(json), ids, 20); } catch (const std::exception& e) { return std::string(e.what()).find(what) != std::string::npos; }
        return false;
    };
    CHECK(fails(R"({"cars": {}})", "traffic.roads.tiles"));
    CHECK(fails(R"({"roads": {"tiles": "a"}, "cars": {"types": ["bus"]}})", "unknown type 'bus'"));
    CHECK(fails(R"({"roads": {"tiles": "a", "side": "middle"}})", "'right' or 'left'"));
    CHECK(fails(R"({"roads": {"tiles": "a"}, "pedestrians": {"types": ["ped"]}})", "traffic.pedestrians.walk"));
    CHECK(fails(R"({"roads": {"tiles": "a"}, "cars": {"spawnMin": 30, "spawnMax": 20}})", "spawnMin < spawnMax"));
    CHECK(fails(R"({"roads": {"tiles": "a"}, "signals": {"offset": "zig"}})", "'random', 'wave' or 'none'"));
    // traffic cars must be driving vehicles
    bool threw = false;
    try {
        std::string rules = kTrafficRules;
        rules.replace(rules.find(R"("types": ["sedan"])"), 18, R"("types": ["ped"])");
        makeSimJson(rules, trafficMap(kTrafficHero));
    } catch (const std::exception& e) { threw = std::string(e.what()).find("'ped' is not in driving.vehicles") != std::string::npos; }
    CHECK(threw);
}

TEST(traffic_cars_keep_moving_and_never_enter_on_red) {
    auto sim = makeSimJson(kTrafficRules, trafficMap(kTrafficHero), 7);
    auto& w = sim->world();
    const auto& net = trf(*sim).net();
    run(*sim, 3.0);
    CHECK(tstore(*sim).cars.size() >= 3 && tstore(*sim).lights.size() == 5 * 4 - 4);   // the rest appear out of sight later   // 4 arms in the middle, 3 at the edges
    std::map<uint32_t, int> inBox;         // car -> junction it is in (-1 outside)
    std::map<uint32_t, Fixed> travelled;
    std::map<uint32_t, Vec3> last;
    int entries = 0, onRed = 0;
    for (int i = 0; i < 60 * int(sim->tickRate()); ++i) {
        sim->step();
        for (auto& c : tstore(*sim).cars) {
            const Vec3 p = w.get<Transform>(c.e).pos;
            if (last.count(c.e.index)) travelled[c.e.index] += (p - last[c.e.index]).length();
            last[c.e.index] = p;
            const int j = junctionOfCell(net, p);
            auto it = inBox.find(c.e.index);
            if (it != inBox.end() && it->second < 0 && j >= 0 && net.junctions[size_t(j)].signal) {
                ++entries;
                const Vec3 f = w.get<game::Unit>(c.e).facing;
                const auto axis = fxAbs(f.x) > fxAbs(f.z) ? tl::TrafficAxis::X : tl::TrafficAxis::Z;
                // a car may still cross on yellow and in the all-red gap after it; never once the other way is green
                if (trf(*sim).light(*sim, j, axis) == tl::TrafficLight::Red && trf(*sim).light(*sim, j, axis == tl::TrafficAxis::X ? tl::TrafficAxis::Z : tl::TrafficAxis::X) == tl::TrafficLight::Green) ++onRed;
            }
            inBox[c.e.index] = j;
        }
    }
    CHECK(entries >= 10);
    CHECK(onRed == 0);
    int moving = 0;
    for (auto& [e, d] : travelled) moving += d > Fixed::fromInt(15);
    CHECK(moving >= 4);
    CHECK(tstore(*sim).cars.size() == 6);
    // the props show the signals
    int red = 0;
    for (auto& l : tstore(*sim).lights) {
        const std::string id = mod(*sim).rules().types[w.get<game::Unit>(l.e).type].id;
        red += id == "lamp_r";
        // set while the tick ran (sim.tick() one lower than now)
        CHECK(l.shown == uint8_t(tl::trafficLight(trf(*sim).rules().timing, trf(*sim).offset(l.junction), sim->tick() - 1, tl::TrafficAxis(l.axis))));
    }
    CHECK(red > 0);
}

TEST(traffic_pedestrians_walk_the_sidewalks_and_cross_on_walk) {
    std::string rules = kTrafficRules;
    rules.replace(rules.find(R"("max": 6)"), 8, R"("max": 0)");
    auto sim = makeSimJson(rules, trafficMap(kTrafficHero), 11);
    auto& w = sim->world();
    run(*sim, 3.0);
    CHECK(tstore(*sim).people.size() >= 4);
    const int W = trf(*sim).net().w;
    std::map<uint32_t, int> lastCell;
    std::map<uint32_t, Vec3> start;
    for (auto& p : tstore(*sim).people) start[p.e.index] = w.get<Transform>(p.e).pos;
    int stepsOnRoad = 0, crossings = 0, badCrossings = 0;
    for (int i = 0; i < 90 * int(sim->tickRate()); ++i) {
        sim->step();
        for (auto& p : tstore(*sim).people) {
            const Vec3 pos = w.get<Transform>(p.e).pos;
            const int cell = int(pos.z.floorInt()) * W + int(pos.x.floorInt());
            const int strip = trf(*sim).stripAt(cell);
            const auto it = lastCell.find(p.e.index);
            if (strip >= 0 && it != lastCell.end() && trf(*sim).stripAt(it->second) < 0) {
                ++crossings;
                // it sets off within the walk window (its body reaches the strip a moment later)
                // inside the walk window (a walker who set off in time may reach the strip a moment later)
                if (!walkNow(*sim, cell) && !p.panic) ++badCrossings;
            }
            if (trf(*sim).onRoad({pos.x, pos.z}) && strip < 0) ++stepsOnRoad;
            lastCell[p.e.index] = cell;
        }
    }
    CHECK(crossings >= 3);
    CHECK(badCrossings <= crossings / 8);   // pushed onto the kerb by the crowd at most
    CHECK(stepsOnRoad < 90 * int(sim->tickRate()) / 10);   // only brushing past the curb, never walking down the road
    int walked = 0;
    for (auto& p : tstore(*sim).people) if (start.count(p.e.index) && (w.get<Transform>(p.e).pos - start[p.e.index]).length() > Fixed::fromInt(3)) ++walked;
    CHECK(walked >= int(start.size()) / 2 && walked >= 2);
    CHECK(tstore(*sim).people.size() == 10);
}

TEST(traffic_hijack_throws_the_driver_out) {
    auto sim = makeSimJson(kTrafficRules, trafficMap(kTrafficHero), 5);
    auto& w = sim->world();
    run(*sim, 2.0);
    Entity hero = mod(*sim).topDown()->heroOf(*sim, 1);
    CHECK(!tstore(*sim).cars.empty());
    const Entity car = tstore(*sim).cars.front().e;
    // stand next to the car
    const Vec3 cp = w.get<Transform>(car).pos;
    w.get<Transform>(hero).pos = cp + Vec3{Fixed{}, Fixed{}, Fixed::fromRatio(1, 2)};
    const size_t people = tstore(*sim).people.size();
    Command c; c.player = 1; c.type = game::kCmdTopDownEnter; sim->submit(c);
    run(*sim, 0.3);
    CHECK(mod(*sim).driving()->isDriving(*sim, hero) && mod(*sim).driving()->carOf(*sim, hero)->e == car);
    bool still = false;
    for (auto& tc : tstore(*sim).cars) still = still || tc.e == car;
    CHECK(!still);
    CHECK(tstore(*sim).people.size() >= people + 1);
    bool runner = false;
    for (auto& p : tstore(*sim).people) runner = runner || p.panic;
    CHECK(runner);
}

TEST(traffic_determinism_save_and_rollback) {
    const std::string map = trafficMap(kTrafficHero);
    auto play = [&](Simulation& sim, std::vector<uint64_t>* sums) {
        for (int i = 0; i < 600; ++i) {
            if (i == 100) steer(sim, 1, kSouth);
            if (i == 160) steer(sim, 1, {}, 0);
            sim.step();
            if (sums && i % 50 == 0) sums->push_back(sim.checksum());
        }
    };
    auto s1 = makeSimJson(kTrafficRules, map, 3), s2 = makeSimJson(kTrafficRules, map, 3);
    std::vector<uint64_t> c1, c2;
    play(*s1, &c1);
    play(*s2, &c2);
    CHECK(c1 == c2 && c1.front() != c1.back());
    std::string save = s1->serialize();
    auto s3 = makeSimJson(kTrafficRules, map, 3);
    std::string err;
    CHECK(s3->deserialize(save, &err));
    CHECK(s3->checksum() == s1->checksum());
    run(*s1, 5); run(*s3, 5);
    CHECK(s3->checksum() == s1->checksum());
    SimSnapshot snap;
    s1->saveState(snap);
    run(*s1, 3);
    const uint64_t after = s1->checksum();
    s1->loadState(snap);
    run(*s1, 3);
    CHECK(s1->checksum() == after);
    // a map can switch the traffic off
    std::string off = map;
    off.insert(off.size() - 1, R"(,"traffic":false)");
    auto s4 = makeSimJson(kTrafficRules, off, 3);
    CHECK(!trf(*s4).active() && !trf(*s4).store(*s4));
}

// rules with one of the traffic sub-blocks replaced: `"key": {old...}` -> text
namespace {
std::string trafficRulesWith(const std::string& from, const std::string& to) {
    std::string rules = kTrafficRules;
    const size_t at = rules.find(from);
    if (at == std::string::npos) throw std::runtime_error("trafficRulesWith: '" + from + "' not found");
    rules.replace(at, from.size(), to);
    return rules;
}
} // namespace

TEST(traffic_buses_call_at_stops_and_emergency_vehicles_run) {
    // no ordinary cars: one bus (a stop object on the north road and a stop on every piece), an emergency call-out every 1..2 s
    const std::string rules = trafficRulesWith(R"("cars": {"types": ["sedan"], "max": 6,)",
        R"("buses": {"types": ["sedan"], "max": 1, "stops": ["shelter"], "autoStops": 1, "dwell": [1, 1], "speed": [6, 7]},
           "emergency": {"types": ["sedan"], "max": 1, "interval": [1, 2], "speed": [12, 13]},
           "cars": {"types": ["sedan"], "max": 0,)");
    auto sim = makeSimJson(rules, trafficMap(std::string(kTrafficHero) + R"(,{"type":"shelter","owner":0,"pos":[20.5,4.4]})"), 9);
    auto& w = sim->world();
    int stopsMade = 0, lastStop = -1;
    bool sawEmergency = false, dwelled = false;
    Fixed emSpeed{}, busDist{};
    std::map<uint32_t, Vec3> last;
    for (int i = 0; i < 90 * int(sim->tickRate()); ++i) {
        sim->step();
        int buses = 0;
        for (auto& c : tstore(*sim).cars) {
            CHECK(c.kind != game::kTrafficCar);
            CHECK(trf(*sim).kindOf(*sim, c.e) == int(c.kind));
            const Vec3 p = w.get<Transform>(c.e).pos;
            if (c.kind == game::kTrafficBus) {
                ++buses;
                if (last.count(c.e.index)) busDist += (p - last[c.e.index]).length();
                if (c.lastStop >= 0 && c.lastStop != lastStop) { ++stopsMade; lastStop = c.lastStop; }
                if (sim->tick() < c.dwellUntil) { dwelled = true; CHECK(mod(*sim).driving()->speed(*sim, c.e) < Fixed::fromInt(1)); }   // standing at the stop
            } else {
                sawEmergency = true;
                emSpeed = fxMax(emSpeed, mod(*sim).driving()->speed(*sim, c.e));
            }
            last[c.e.index] = p;
        }
        CHECK(buses <= 1);
    }
    CHECK(stopsMade >= 2 && dwelled);
    CHECK(busDist > Fixed::fromInt(20));
    CHECK(sawEmergency && emSpeed > Fixed::fromInt(9));   // faster than any ordinary car of these rules
    CHECK(trf(*sim).kindOf(*sim, mod(*sim).topDown()->heroOf(*sim, 1)) == -1);
}

TEST(traffic_light_props_only_near_the_heroes_and_parked_cars_sleep) {
    // lights within 8 cells of the hero; a parked car far from everything
    const std::string rules = trafficRulesWith(R"("signals": {"green": 6,)", R"("signals": {"radius": 8, "green": 6,)");
    auto sim = makeSimJson(rules, trafficMap(std::string(kTrafficHero) + R"(,{"type":"sedan","owner":0,"pos":[20.5,20.5]})"), 4);
    auto& w = sim->world();
    run(*sim, 1.0);
    // the hero stands at (8.5, 4.5): only the junction at the top (centre 14, 2) is within 8 cells -> its three arms
    CHECK(tstore(*sim).lit.size() == 1 && tstore(*sim).lights.size() == 3);
    const int top = tstore(*sim).lit[0];
    CHECK(trf(*sim).net().junctions[size_t(top)].centre().y == Fixed::fromInt(2));
    // signals still answer everywhere (phases are a function of the tick, not of the props)
    CHECK(trf(*sim).junctionAt(Fixed::fromInt(14), Fixed::fromInt(14)) >= 0);
    // the hero goes to the middle of the map: the central junction lights up, the top one is cleared away
    w.get<Transform>(mod(*sim).topDown()->heroOf(*sim, 1)).pos = Vec3{Fixed::fromRatio(21, 2), Fixed{}, Fixed::fromRatio(33, 2)};
    run(*sim, 1.0);
    CHECK(tstore(*sim).lit.size() == 1 && tstore(*sim).lit[0] != top && tstore(*sim).lights.size() == 4);
    for (auto& l : tstore(*sim).lights) CHECK(w.alive(l.e) && l.junction == tstore(*sim).lit[0]);
    // the parked car (not traffic, nobody in it) never steps its controller
    Entity parked = kNullEntity;
    w.each<game::Unit, Transform>([&](Entity e, game::Unit&, Transform& t) { if (t.pos.x == Fixed::fromRatio(41, 2) && t.pos.z == Fixed::fromRatio(41, 2)) parked = e; });
    CHECK(parked.valid() && mod(*sim).driving()->car(*sim, parked) && trf(*sim).kindOf(*sim, parked) == -1);
    CHECK(mod(*sim).driving()->car(*sim, parked)->st.lastTick == 0);
}

TEST(traffic_goes_round_an_island_in_a_junction) {
    auto sim = makeSimJson(kTrafficRules, trafficMap(kTrafficHero, true), 7);
    auto& w = sim->world();
    const auto& net = trf(*sim).net();
    const int centre = net.junctionAt[size_t(12 * 28 + 12)];
    CHECK(centre >= 0 && net.junctionAt[size_t(13 * 28 + 13)] < 0 && net.junctions[size_t(centre)].arms() == 4);
    std::map<uint32_t, int> inBox;
    std::map<uint32_t, Fixed> travelled;
    std::map<uint32_t, Vec3> last;
    int through = 0;
    for (int i = 0; i < 120 * int(sim->tickRate()); ++i) {
        sim->step();
        for (auto& c : tstore(*sim).cars) {
            const Vec3 p = w.get<Transform>(c.e).pos;
            if (last.count(c.e.index)) travelled[c.e.index] += (p - last[c.e.index]).length();
            last[c.e.index] = p;
            const int j = junctionOfCell(net, p);
            if (inBox.count(c.e.index) && inBox[c.e.index] == centre && j != centre) ++through;   // left the ring
            inBox[c.e.index] = j;
            // never inside the monument
            CHECK(!(p.x > Fixed::fromRatio(131, 10) && p.x < Fixed::fromRatio(149, 10) && p.z > Fixed::fromRatio(131, 10) && p.z < Fixed::fromRatio(149, 10)));
        }
    }
    CHECK(through >= 6);
    int moving = 0;
    for (auto& [e, d] : travelled) moving += d > Fixed::fromInt(30);
    CHECK(moving >= 4);
}

// ------------------------------------------------------------------ the living city: incidents and services
namespace {
const char* kTrafficCityRules = R"J({
  "victory": "noUnits",
  "resources": {"gold": {"start": 0}},
  "topDown": {"heroes": ["guy"], "move": {"accel": 40, "decel": 60}, "autoFire": false},
  "driving": {"metersPerCell": 4, "enterRange": 1.5, "vehicles": {"sedan": {"length": 1.1, "width": 0.45, "wheeled": {"massMilli": 1200000}}}},
  "traffic": {
    "roads": {"tiles": "a"},
    "cars": {"types": ["sedan"], "max": 0, "spawnMin": 4, "spawnMax": 40, "despawn": 60},
    "signals": {"green": 6, "yellow": 2, "allRed": 1, "walk": 4},
    "pedestrians": {"walk": "p", "types": {"ped": 1}, "max": 0, "owner": 2, "spawnMin": 2, "spawnMax": 40, "despawn": 60},
    "services": {
      "ambulance": {"answers": "injured", "types": ["sedan"], "stations": ["hospital"], "crew": "medic", "crewCount": 2, "work": 2, "speed": [10, 11]},
      "fire": {"answers": "fire", "types": ["sedan"], "stations": ["hospital"], "work": 3, "speed": [10, 11]},
      "tow": {"answers": "wreck", "types": ["sedan"], "work": 2, "speed": [8, 9]},
      "police": {"answers": "crime", "types": ["sedan"], "stations": ["hospital"], "crew": "cop", "crewCount": 2, "work": 4, "maxWork": 20, "owner": 2, "speed": [10, 11]}
    },
    "incidents": {"body": "body", "wreck": "wreck", "fire": "fire", "burn": 200, "fireDamage": 0, "treat": 5, "witness": 6, "lots": ["lot"], "abandon": 0}
  },
  "types": {
    "guy":   {"health": {"max": 500}, "move": {"speed": 4, "radius": 0.2}, "sight": 10},
    "sedan": {"health": {"max": 400}, "move": {"speed": 0, "radius": 0.5}, "sight": 2},
    "ped":   {"health": {"max": 40}, "move": {"speed": 0.5, "radius": 0.1}, "sight": 4},
    "medic": {"health": {"max": 40}, "move": {"speed": 1.5, "radius": 0.1}, "sight": 4},
    "cop":   {"health": {"max": 80}, "move": {"speed": 1.5, "radius": 0.1}, "sight": 8,
              "weapons": [{"id": "gun", "range": 4, "damage": 5, "cooldown": 0.5, "projectileSpeed": 30}]},
    "body":  {"health": {"max": 1000}, "invulnerable": true, "move": {"speed": 0, "radius": 0.05}, "sight": 0},
    "wreck": {"health": {"max": 1000}, "invulnerable": true, "move": {"speed": 0, "radius": 0.4}, "sight": 0},
    "fire":  {"health": {"max": 1000}, "invulnerable": true, "move": {"speed": 0, "radius": 0.05}, "sight": 0},
    "hospital": {"health": {"max": 1000}, "invulnerable": true, "move": {"speed": 0, "radius": 0.1}, "sight": 0},
    "lot":   {"health": {"max": 1000}, "invulnerable": true, "move": {"speed": 0, "radius": 0.1}, "sight": 0},
    "dummy": {"health": {"max": 100000}, "invulnerable": true, "move": {"speed": 0, "radius": 0.3}, "sight": 2}
  }
})J";
// the hero on the north sidewalk, a station object on the same road two blocks east, a lot inside the south-east block
const char* kTrafficCityStart = R"({"type":"guy","owner":1,"pos":[8.5,4.5]},{"type":"dummy","owner":2,"pos":[8.5,8.5]},
    {"type":"hospital","owner":0,"pos":[20.5,4.4]},{"type":"lot","owner":0,"pos":[20.5,18.5]})";
int countOfType(Simulation& sim, const char* id) {
    int n = 0;
    auto& types = mod(sim).rules().types;
    sim.world().each<game::Unit, Transform>([&](Entity, game::Unit& u, Transform&) { if (u.hp.raw > 0 && types[u.type].id == id) ++n; });
    return n;
}
const game::TrafficIncident* incidentKind(Simulation& sim, uint8_t kind) {
    for (auto& i : tstore(sim).incidents) if (i.kind == kind) return &i;
    return nullptr;
}
} // namespace

TEST(traffic_city_ambulance_takes_the_injured_to_hospital_and_he_walks_out) {
    auto sim = makeSimJson(kTrafficCityRules, trafficMap(std::string(kTrafficCityStart) + R"(,{"type":"ped","owner":2,"pos":[9.5,4.5]})"), 2);
    auto& w = sim->world();
    run(*sim, 0.5);
    const Entity ped = firstOf(*sim, "ped");
    CHECK(ped.valid() && tstore(*sim).incidents.empty());
    mod(*sim).dealDamage(*sim, ped, Fixed::fromInt(60), 0, kNullEntity, 0);   // knocked down: a blow of 1.5 x his 40 hp
    trf(*sim).collect(*sim);   // (the hit happened between two steps)
    run(*sim, 0.5);
    CHECK(countOfType(*sim, "ped") == 0 && countOfType(*sim, "body") == 1);
    const game::TrafficIncident* inc = incidentKind(*sim, game::kIncidentInjured);
    CHECK(inc && inc->victim == uint16_t(mod(*sim).rules().findType("ped")) && !inc->fatal);
    bool coming = false, medics = false, worked = false;
    int taken = -1, out = -1;
    Fixed far{};
    for (int i = 0; i < 200 * int(sim->tickRate()); ++i) {
        sim->step();
        for (auto& c : tstore(*sim).cars) {
            if (c.task == game::kTaskGoing) { coming = true; CHECK(c.kind == game::kTrafficEmergency && c.homePiece >= 0); far = fxMax(far, (w.get<Transform>(c.e).pos - Vec3{Fixed::fromRatio(41, 2), Fixed{}, Fixed::fromRatio(5, 2)}).length()); }
            if (c.task == game::kTaskCrewOut) { medics = true; CHECK(c.crew.size() == 2 && countOfType(*sim, "medic") == 2); }
            if (c.task == game::kTaskWorking) worked = true;
        }
        if (taken < 0 && countOfType(*sim, "body") == 0) { taken = i; CHECK(tstore(*sim).patients.size() == 1 && !incidentKind(*sim, game::kIncidentInjured)); }
        if (taken >= 0 && out < 0 && countOfType(*sim, "ped") == 1) out = i;
        if (out >= 0 && tstore(*sim).cars.empty()) break;
    }
    CHECK(coming && medics && worked && far > Fixed::fromInt(8));
    CHECK(taken > 0 && out > taken + 4 * int(sim->tickRate()));   // treated for 5 s first
    CHECK(countOfType(*sim, "medic") == 0 && tstore(*sim).cars.empty());   // crew back in, the ambulance drove into its station
    CHECK(tstore(*sim).patients.empty() && tstore(*sim).people.size() == 1);
    // he walks out next to the hospital
    if (!tstore(*sim).people.empty()) {
        const Vec3 p = w.get<Transform>(tstore(*sim).people[0].e).pos;
        CHECK((p - Vec3{Fixed::fromRatio(41, 2), Fixed{}, Fixed::fromRatio(22, 5)}).length() < Fixed::fromInt(12));
    }
}

TEST(traffic_city_a_fatal_blow_is_taken_to_hospital_too_but_never_comes_back) {
    auto sim = makeSimJson(kTrafficCityRules, trafficMap(std::string(kTrafficCityStart) + R"(,{"type":"ped","owner":2,"pos":[9.5,4.5]})"), 2);
    run(*sim, 0.5);
    mod(*sim).dealDamage(*sim, firstOf(*sim, "ped"), Fixed::fromInt(200), 0, kNullEntity, 0);   // a rocket: five times his 40 hp
    trf(*sim).collect(*sim);
    run(*sim, 0.5);
    const game::TrafficIncident* inc = incidentKind(*sim, game::kIncidentInjured);
    CHECK(inc && inc->fatal && countOfType(*sim, "body") == 1);
    bool medics = false;
    int taken = -1;
    for (int i = 0; i < 120 * int(sim->tickRate()) && (taken < 0 || i < taken + 15 * int(sim->tickRate())); ++i) {
        sim->step();
        medics = medics || countOfType(*sim, "medic") == 2;
        if (taken < 0 && countOfType(*sim, "body") == 0) taken = i;
        CHECK(tstore(*sim).patients.empty());
    }
    CHECK(medics && taken > 0);                 // the ambulance came for him all the same
    CHECK(countOfType(*sim, "ped") == 0 && tstore(*sim).people.empty());   // 15 s later (treat is 5): nobody walked out
}

TEST(traffic_city_fire_engine_first_then_the_tow_truck_takes_the_wreck_to_the_lot) {
    // a parked car on the north road blows up
    auto sim = makeSimJson(kTrafficCityRules, trafficMap(std::string(kTrafficCityStart) + R"(,{"type":"sedan","owner":0,"pos":[9.5,2.5],"facing":90})"), 2);
    run(*sim, 0.5);
    const Entity car = firstOf(*sim, "sedan");
    mod(*sim).dealDamage(*sim, car, Fixed::fromInt(100000), 0, kNullEntity, 0);
    run(*sim, 0.5);
    CHECK(countOfType(*sim, "wreck") == 1 && countOfType(*sim, "fire") == 1);
    CHECK(incidentKind(*sim, game::kIncidentFire) && incidentKind(*sim, game::kIncidentWreck));
    int fireOut = -1, towCalled = -1, towed = -1;
    for (int i = 0; i < 240 * int(sim->tickRate()); ++i) {
        sim->step();
        const game::TrafficIncident* wr = incidentKind(*sim, game::kIncidentWreck);
        if (fireOut < 0 && countOfType(*sim, "fire") == 0) fireOut = i;
        if (fireOut < 0) CHECK(wr && wr->state == 0);   // nobody tows a burning wreck
        if (towCalled < 0 && wr && wr->state > 0) towCalled = i;
        if (towed < 0 && !wr) { towed = i; break; }
    }
    CHECK(fireOut > 0 && towCalled > fireOut && towed > towCalled);
    // the wreck stands on the lot now
    CHECK(countOfType(*sim, "wreck") == 1);
    Vec3 at{};
    auto& types = mod(*sim).rules().types;
    sim->world().each<game::Unit, Transform>([&](Entity, game::Unit& u, Transform& t) { if (u.hp.raw > 0 && types[u.type].id == "wreck") at = t.pos; });
    CHECK((at - Vec3{Fixed::fromRatio(41, 2), Fixed{}, Fixed::fromRatio(37, 2)}).length() < Fixed::fromInt(5));
    CHECK(tstore(*sim).towed == 1);
}

TEST(traffic_city_police_come_when_somebody_saw_it_and_fight) {
    auto sim = makeSimJson(kTrafficCityRules, trafficMap(std::string(kTrafficCityStart) + R"(,{"type":"ped","owner":2,"pos":[10.5,4.6]})"), 2);
    auto& w = sim->world();
    run(*sim, 0.5);
    const Entity hero = mod(*sim).topDown()->heroOf(*sim, 1);
    // no witness within 6 cells: nobody calls
    CHECK(trf(*sim).report(*sim, game::kIncidentCrime, {Fixed::fromInt(22), Fixed::fromInt(22)}, kNullEntity, hero) == 0);
    const uint32_t id = trf(*sim).report(*sim, game::kIncidentCrime, {Fixed::fromRatio(17, 2), Fixed::fromRatio(9, 2)}, kNullEntity, hero);
    CHECK(id != 0 && trf(*sim).report(*sim, game::kIncidentCrime, {Fixed::fromInt(9), Fixed::fromRatio(9, 2)}, kNullEntity, hero) == id);   // the same call
    bool cops = false;
    const Fixed hp0 = w.get<game::Unit>(hero).hp;
    for (int i = 0; i < 120 * int(sim->tickRate()); ++i) {
        sim->step();
        if (countOfType(*sim, "cop") == 2) cops = true;
        if (cops && w.get<game::Unit>(hero).hp < hp0) break;
    }
    CHECK(cops);
    CHECK(w.get<game::Unit>(hero).hp < hp0);   // they opened fire on the offender
}

TEST(traffic_city_determinism_and_saves_with_services_on_call) {
    const std::string map = trafficMap(std::string(kTrafficCityStart) + R"(,{"type":"ped","owner":2,"pos":[9.5,4.5]},{"type":"ped","owner":2,"pos":[10.5,4.6]},
        {"type":"sedan","owner":0,"pos":[18.5,14.5],"facing":90})");
    auto play = [&](Simulation& sim, int from, int to, std::vector<uint64_t>* sums) {
        for (int i = from; i < to; ++i) {
            if (i == 10) mod(sim).dealDamage(sim, firstOf(sim, "ped"), Fixed::fromInt(1000), 0, mod(sim).topDown()->heroOf(sim, 1), 1);
            if (i == 30) mod(sim).dealDamage(sim, firstOf(sim, "sedan"), Fixed::fromInt(100000), 0, kNullEntity, 0);
            sim.step();
            if (sums && i % 50 == 0) sums->push_back(sim.checksum());
        }
    };
    auto s1 = makeSimJson(kTrafficCityRules, map, 6), s2 = makeSimJson(kTrafficCityRules, map, 6);
    std::vector<uint64_t> c1, c2;
    play(*s1, 0, 500, &c1);
    play(*s2, 0, 500, &c2);
    CHECK(c1 == c2);
    CHECK(tstore(*s1).incidents.size() >= 2);   // the injured (and the crime the other one saw), the fire, the wreck
    bool onCall = false;
    for (auto& c : tstore(*s1).cars) onCall = onCall || c.task != game::kTaskNone;
    CHECK(onCall);
    std::string save = s1->serialize();
    auto s3 = makeSimJson(kTrafficCityRules, map, 6);
    std::string err;
    CHECK(s3->deserialize(save, &err));
    CHECK(s3->checksum() == s1->checksum());
    play(*s1, 500, 1500, nullptr); play(*s3, 500, 1500, nullptr);
    CHECK(s3->checksum() == s1->checksum());
    SimSnapshot snap;
    s1->saveState(snap);
    run(*s1, 5);
    const uint64_t after = s1->checksum();
    s1->loadState(snap);
    run(*s1, 5);
    CHECK(s1->checksum() == after);
}

// ------------------------------------------------------------------ population: homes, jobs, shifts
namespace {
// a game day of 1200 s (an hour = 50 s) that starts at 8:00; twelve people in one house, a shop with one keeper
// (9-13) and a port with five jobs on a day shift (9-13) and five on a night shift (20-4): one is out of work
std::string populationRules() {
    std::string rules = kTrafficCityRules;
    const std::string at = R"("services": {)";
    rules.insert(rules.find(at), R"("population": {"count": 12, "dayLength": 1200, "startHour": 8, "walk": 0.5, "types": ["ped"],
        "kinds": {"house": {"lives": 12}, "shop": {"jobs": 1, "shifts": [[9, 13]]}, "port": {"jobs": 5, "shifts": [[9, 13], [20, 4]]}}},
    )");
    return rules;
}
const char* kPopulationPlaces = R"(,"places":[{"kind":"house","pos":[6.5,6.5]},{"kind":"shop","pos":[20.5,6.5]},{"kind":"port","pos":[20.5,20.5]}])";
std::string populationMap() {
    std::string map = trafficMap(R"({"type":"guy","owner":1,"pos":[8.5,4.5]},{"type":"dummy","owner":2,"pos":[8.5,8.5]})");
    map.insert(map.size() - 1, kPopulationPlaces);
    return map;
}
int citizensIn(Simulation& sim, int state, int shift = -1, int kind = -1) {
    int n = 0;
    for (auto& c : tstore(sim).citizens)
        if (c.state == state && (shift < 0 || (c.work >= 0 && c.shift == shift)) && (kind < 0 || (c.work >= 0 && trf(sim).places()[size_t(c.work)].kind == kind))) ++n;
    return n;
}
void runToHour(Simulation& sim, double hour) { while (trf(sim).minuteOfDay(sim) != int(hour * 60)) sim.step(); }
} // namespace

TEST(traffic_population_people_go_to_work_by_their_shifts_and_can_be_followed) {
    auto sim = makeSimJson(populationRules(), populationMap(), 3);
    auto& w = sim->world();
    run(*sim, 0.2);
    CHECK(trf(*sim).places().size() == 3 && tstore(*sim).citizens.size() == 12);
    int employed = 0, keepers = 0;
    for (auto& c : tstore(*sim).citizens) { employed += c.work >= 0; keepers += c.work == 1; CHECK(c.home == 0 && c.state == game::kCitizenHome); }
    CHECK(employed == 11 && keepers == 1);
    CHECK(trf(*sim).minuteOfDay(*sim) == 8 * 60 && trf(*sim).present(*sim, 1) == 0);   // 8:00, the shop is shut
    // the morning: follow the first one who comes out of the house as a real person
    int followed = -1, maxLive = 0;
    Vec3 lastSeen{};
    bool wentIn = false;
    while (trf(*sim).minuteOfDay(*sim) != 11 * 60) {
        sim->step();
        int live = 0;
        for (auto& p : tstore(*sim).people) {
            if (p.citizen < 0) continue;
            ++live;
            if (followed < 0) { followed = p.citizen; CHECK(trf(*sim).citizenOf(*sim, p.e) == &tstore(*sim).citizens[size_t(followed)]); }
            if (p.citizen == followed) lastSeen = w.get<Transform>(p.e).pos;
        }
        maxLive = std::max(maxLive, live);
        if (followed >= 0 && tstore(*sim).citizens[size_t(followed)].state == game::kCitizenWork) wentIn = true;
    }
    CHECK(maxLive >= 3 && followed >= 0 && wentIn);
    // he walked to the door of his own workplace and went in
    const auto& me = tstore(*sim).citizens[size_t(followed)];
    CHECK(!me.e.valid() && (lastSeen - Vec3{trf(*sim).places()[size_t(me.work)].pos.x, Fixed{}, trf(*sim).places()[size_t(me.work)].pos.y}).length() < Fixed::fromInt(7));
    // 11:00: the day shifts are in, the shop is open, the night shift and the one without a job are at home
    CHECK(citizensIn(*sim, game::kCitizenWork, 0) == 6 && trf(*sim).present(*sim, 1) == 1 && trf(*sim).present(*sim, 2) == 5);
    CHECK(citizensIn(*sim, game::kCitizenHome) == 6);
    // 15:30: everybody is home again (or nearly there), the shop is shut
    runToHour(*sim, 15.5);
    CHECK(trf(*sim).present(*sim, 1) == 0 && trf(*sim).present(*sim, 2) == 0 && citizensIn(*sim, game::kCitizenHome) >= 9);
    // 22:00: the night shift of the port is at work
    runToHour(*sim, 22);
    CHECK(citizensIn(*sim, game::kCitizenWork, 1, 2) == 5 && trf(*sim).present(*sim, 2) == 5 && trf(*sim).present(*sim, 1) == 0);
}

TEST(traffic_population_far_from_the_hero_runs_on_the_schedule_alone) {
    // people appear at most 3 cells from the hero: the whole commute happens unseen
    std::string rules = populationRules();
    const std::string from = R"("max": 0, "owner": 2, "spawnMin": 2, "spawnMax": 40, "despawn": 60})";
    rules.replace(rules.find(from), from.size(), R"("max": 0, "owner": 2, "spawnMin": 1, "spawnMax": 2, "despawn": 3})");
    auto sim = makeSimJson(rules, populationMap(), 3);
    run(*sim, 0.2);
    int units = 0;
    sim->world().each<game::Unit, Transform>([&](Entity, game::Unit&, Transform&) { ++units; });
    runToHour(*sim, 11);
    CHECK(tstore(*sim).people.empty());
    int now = 0;
    sim->world().each<game::Unit, Transform>([&](Entity, game::Unit&, Transform&) { ++now; });
    CHECK(now == units);   // nobody was ever spawned
    CHECK(citizensIn(*sim, game::kCitizenWork, 0) == 6 && trf(*sim).present(*sim, 1) == 1);
    runToHour(*sim, 15.5);
    CHECK(citizensIn(*sim, game::kCitizenHome) == 12);
}

TEST(traffic_population_determinism_and_saves) {
    auto s1 = makeSimJson(populationRules(), populationMap(), 5), s2 = makeSimJson(populationRules(), populationMap(), 5);
    std::vector<uint64_t> c1, c2;
    for (int i = 0; i < 1500; ++i) { s1->step(); s2->step(); if (i % 100 == 0) { c1.push_back(s1->checksum()); c2.push_back(s2->checksum()); } }
    CHECK(c1 == c2 && c1.front() != c1.back());
    bool live = false;
    for (auto& p : tstore(*s1).people) live = live || p.citizen >= 0;
    CHECK(live);   // saved in the middle of the morning walk
    std::string save = s1->serialize();
    auto s3 = makeSimJson(populationRules(), populationMap(), 5);
    std::string err;
    CHECK(s3->deserialize(save, &err));
    CHECK(s3->checksum() == s1->checksum());
    run(*s1, 60); run(*s3, 60);
    CHECK(s3->checksum() == s1->checksum());
    SimSnapshot snap;
    s1->saveState(snap);
    run(*s1, 10);
    const uint64_t after = s1->checksum();
    s1->loadState(snap);
    run(*s1, 10);
    CHECK(s1->checksum() == after);
}

// ------------------------------------------------------------------ getting to work: round the river, by car, by bus
namespace {
std::string replaced(std::string s, const std::string& from, const std::string& to) {
    const size_t at = s.find(from);
    if (at == std::string::npos) { std::printf("replaced: '%s' not found\n", from.c_str()); std::abort(); }
    return s.replace(at, from.size(), to);
}
// the shares of the citizens who drive / take the bus, and whatever else the population block needs
std::string commuteRules(const std::string& extra, const std::string& roads = R"("roads": {"tiles": "a"},)") {
    std::string r = replaced(populationRules(), R"("walk": 0.5, "types": ["ped"],)", R"("walk": 0.5, "types": ["ped"], )" + extra);
    r = replaced(r, R"("roads": {"tiles": "a"},)", roads);
    return replaced(r, R"("ped":   {"health": {"max": 40}, "move": {"speed": 0.5,)", R"("ped":   {"health": {"max": 40}, "move": {"speed": 1.5,)");
}
}

TEST(traffic_walk_route_goes_round_to_the_bridge) {
    // 30 x 9 sidewalk field cut by a river (column 15) with one bridge in the bottom row
    const int W = 30, H = 9;
    std::vector<uint8_t> walk(size_t(W * H), 1);
    for (int z = 0; z < H - 1; ++z) walk[size_t(z * W + 15)] = 0;
    tl::TrafficWalkSearch search;
    std::vector<int32_t> path, again;
    CHECK(tl::trafficWalkRoute(W, H, walk, 0 * W + 13, 0 * W + 17, search, path));
    CHECK(path.size() == 20 && path.back() == 17);   // 8 down, 4 across, 8 up
    bool overBridge = false;
    int prev = 13;
    for (int c : path) {
        CHECK(walk[size_t(c)] && std::abs(c % W - prev % W) + std::abs(c / W - prev / W) == 1);
        overBridge = overBridge || c == (H - 1) * W + 15;
        prev = c;
    }
    CHECK(overBridge);
    CHECK(tl::trafficWalkRoute(W, H, walk, 13, 17, search, again) && again == path);   // the work arrays are reused
    CHECK(!tl::trafficWalkRoute(W, H, walk, 13, 17, search, again, 10) && again.empty());   // gives up on a budget
    walk[size_t((H - 1) * W + 15)] = 0;   // no bridge
    CHECK(!tl::trafficWalkRoute(W, H, walk, 13, 17, search, again));
    CHECK(tl::trafficWalkRoute(W, H, walk, 13, 13, search, again) && again.empty());
}

TEST(traffic_population_walkers_go_round_what_cuts_their_sidewalk) {
    // no crossing between the house block and the shop block (the shop's corner cells are lawn): the only way is
    // round through the two southern blocks
    std::string map = trafficMap(R"({"type":"guy","owner":1,"pos":[14.5,13.5]},{"type":"dummy","owner":2,"pos":[8.5,8.5]})");
    auto rows = trafficRows();
    const std::string r4 = rows[4], r11 = rows[11];
    rows[4][16] = 'l'; rows[11][16] = 'l';
    map = replaced(map, "\"" + r4 + "\"", "\"" + rows[4] + "\"");
    map = replaced(map, "\"" + r11 + "\"", "\"" + rows[11] + "\"");
    map.insert(map.size() - 1, R"(,"places":[{"kind":"house","pos":[6.5,6.5]},{"kind":"shop","pos":[20.5,6.5]}])");
    std::string rules = commuteRules("");
    rules = replaced(rules, R"("house": {"lives": 12})", R"("house": {"lives": 1})");
    auto sim = makeSimJson(rules, map, 3);
    auto& w = sim->world();
    run(*sim, 0.2);
    CHECK(tstore(*sim).citizens.size() == 1 && tstore(*sim).citizens[0].work == 1);
    bool south = false, seenNearDoor = false, unseen = false;
    const Vec2 door = trf(*sim).places()[1].pos;
    while (trf(*sim).minuteOfDay(*sim) != 12 * 60 && tstore(*sim).citizens[0].state != game::kCitizenWork) {
        sim->step();
        const auto& c = tstore(*sim).citizens[0];
        if (c.state != game::kCitizenToWork) continue;
        if (!c.e.valid()) { unseen = unseen || sim->tick() - c.left > 3 * sim->tickRate(); continue; }
        const Vec3 p = w.get<Transform>(c.e).pos;
        south = south || p.z > Fixed::fromInt(16);
        seenNearDoor = seenNearDoor || (Vec2{p.x, p.z} - door).length() < Fixed::fromInt(5);
    }
    CHECK(tstore(*sim).citizens[0].state == game::kCitizenWork);
    CHECK(south && seenNearDoor && !unseen);   // on his feet all the way, through the southern blocks, to the door
}

TEST(traffic_population_drivers_park_at_the_kerb_drive_to_work_and_walk_in) {
    // everybody drives; one lane and a parking lane per side
    const std::string rules = commuteRules(R"("car": 100, "parked": 12, "drive": 2,)", R"("roads": {"tiles": "a", "parkingLanes": 1},)");
    auto sim = makeSimJson(rules, populationMap(), 3);
    auto& w = sim->world();
    run(*sim, 2.5);
    const int cars0 = countOfType(*sim, "sedan");
    int owned = 0;
    for (auto& c : tstore(*sim).citizens) {
        CHECK(c.commute == game::kCommuteCar);
        if (!c.car.valid()) continue;
        ++owned;
        CHECK(mod(*sim).driving()->car(*sim, c.car) && !trf(*sim).onRoad({w.get<Transform>(c.car).pos.x, w.get<Transform>(c.car).pos.z}) == false);
        CHECK(trf(*sim).kindOf(*sim, c.car) < 0);   // parked: not traffic
    }
    CHECK(owned >= 3 && owned == cars0 && tstore(*sim).cars.empty());   // their cars stand at the kerb by the house
    int drove = -1;
    bool parkedAtWork = false, walkedIn = false, lookedUp = false;
    Entity theCar = kNullEntity;
    while (trf(*sim).minuteOfDay(*sim) != 11 * 60) {
        sim->step();
        for (auto& tc : tstore(*sim).cars) {
            if (tc.citizen < 0) continue;   // could not get to the kerb: it dropped him off and drives on
            CHECK(tc.task == game::kTaskTrip);
            if (drove < 0) { drove = tc.citizen; theCar = tc.e; }
            if (tc.citizen == drove) lookedUp = lookedUp || trf(*sim).citizenOf(*sim, tc.e) == &tstore(*sim).citizens[size_t(drove)];
        }
        if (drove < 0) continue;
        const auto& me = tstore(*sim).citizens[size_t(drove)];
        if (me.car.valid() && me.state != game::kCitizenHome) {
            const Vec3 p = w.get<Transform>(me.car).pos;
            const Vec2 work = trf(*sim).places()[size_t(me.work)].pos;
            parkedAtWork = parkedAtWork || ((Vec2{p.x, p.z} - work).length() < Fixed::fromInt(9) && trf(*sim).kindOf(*sim, me.car) < 0);
        }
        if (parkedAtWork && me.state == game::kCitizenWork) walkedIn = true;
    }
    CHECK(drove >= 0 && theCar.valid() && lookedUp);
    CHECK(parkedAtWork && walkedIn);
    CHECK(citizensIn(*sim, game::kCitizenWork, 0) == 6);
    // the afternoon: they drive home again
    runToHour(*sim, 15.5);
    CHECK(citizensIn(*sim, game::kCitizenHome) >= 10);
    const auto& me = tstore(*sim).citizens[size_t(drove)];
    if (me.car.valid()) CHECK((w.get<Transform>(me.car).pos - Vec3{Fixed::fromRatio(13, 2), Fixed{}, Fixed::fromRatio(13, 2)}).length() < Fixed::fromInt(10));
}

TEST(traffic_population_a_stolen_car_is_reported_and_its_owner_goes_without) {
    const std::string rules = commuteRules(R"("car": 100, "parked": 12, "drive": 2,)", R"("roads": {"tiles": "a", "parkingLanes": 1},)");
    auto sim = makeSimJson(rules, populationMap(), 3);
    run(*sim, 2.5);
    // the hero takes the car of a citizen with a day job
    int victim = -1;
    for (size_t i = 0; i < tstore(*sim).citizens.size(); ++i) {
        const auto& c = tstore(*sim).citizens[i];
        if (c.car.valid() && c.work >= 0 && c.shift == 0 && victim < 0) victim = int(i);
    }
    CHECK(victim >= 0);
    if (victim < 0) return;
    Entity car = tstore(*sim).citizens[size_t(victim)].car;
    const Vec3 at = sim->world().get<Transform>(car).pos;
    const Entity hero = mod(*sim).topDown()->heroOf(*sim, 1);
    sim->world().get<Transform>(hero).pos = at + Vec3{Fixed{}, Fixed{}, Fixed::fromRatio(1, 2)};
    sim->step();
    Command enter; enter.player = 1; enter.type = game::kCmdTopDownEnter;
    sim->submit(enter);
    run(*sim, 2);
    CHECK(mod(*sim).driving()->carOf(*sim, hero) != nullptr);
    if (!mod(*sim).driving()->carOf(*sim, hero)) return;
    car = mod(*sim).driving()->carOf(*sim, hero)->e;   // whichever of the parked cars he got into
    victim = -1;
    for (size_t i = 0; i < tstore(*sim).citizens.size(); ++i) if (tstore(*sim).citizens[i].car == car) victim = int(i);
    CHECK(victim >= 0 && tstore(*sim).citizens[size_t(victim)].work >= 0);
    if (victim < 0) return;
    const bool day = tstore(*sim).citizens[size_t(victim)].shift == 0;
    bool reported = false;
    while (trf(*sim).minuteOfDay(*sim) != (day ? 11 : 22) * 60) {
        sim->step();
        for (auto& inc : tstore(*sim).incidents) reported = reported || (inc.kind == game::kIncidentCrime && inc.subject == car);
    }
    CHECK(reported);
    const auto& c = tstore(*sim).citizens[size_t(victim)];
    CHECK(!c.car.valid() && c.state == game::kCitizenWork);   // he got to work all the same
}

TEST(traffic_population_bus_riders_wait_at_the_stop_and_ride) {
    const std::string rules = replaced(commuteRules(R"("bus": 100, "ride": 2,)"), R"("cars": {"types": ["sedan"], "max": 0,)",
        R"("buses": {"types": ["sedan"], "max": 4, "lines": 2, "autoStops": 1, "dwell": [1, 1], "speed": [6, 7]}, "cars": {"types": ["sedan"], "max": 0,)");
    auto sim = makeSimJson(rules, populationMap(), 3);
    run(*sim, 0.2);
    CHECK(trf(*sim).lineCount() == 2 && trf(*sim).lineStops(0).size() >= 4);
    for (auto& c : tstore(*sim).citizens) CHECK(c.commute == game::kCommuteBus);
    int waited = 0, boarded = 0, alighted = 0;
    std::vector<uint8_t> leg(tstore(*sim).citizens.size(), game::kLegWalk);
    while (trf(*sim).minuteOfDay(*sim) != 12 * 60) {
        sim->step();
        for (size_t i = 0; i < leg.size(); ++i) {
            const auto& c = tstore(*sim).citizens[i];
            if (c.state != game::kCitizenToWork) { leg[i] = game::kLegWalk; continue; }
            if (c.leg == game::kLegAtStop && leg[i] != game::kLegAtStop) { ++waited; CHECK(c.e.valid() && c.stopA >= 0 && c.stopB >= 0 && c.stopA != c.stopB); }
            if (c.leg == game::kLegRide && leg[i] == game::kLegAtStop) {   // in a bus of his line
                ++boarded;
                const int kind = trf(*sim).kindOf(*sim, c.e);
                CHECK(c.e.valid() && kind == game::kTrafficBus && c.line >= 0);
                for (auto& bus : tstore(*sim).cars) if (bus.e == c.e) CHECK(bus.line == c.line);
            }
            if (c.leg == game::kLegWalk && leg[i] == game::kLegRide && c.e.valid()) {   // got off at his stop
                ++alighted;
                const Vec3 p = sim->world().get<Transform>(c.e).pos;
                CHECK(c.work >= 0 && (Vec2{p.x, p.z} - trf(*sim).places()[size_t(c.work)].pos).length() < Fixed::fromInt(9));
            }
            leg[i] = c.leg;
        }
    }
    CHECK(waited >= 3 && boarded >= 1 && alighted >= 1);
    CHECK(citizensIn(*sim, game::kCitizenWork, 0) >= 5);   // by bus or, when none came, on foot
}

TEST(traffic_population_commute_determinism_and_saves) {
    const std::string rules = replaced(commuteRules(R"("car": 40, "bus": 40, "parked": 8,)", R"("roads": {"tiles": "a", "parkingLanes": 1},)"),
        R"("cars": {"types": ["sedan"], "max": 0,)", R"("buses": {"types": ["sedan"], "max": 2, "autoStops": 1, "dwell": [1, 1]}, "cars": {"types": ["sedan"], "max": 3,)");
    auto s1 = makeSimJson(rules, populationMap(), 5), s2 = makeSimJson(rules, populationMap(), 5);
    std::vector<uint64_t> c1, c2;
    for (int i = 0; i < 1800; ++i) { s1->step(); s2->step(); if (i % 100 == 0) { c1.push_back(s1->checksum()); c2.push_back(s2->checksum()); } }
    CHECK(c1 == c2 && c1.front() != c1.back());
    int modes[3] = {0, 0, 0};
    for (auto& c : tstore(*s1).citizens) modes[c.commute]++;
    CHECK(modes[game::kCommuteCar] >= 1 && modes[game::kCommuteBus] >= 1 && modes[game::kCommuteWalk] >= 1);
    std::string save = s1->serialize();
    auto s3 = makeSimJson(rules, populationMap(), 5);
    std::string err;
    CHECK(s3->deserialize(save, &err));
    CHECK(s3->checksum() == s1->checksum());
    run(*s1, 120); run(*s3, 120);
    CHECK(s3->checksum() == s1->checksum());
    SimSnapshot snap;
    s1->saveState(snap);
    run(*s1, 10);
    const uint64_t after = s1->checksum();
    s1->loadState(snap);
    run(*s1, 10);
    CHECK(s1->checksum() == after);
}

TEST(traffic_people_jump_out_of_the_way_of_a_car) {
    // the hero drives down the road at a man who stands in his lane
    std::string rules = kTrafficRules;
    rules = replaced(rules, R"("max": 6)", R"("max": 0)");
    rules = replaced(rules, R"("max": 10, "owner": 2)", R"("max": 0, "owner": 2)");
    auto sim = makeSimJson(rules, trafficMap(R"({"type":"guy","owner":1,"pos":[1.5,1.0]},{"type":"dummy","owner":2,"pos":[8.5,8.5]},
        {"type":"sedan","owner":0,"pos":[1.5,1.5]},{"type":"ped","owner":2,"pos":[2.5,20.5]})"), 4);
    auto& w = sim->world();
    run(*sim, 0.1);
    const Entity car = firstOf(*sim, "sedan"), man = firstOf(*sim, "ped"), hero = mod(*sim).topDown()->heroOf(*sim, 1);
    Command c; c.player = 1; c.type = game::kCmdTopDownEnter; sim->submit(c);
    run(*sim, 0.5);
    CHECK(car.valid() && man.valid() && mod(*sim).driving()->isDriving(*sim, hero));
    if (!car.valid() || !man.valid() || !mod(*sim).driving()->isDriving(*sim, hero)) return;
    steer(*sim, 1, kNorth);   // stick up = throttle
    int guard = 0;
    while (mod(*sim).driving()->speed(*sim, car) < Fixed::fromInt(5) && guard++ < 200) sim->step();
    CHECK(guard < 200);
    const auto* dc = mod(*sim).driving()->car(*sim, car);
    Vec3 dir{dc->st.velocity.x, Fixed{}, dc->st.velocity.z};
    dir = dir / dir.length();
    w.get<Transform>(man).pos = w.get<Transform>(car).pos + dir * Fixed::fromRatio(7, 2);
    const Fixed hp = w.get<game::Unit>(man).hp;
    Fixed aside{};
    bool passed = false;
    for (int i = 0; i < 4 * int(sim->tickRate()) && w.alive(man); ++i) {
        sim->step();
        if (!w.alive(man)) break;
        const Vec3 rel = w.get<Transform>(man).pos - w.get<Transform>(car).pos;
        if (fxAbs(rel.x * dir.x + rel.z * dir.z) < Fixed::fromRatio(1, 2)) { passed = true; aside = fxMax(aside, fxAbs(rel.x * dir.z - rel.z * dir.x)); }
    }
    CHECK(w.alive(man) && w.get<game::Unit>(man).hp == hp);   // not touched
    CHECK(passed && aside > Fixed::fromRatio(3, 5));           // he was beside the car when it passed
}

TEST(traffic_population_parents_go_by_the_school) {
    // one working parent: the school is in the north-east block, the job in the south-east one
    std::string rules = commuteRules(R"("parents": 100,)");
    rules = replaced(rules, R"("house": {"lives": 12})", R"("house": {"lives": 1}, "school": {"children": 5})");
    rules = replaced(rules, R"("shifts": [[9, 13], [20, 4]])", R"("shifts": [[9, 13]])");
    std::string map = trafficMap(R"({"type":"guy","owner":1,"pos":[14.5,13.5]},{"type":"dummy","owner":2,"pos":[8.5,8.5]})");
    map.insert(map.size() - 1, R"(,"places":[{"kind":"house","pos":[6.5,6.5]},{"kind":"school","pos":[20.5,6.5]},{"kind":"port","pos":[20.5,20.5]}])");
    auto sim = makeSimJson(rules, map, 3);
    auto& w = sim->world();
    run(*sim, 0.2);
    CHECK(tstore(*sim).citizens.size() == 1);
    const auto& c = tstore(*sim).citizens[0];
    CHECK(c.work == 2 && c.school == 1);
    const Vec2 school = trf(*sim).places()[1].pos, work = trf(*sim).places()[2].pos;
    Fixed nearSchool = Fixed::fromInt(100);
    bool wasVia = false, atSchoolFirst = false;
    while (trf(*sim).minuteOfDay(*sim) != 12 * 60 && c.state != game::kCitizenWork) {
        sim->step();
        if (c.state != game::kCitizenToWork) continue;
        wasVia = wasVia || c.via;
        if (!c.e.valid()) continue;
        const Vec3 p = w.get<Transform>(c.e).pos;
        nearSchool = fxMin(nearSchool, (Vec2{p.x, p.z} - school).length());
        if (!c.via && !atSchoolFirst) { atSchoolFirst = true; CHECK((Vec2{p.x, p.z} - school).length() < Fixed::fromInt(5) && (Vec2{p.x, p.z} - work).length() > Fixed::fromInt(8)); }
    }
    CHECK(wasVia && atSchoolFirst && nearSchool < Fixed::fromInt(5));
    CHECK(c.state == game::kCitizenWork && !c.via);
    // the way home goes by the school again
    bool viaHome = false;
    while (trf(*sim).minuteOfDay(*sim) != 16 * 60) { sim->step(); viaHome = viaHome || (c.state == game::kCitizenToHome && c.via); }
    CHECK(viaHome && c.state == game::kCitizenHome);
}

// ------------------------------------------------------------------ gangs
TEST(traffic_gang_takes_a_car_robs_the_shop_and_gets_away) {
    // five citizens without work, a shop to rob, no police in this town
    std::string r = commuteRules("");
    r = replaced(r, R"("shifts": [[9, 13], [20, 4]])", R"("shifts": [[9, 13]])");
    r = replaced(r, R"("port": {"jobs": 5,)", R"("port": {"jobs": 3,)");
    r = replaced(r, R"("roads": {"tiles": "a"},)", R"("roads": {"tiles": "a", "parkingLanes": 1}, "gangs": {"every": 3, "size": [2, 3], "targets": {"shop": 500}, "rob": 4, "jail": 60},)");
    r = replaced(r, R"("police": {)", R"("police": {"max": 0, )");
    auto sim = makeSimJson(r, populationMap(), 3);
    auto& w = sim->world();
    const Vec2 shop = trf(*sim).places()[1].pos;
    bool drove = false, out = false, robbed = false, fled = false, reported = false, robbersAtDoor = false;
    size_t members = 0;
    for (int i = 0; i < 150 * int(sim->tickRate()) && tstore(*sim).gangRobberies == 0; ++i) {
        sim->step();
        const auto& g = tstore(*sim).gang;
        if (g.state == game::kGangNone) continue;
        members = std::max(members, g.members.size());
        for (int32_t id : g.members) CHECK(tstore(*sim).citizens[size_t(id)].state == game::kCitizenGang && tstore(*sim).citizens[size_t(id)].work < 0);
        drove = drove || (g.state == game::kGangDrive && trf(*sim).kindOf(*sim, g.car) >= 0);
        out = out || g.state == game::kGangOut;
        if (g.state == game::kGangRob) {
            robbed = true;
            for (auto& inc : tstore(*sim).incidents) reported = reported || (inc.kind == game::kIncidentCrime && inc.id == g.call);
            for (Entity m : g.crew) robbersAtDoor = robbersAtDoor || (Vec2{w.get<Transform>(m).pos.x, w.get<Transform>(m).pos.z} - shop).length() < Fixed::fromInt(5);
        }
        fled = fled || (g.state == game::kGangFlee && g.crew.empty());
    }
    CHECK(members >= 2 && drove && out && robbed && reported && robbersAtDoor && fled);
    CHECK(tstore(*sim).gangRobberies == 1 && tstore(*sim).gangLoot == 500 && tstore(*sim).gangCaught == 0);
    CHECK(tstore(*sim).gang.state == game::kGangNone);
    for (auto& c : tstore(*sim).citizens) CHECK(c.state != game::kCitizenGang);   // home again
    bool crime = false;
    for (auto& inc : tstore(*sim).incidents) crime = crime || inc.kind == game::kIncidentCrime;
    CHECK(!crime);   // the call is closed
}

TEST(traffic_gang_meets_the_police) {
    std::string r = commuteRules("");
    r = replaced(r, R"("shifts": [[9, 13], [20, 4]])", R"("shifts": [[9, 13]])");
    r = replaced(r, R"("port": {"jobs": 5,)", R"("port": {"jobs": 3,)");
    r = replaced(r, R"("roads": {"tiles": "a"},)", R"("roads": {"tiles": "a", "parkingLanes": 1}, "gangs": {"every": 3, "size": [2, 3], "targets": {"shop": 500}, "rob": 12, "jail": 60, "owner": 3},)");
    auto s1 = makeSimJson(r, populationMap(), 3), s2 = makeSimJson(r, populationMap(), 3);
    bool policeCame = false;
    for (int i = 0; i < 200 * int(s1->tickRate()) && tstore(*s1).gangRobberies + tstore(*s1).gangCaught == 0 && !(tstore(*s1).gang.state == game::kGangNone && i > 60 * 20); ++i) {
        s1->step(); s2->step();
        if (i % 100 == 0) CHECK(s1->checksum() == s2->checksum());
        for (auto& c : tstore(*s1).cars) policeCame = policeCame || (c.service >= 0 && c.call == tstore(*s1).gang.call && c.call != 0);
    }
    CHECK(policeCame);
    // however it ended (arrest, shot at the door, got away): nobody is left "out with the gang", and jail lets them out
    CHECK(tstore(*s1).gang.state == game::kGangNone);
    int jailed = 0;
    for (auto& c : tstore(*s1).citizens) { CHECK(c.state != game::kCitizenGang); jailed += c.state == game::kCitizenJail; }
    CHECK((tstore(*s1).gangCaught > 0) == (jailed > 0));
    std::string save = s1->serialize(), err;
    auto s3 = makeSimJson(r, populationMap(), 3);
    CHECK(s3->deserialize(save, &err) && s3->checksum() == s1->checksum());
    std::vector<uint32_t> until;
    for (auto& c : tstore(*s1).citizens) until.push_back(c.state == game::kCitizenJail ? c.eta : 0);
    run(*s1, 70);   // (other gangs form meanwhile: only this sentence is looked at)
    for (size_t i = 0; i < until.size(); ++i) if (until[i]) CHECK(tstore(*s1).citizens[i].state != game::kCitizenJail || tstore(*s1).citizens[i].eta != until[i]);
}

TEST(traffic_gang_leaves_a_robber_somebody_holds) {
    std::string r = commuteRules("");
    r = replaced(r, R"("shifts": [[9, 13], [20, 4]])", R"("shifts": [[9, 13]])");
    r = replaced(r, R"("port": {"jobs": 5,)", R"("port": {"jobs": 3,)");
    r = replaced(r, R"("roads": {"tiles": "a"},)", R"("roads": {"tiles": "a", "parkingLanes": 1}, "gangs": {"every": [500, 500], "size": [3, 3], "targets": {"shop": 500}, "rob": 4, "jail": 60},)");
    r = replaced(r, R"("police": {)", R"("police": {"max": 0, )");
    auto sim = makeSimJson(r, populationMap(), 3);
    tstore(*sim).nextGang = 60;   // in three seconds
    Entity heldOne = kNullEntity;
    uint32_t call = 0;
    for (int i = 0; i < 150 * int(sim->tickRate()) && tstore(*sim).gangRobberies == 0 && !(heldOne.valid() && tstore(*sim).gang.state == game::kGangNone); ++i) {
        sim->step();
        auto& g = tstore(*sim).gang;
        if (g.state == game::kGangRob && !heldOne.valid() && !g.crew.empty()) {   // an arrest by the game: the first of them is taken
            heldOne = g.crew[0];
            call = g.call;
            sim->world().get<game::Unit>(heldOne).flags |= game::kUnitInCargo;   // (in the patrol car)
        }
    }
    CHECK(heldOne.valid() && sim->world().alive(heldOne));   // not taken into the car, not removed
    CHECK(tstore(*sim).gangRobberies == 1 && tstore(*sim).gangCaught == 1);   // the others got away with it
    int jailed = 0, home = 0;
    for (auto& c : tstore(*sim).citizens) { jailed += c.state == game::kCitizenJail; home += c.state == game::kCitizenHome && c.work < 0; CHECK(c.state != game::kCitizenGang); }
    CHECK(jailed == 1 && home >= 2);
    CHECK(call != 0);
}
