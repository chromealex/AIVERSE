// ------------------------------------------------------------------ Bunker Wars routes, groups, multiscreen
// (modules/rts/RtsRoutes*.cpp, docs/game-format.md § Routes)

static bool nearFx(Fixed a, double b, double eps = 1e-3) { return std::fabs(a.toDouble() - b) <= eps; }

TEST(routes_spline_matches_bw) {
    game::RouteRules rr;   // BW defaults: 6 lines, column padding 0.5, trim 0.25, 10 nodes between points
    game::RouteDef st;
    st.from = "a"; st.to = "b";
    st.points = {{Fixed{}, Fixed{}}, {Fixed::fromInt(10), Fixed{}}};
    st.offsets.assign(2, {}); st.zones.assign(2, 0); st.slowdown.assign(2, Fixed{});
    auto lines = game::buildRouteLines(st, rr);
    CHECK(lines.size() == 6);
    // 12 Catmull-Rom nodes 10/11 apart; BW trimming drops the first and the last node (the moved node is removed again)
    // Catmull-Rom with doubled end points eases in / out; BW trimming drops the nodes within 0.2 of the ends (and the
    // moved end node again), so the line starts beyond 0.25 and ends before 9.75 on the straight
    bool straight = lines[0].size() >= 8 && lines[0].size() <= 12 && lines[0].front().x >= Fixed::fromRatio(1, 4) && lines[0].back().x <= Fixed::fromRatio(39, 4);
    for (size_t i = 1; i < lines[0].size(); ++i) straight = straight && lines[0][i].x > lines[0][i - 1].x && lines[0][i].y.raw == 0;
    CHECK(straight && lines[1] == lines[0]);                             // no inner point: every line is the same
    // a bend: the inner control point gets the column offset of each line (even left, odd right, 0.5 / 1.5 / 2.5 columns)
    game::RouteDef bend = st;
    bend.points = {{Fixed{}, Fixed{}}, {Fixed::fromInt(5), Fixed{}}, {Fixed::fromInt(10), Fixed{}}};
    bend.offsets.assign(3, {}); bend.zones.assign(3, 0); bend.slowdown.assign(3, Fixed{});
    bend.offsets[1][3] = Fixed::fromRatio(1, 10);   // per-line extra offset (BW Spline.Offset)
    lines = game::buildRouteLines(bend, rr);
    const double want[6] = {-0.25, 0.25, -0.75, 0.85, -1.25, 1.25};   // columnPadding 0.5 (BW 20 / 40)
    auto atMid = [](const std::vector<Vec2>& l) { size_t b = 0; for (size_t i = 0; i < l.size(); ++i) if (fxAbs(l[i].x - Fixed::fromInt(5)) < fxAbs(l[b].x - Fixed::fromInt(5))) b = i; return l[b]; };
    for (int k = 0; k < 6; ++k) CHECK(nearFx(atMid(lines[size_t(k)]).x, 5.0) && nearFx(atMid(lines[size_t(k)]).y, want[k]));
    // raw nodes win over the control points (BW rawSplineLines)
    bend.lines = {{{Fixed{}, Fixed::fromInt(3)}, {Fixed::fromInt(10), Fixed::fromInt(3)}}};
    lines = game::buildRouteLines(bend, rr);
    CHECK(nearFx(lines[0][3].y, 3.0) && nearFx(atMid(lines[1]).y, 0.25));
    // AI length: slowdown segments count longer
    bend.zones = {2, 2, 0}; bend.slowdown = {Fixed::fromInt(2), Fixed::fromInt(3), Fixed{}};
    CHECK(nearFx(game::routeAiLength(bend), 20.0));
    CHECK(game::screenGroupsMatch({}, {}) && !game::screenGroupsMatch({1}, {}) && game::screenGroupsMatch({1, 2}, {2}) && !game::screenGroupsMatch({1}, {2}));
    // JSON round trip
    const game::RouteDef back = game::parseRoute(game::routeToJson(bend), 0);
    CHECK(back.points.size() == 3 && back.offsets[1][3] == bend.offsets[1][3] && back.lines.size() == 1 && back.zones[1] == 2);
}

