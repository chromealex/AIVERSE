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
    int red = 0, green = 0;
    for (auto& l : tstore(*sim).lights) {
        const std::string id = mod(*sim).rules().types[w.get<game::Unit>(l.e).type].id;
        red += id == "lamp_r"; green += id == "lamp_g";
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