static const char* kRouteRules = R"({
  "lanes": {"routes": {"lines": 6}%GROUPS%},
  "types": {
    "village": {"structure": {"footprint": [2, 2]}, "invulnerable": true, "garrison": {"max": 60, "unit": "shroom", "interval": 1000}},
    "shroom": {"move": {"speed": 3}, "entersGarrison": true},
    "rock": {"health": {"max": 10}}
  }})";
static std::string routeRules(bool groups) {
    std::string r = kRouteRules;
    r.replace(r.find("%GROUPS%"), 8, groups ? R"(, "groups": {"rowSpacing": 3})" : "");
    return r;
}
static std::string routeMap(const std::string& extra = "") {
    std::string rows;
    for (int y = 0; y < 16; ++y) rows += std::string(y ? "," : "") + "\"" + std::string(40, '.') + "\"";
    return R"({"size":[40,16],"players":[{"name":"A","team":1},{"name":"B","team":2}],"terrain":{"rows":[)" + rows + R"(]},
      "entities":[{"id":"a","type":"village","owner":1,"pos":[5,8],"garrison":20},{"id":"b","type":"village","owner":0,"pos":[20,8],"garrison":0},
                  {"id":"c","type":"village","owner":2,"pos":[35,8],"garrison":5},{"id":"r","type":"rock","owner":0,"pos":[28,3]},
                  {"id":"d","type":"village","owner":1,"pos":[12,14],"garrison":1}],
      "routes":[{"from":"a","to":"b","points":[[5,8],[12,6],[20,8]]},{"from":"b","to":"c","points":[[20,8],[35,8]],"blockers":["r"]}])" + extra + "}";
}
static Entity villageAt(Simulation& sim, int x) {
    Entity out;
    sim.world().each<game::Garrison, Transform>([&](Entity e, game::Garrison&, Transform& t) { if (t.pos.x.floorInt() == x || t.pos.x.floorInt() == x - 1) out = e; });
    return out;
}
static void sendCmd(Simulation& sim, uint8_t p, Entity from, Entity to, int pct) {
    Command c; c.player = p; c.type = game::kCmdSend; c.units = {from}; c.target = to; c.param = pct;
    sim.submit(c);
}

TEST(routes_replace_lanes_and_gate_sends) {
    auto sim = makeSimJson(routeRules(false), routeMap());
    auto& m = mod(*sim);
    auto& gs = m.state(*sim);
    World& w = sim->world();
    CHECK(m.routesAuthored() && gs.lanes.size() == 12);                  // every route keeps its lines (stable lane indices)
    Entity a = villageAt(*sim, 5), b = villageAt(*sim, 20), c = villageAt(*sim, 35);
    CHECK(m.canSend(*sim, a, b) && m.canSend(*sim, b, a) && !m.canSend(*sim, a, c) && !m.canSend(*sim, b, c));
    CHECK(m.routeLength(*sim, a, b).raw > 0 && m.routeLength(*sim, a, c).raw < 0);
    sendCmd(*sim, 1, a, c, 100);                                          // no route: ignored
    run(*sim, 0.5);
    CHECK(w.get<game::Garrison>(a).count == 20);
    sendCmd(*sim, 1, a, b, 50);
    run(*sim, 1.5);
    int walkers = 0;
    w.each<game::LaneWalker>([&](Entity, game::LaneWalker& lw) { ++walkers; CHECK(gs.routes.laneRoute[size_t(lw.lane)] == 0); });
    CHECK(walkers > 4);
    run(*sim, 15);
    CHECK(w.get<Owner>(b).player == 1);                                   // arrived and captured
    // the rock dies: the route b-c opens
    Entity rock;
    w.each<game::Unit>([&](Entity e, game::Unit& u) { if (m.rules().types[u.type].id == "rock") rock = e; });
    w.get<game::Unit>(rock).hp = Fixed{};
    run(*sim, 2);
    CHECK(gs.lanes.size() == 12 && m.canSend(*sim, b, c));               // b-c opened (BW Route.RemoveLocker)
}

TEST(routes_rows_stay_abreast) {
    auto sim = makeSimJson(routeRules(true), routeMap());
    auto& m = mod(*sim);
    World& w = sim->world();
    Entity a = villageAt(*sim, 5), b = villageAt(*sim, 20);
    sendCmd(*sim, 1, a, b, 30);                                           // one row of 6
    run(*sim, 2);
    std::vector<Entity> row;
    w.each<game::LaneWalker>([&](Entity e, game::LaneWalker&) { row.push_back(e); });
    CHECK(row.size() == 6);
    // every line is walked at line 0's progress (BW): the row crosses x = 12 (the bend) at about the same time
    Fixed lo = Fixed::fromInt(100), hi{};
    for (Entity e : row) { lo = fxMin(lo, w.get<Transform>(e).pos.x); hi = fxMax(hi, w.get<Transform>(e).pos.x); }
    CHECK(hi - lo < Fixed::fromRatio(1, 2));
    (void)m;
}

TEST(routes_bw_groups_rows_merge_close) {
    auto sim = makeSimJson(routeRules(true), routeMap());
    auto& m = mod(*sim);
    auto& gs = m.state(*sim);
    World& w = sim->world();
    Entity a = villageAt(*sim, 5), b = villageAt(*sim, 20);
    auto walkers = [&] { int n = 0; w.each<game::LaneWalker>([&](Entity, game::LaneWalker&) { ++n; }); return n; };
    sendCmd(*sim, 1, a, b, 50);                                           // need 10
    for (int i = 0; i < 20 && gs.routes.groups.empty(); ++i) sim->step();
    CHECK(gs.routes.groups.size() == 1 && walkers() == 6);                // the first row leaves at once
    CHECK(w.get<game::Garrison>(a).count == 14);                          // paid per row, not at the send
    sendCmd(*sim, 1, a, b, 50);                                           // merges: need = min(10 + 7 - 6, 14) + 6 = 17
    for (int i = 0; i < 8 && gs.routes.groups[0].need != 17; ++i) sim->step();
    CHECK(gs.routes.groups.size() == 1 && gs.routes.groups[0].need == 17);
    run(*sim, 0.3);
    CHECK(walkers() == 6);                                                // the next row waits for 3 cells of walking
    run(*sim, 0.6);
    CHECK(walkers() == 12 && w.get<game::Garrison>(a).count == 8);
    // the source changes hands: the group closes, the rest never leave
    w.get<Owner>(a).player = 2;
    run(*sim, 2);
    CHECK(gs.routes.groups.empty() && w.get<game::Garrison>(a).count == 8);
    // save / load in the middle of a group: identical continuation
    w.get<Owner>(a).player = 1;
    sendCmd(*sim, 1, a, b, 100);
    for (int i = 0; i < 20 && gs.routes.groups.empty(); ++i) sim->step();
    run(*sim, 0.2);
    CHECK(gs.routes.groups.size() == 1);
    const std::string bytes = sim->serialize();
    auto copy = makeSimJson(routeRules(true), routeMap());
    std::string err;
    CHECK(!bytes.empty() && copy->deserialize(bytes, &err));
    bool same = true;
    for (int i = 0; i < 100; ++i) { sim->step(); copy->step(); if (sim->checksum() != copy->checksum()) same = false; }
    CHECK(same && sim->serialize() == copy->serialize());
}

TEST(routes_screen_groups_and_multiscreen) {
    // c is in screen group 2, b in 1: no route b-c even when it is authored
    std::string map = routeMap(R"(,"screen":{"size":[24,16],"center":[12,8],"bounds":[0,16,0,0]})");
    auto sub = [&](const std::string& from, const std::string& to) { map.replace(map.find(from), from.size(), to); };
    sub(R"("garrison":20})", R"("garrison":20,"screenGroups":[1]})");
    sub(R"("garrison":0})", R"("garrison":0,"screenGroups":[1]})");
    sub(R"("garrison":5})", R"("garrison":5,"screenGroups":[2]})");
    std::string rules = routeRules(false);
    rules.replace(rules.find(R"("interval": 1000)"), 16, R"("interval": 0.5)");
    auto sim = makeSimJson(rules, map);
    auto& m = mod(*sim);
    World& w = sim->world();
    CHECK(m.routeDefs().size() == 1);                                     // b-c dropped (BW CompareScreenGroups)
    Entity a = villageAt(*sim, 5), c = villageAt(*sim, 35);
    CHECK(m.screen().enabled && m.onScreen(*sim, w.get<Transform>(a).pos) && !m.onScreen(*sim, w.get<Transform>(c).pos));
    run(*sim, 3);
    CHECK(w.get<game::Garrison>(a).count > 20 && w.get<game::Garrison>(c).count == 5);   // off the screen: no growth
    m.setScreenCenter(*sim, {Fixed::fromInt(40), Fixed{}, Fixed::fromInt(2)});           // clamped to the bounds
    CHECK(m.screenCenter(*sim).x == Fixed::fromInt(28) && m.screenCenter(*sim).z == Fixed::fromInt(8));
    run(*sim, 3);
    CHECK(w.get<game::Garrison>(c).count > 5);
}

TEST(routes_flying_tower_ufo) {
    // BW PositionFlyingTower (Hero14 UFO: 10 s, range 600 px x aspect 0.5, 3.5 shots / s): a temporary tower at a point
    const char* rules = R"({
      "resources": {"mana": {"start": 100, "regen": 1}},
      "playerAbilities": [{"id": "ufo", "target": "point", "cost": {}, "effects": [{"type": "spawn", "unit": "ufo", "duration": 10}]}],
      "types": {
        "ufo": {"health": {"max": 1}, "invulnerable": true, "sight": 12,
                "weapons": [{"range": 12, "rangeAspect": 0.5, "damage": 100, "cooldown": 0.2857, "projectileSpeed": 6}]},
        "dummy": {"health": {"max": 10}},
        "hq": {"structure": {"footprint": [1, 1]}, "health": {"max": 100}}
      }})";
    auto sim = makeSimJson(rules, flatMap(40, 30, R"(,"fogOfWar":{"mode":"none"},"entities":[{"type":"dummy","owner":2,"pos":[28,15]},{"type":"dummy","owner":2,"pos":[20,23]},{"type":"hq","owner":1,"pos":[2,2]},{"type":"hq","owner":2,"pos":[37,2]}])"));
    World& w = sim->world();
    auto& m = mod(*sim);
    Entity onX, onZ;
    w.each<game::Unit, Transform>([&](Entity e, game::Unit&, Transform& t) { if (t.pos.x.floorInt() == 28) onX = e; else if (t.pos.x.floorInt() == 20) onZ = e; });
    Command c; c.player = 1; c.type = game::kCmdPlayerAbility; c.param = m.rules().findPlayerAbility("ufo");
    c.point = {Fixed::fromInt(20), Fixed{}, Fixed::fromInt(15)};
    sim->submit(c);
    run(*sim, 4);
    CHECK(!w.alive(onX) || w.get<game::Unit>(onX).hp.raw <= 0);           // 8 cells along x: inside the ellipse
    CHECK(w.alive(onZ) && w.get<game::Unit>(onZ).hp.raw > 0);             // 8 cells along z: outside (z radius 6)
    int ufos = 0;
    w.each<game::Unit>([&](Entity, game::Unit& u) { if (m.rules().types[u.type].id == "ufo" && u.hp.raw > 0) ++ufos; });
    CHECK(ufos == 1);
    run(*sim, 7);
    ufos = 0;
    w.each<game::Unit>([&](Entity, game::Unit& u) { if (m.rules().types[u.type].id == "ufo" && u.hp.raw > 0) ++ufos; });
    CHECK(ufos == 0);                                                     // gone after its 10 s
}

TEST(routes_flying_send_offsets_fade_coverage) {
    // a flying garrison (BW PointsFlyingUnits) sends along a straight flying route without a ground route, rows 3 apart,
    // the whole garrison; walkers carry a random diagonal offset faded at the route ends; towersCoverage feeds the AI helpers
    std::string rules = routeRules(true);
    rules.replace(rules.find(R"("types": {)"), 10, R"("statuses": {"fly": {"flags": ["flying"]}}, "types": {)");
    std::string map = routeMap();
    map.replace(map.find(R"("blockers":["r"]})"), 17, R"("blockers":["r"],"towersCoverage":[{"point":"d","distances":[1,2,4]}]})");
    auto sim = makeSimJson(rules, map);
    auto& m = mod(*sim);
    auto& gs = m.state(*sim);
    World& w = sim->world();
    Entity a = villageAt(*sim, 5), b = villageAt(*sim, 20), c = villageAt(*sim, 35), d = villageAt(*sim, 12);
    CHECK(!m.canSend(*sim, a, c));
    game::EffectCtx ctx; ctx.owner = 1;
    m.addStatus(*sim, a, uint16_t(m.rules().findStatus("fly")), Fixed::fromInt(30), ctx);
    CHECK(m.sendsFlying(*sim, a) && m.canSend(*sim, a, c));
    sendCmd(*sim, 1, a, c, 10);
    for (int i = 0; i < 20 && gs.routes.groups.empty(); ++i) sim->step();
    CHECK(gs.routes.groups.size() == 1 && gs.routes.groups[0].flying && gs.routes.groups[0].need == 20);
    CHECK(gs.lanes.size() == 18 && gs.routes.laneFlying[12] == 1);       // 6 flying lines made on first use
    CHECK(!m.sendsFlying(*sim, a));                                      // one flying group at a time
    run(*sim, 1);
    bool straight = true, jittered = false;
    w.each<game::LaneWalker, Transform>([&](Entity e, game::LaneWalker& lw, Transform& t) {
        CHECK(gs.routes.laneFlying[size_t(lw.lane)] == 1);
        auto it = gs.routes.walks.find(e.packed());
        if (it == gs.routes.walks.end()) { straight = false; return; }
        if (it->second.jitter.raw != 0 && it->second.alpha == Fixed::fromInt(1)) jittered = jittered || t.pos.x - it->second.base.x == it->second.jitter;
    });
    CHECK(straight && jittered);
    CHECK(m.routeAlpha(*sim, kNullEntity) == Fixed::fromInt(1));
    // coverage (BW GetTowerCoverageDistance): [0 ... 0, 1, 2, 4] over ranges 0..20 steps of 1.25
    CHECK(m.towerCoverage(*sim, b, c, d, Fixed::fromInt(25)) == Fixed::fromInt(4));
    CHECK(m.towerCoverage(*sim, c, b, d, Fixed::fromRatio(5, 4) * 19 + Fixed::fromRatio(5, 8)) == Fixed::fromInt(3));   // halfway 2 -> 4
    CHECK(m.towerCoverage(*sim, b, c, d, Fixed::fromInt(1)).raw == 0 && m.towerCoverage(*sim, a, b, d, Fixed::fromInt(25)).raw == 0);
    CHECK(m.towerUnitsDamage(*sim, b, c, d, Fixed::fromInt(25), Fixed::fromInt(2), 17, Fixed::fromInt(2), Fixed::fromInt(2)) == 8);   // (4 + 2*2) / 2 s * 2 / s
    CHECK(m.routeAiLength(*sim, a, b).raw > 0 && m.routeAiLength(*sim, a, c).raw < 0);
    // save / load with flying lanes and walker offsets
    const std::string bytes = sim->serialize();
    auto copy = makeSimJson(rules, map);
    std::string err;
    CHECK(!bytes.empty() && copy->deserialize(bytes, &err));
    bool same = true;
    for (int i = 0; i < 60; ++i) { sim->step(); copy->step(); if (sim->checksum() != copy->checksum()) same = false; }
    CHECK(same);
}

#ifdef AIVERSE_TESTS_EDITOR_CORE
#include "apps/editor/core/RouteOps.h"
TEST(routes_editor_ops) {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / ("aiverse_routes_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::remove_all(root);
    fs::create_directories(root / "games");
    fs::copy(fs::path(AIVERSE_SOURCE_DIR) / "templates" / "sporelanes", root / "games" / "sporelanes", fs::copy_options::recursive);
    editor::Workspace ws;
    ws.engineDir = AIVERSE_SOURCE_DIR;
    std::string err;
    CHECK(ws.openGame((root / "games" / "sporelanes").string(), &err));
    auto ok = [](const Json& r) { if (r.has("error")) std::printf("  (op error) %s\n", r.getStr("error").c_str()); return !r.has("error"); };
    std::vector<int> g;
    for (size_t i = 0; i < ws.map.data.entities.size(); ++i) {
        const int t = ws.project.rules.findType(ws.map.data.entities[i].type);
        if (t >= 0 && ws.project.rules.types[size_t(t)].garrisonMax > 0) g.push_back(int(i));
    }
    CHECK(g.size() >= 3);
    Json r = ws.call("route_generate", Json::object());
    CHECK(ok(r) && r["added"].integer() > 0);
    const int64_t made = r["routes"].integer();
    Json l = ws.call("route_list", Json::object());
    CHECK(ok(l) && int64_t(l["routes"].size()) == made && l["routes"][size_t(0)]["length"].toDouble() > 0 && l["routes"][size_t(0)]["points"].size() == 4);
    // a hand-made route replaces the generated one between the same pair (either direction)
    Json set = Json::object();
    set.set("from", Json::number(int64_t(g[1]))); set.set("to", Json::number(int64_t(g[0])));
    const auto& e0 = ws.map.data.entities[size_t(g[0])]; const auto& e1 = ws.map.data.entities[size_t(g[1])];
    Json pts = Json::array();
    auto pt = [&](double x, double z) { Json p = Json::array(); p.push(Json::number(x)); p.push(Json::number(z)); return p; };
    pts.push(pt(e1.x, e1.z)); pts.push(pt((e0.x + e1.x) / 2 + 1, (e0.z + e1.z) / 2 + 1)); pts.push(pt(e0.x, e0.z));
    set.set("points", pts);
    r = ws.call("route_set", set);
    CHECK(ok(r) && r["points"].size() == 3 && !ws.map.data.entities[size_t(g[0])].id.empty());
    l = ws.call("route_list", Json::object());
    CHECK(int64_t(l["routes"].size()) == made + (r.getStr("summary").find("заменён") != std::string::npos ? 0 : 1));
    CHECK(!ok(ws.call("route_set", Json::parse(R"({"from": 0, "to": 0})"))));
    CHECK(!editor::routePreviewLines(ws.project.rules, ws.map).empty());
    CHECK(ok(ws.call("screen_set", Json::parse(R"({"size": [20, 14], "bounds": [0, 10, 0, 0]})"))) && ws.map.data.extra["screen"]["size"].size() == 2);
    CHECK(!ok(ws.call("screen_set", Json::parse(R"({"size": [0, 14]})"))));
    // the edited map plays: routes authored, sends gated
    auto sim = makeSimJson(ws.project.rulesJson.dump(), ws.map.toJson().dump());
    CHECK(mod(*sim).routesAuthored() && mod(*sim).screen().enabled);
    CHECK(ok(ws.call("route_delete", Json::parse(R"({"all": true})"))) && !ws.map.data.extra.has("routes"));
    fs::remove_all(root);
}
#endif
