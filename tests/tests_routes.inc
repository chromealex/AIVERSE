// ------------------------------------------------------------------ Bunker Wars routes, groups, multiscreen
// (modules/rts/RtsRoutes*.cpp, docs/game-format.md § Routes)

static bool nearFx(Fixed a, double b, double eps = 1e-3) { return std::fabs(a.toDouble() - b) <= eps; }

TEST(routes_spline_matches_bw) {
    game::RouteRules rr;   // BW defaults: 6 lines, column padding 0.4, trim 0.2, 10 nodes between points
    game::RouteDef st;
    st.from = "a"; st.to = "b";
    st.points = {{Fixed{}, Fixed{}}, {Fixed::fromInt(10), Fixed{}}};
    st.offsets.assign(2, {}); st.zones.assign(2, 0); st.slowdown.assign(2, Fixed{});
    auto lines = game::buildRouteLines(st, rr);
    CHECK(lines.size() == 6);
    // 12 Catmull-Rom nodes 10/11 apart; BW trimming drops the first and the last node (the moved node is removed again)
    // Catmull-Rom with doubled end points eases in / out; BW trimming drops the nodes within 0.2 of the ends (and the
    // moved end node again), so the line starts beyond 0.2 and ends before 9.8 on the straight
    bool straight = lines[0].size() >= 8 && lines[0].size() <= 12 && lines[0].front().x >= Fixed::fromRatio(1, 5) && lines[0].back().x <= Fixed::fromRatio(49, 5);
    for (size_t i = 1; i < lines[0].size(); ++i) straight = straight && lines[0][i].x > lines[0][i - 1].x && lines[0][i].y.raw == 0;
    CHECK(straight && lines[1] == lines[0]);                             // no inner point: every line is the same
    // a bend: the inner control point gets the column offset of each line (even left, odd right, 0.5 / 1.5 / 2.5 columns)
    game::RouteDef bend = st;
    bend.points = {{Fixed{}, Fixed{}}, {Fixed::fromInt(5), Fixed{}}, {Fixed::fromInt(10), Fixed{}}};
    bend.offsets.assign(3, {}); bend.zones.assign(3, 0); bend.slowdown.assign(3, Fixed{});
    bend.offsets[1][3] = Fixed::fromRatio(1, 10);   // per-line extra offset (BW Spline.Offset)
    lines = game::buildRouteLines(bend, rr);
    const double want[6] = {-0.2, 0.2, -0.6, 0.7, -1.0, 1.0};
    for (int k = 0; k < 6; ++k) CHECK(nearFx(lines[size_t(k)][10].x, 5.0) && nearFx(lines[size_t(k)][10].y, want[k]));
    // raw nodes win over the control points (BW rawSplineLines)
    bend.lines = {{{Fixed{}, Fixed::fromInt(3)}, {Fixed::fromInt(10), Fixed::fromInt(3)}}};
    lines = game::buildRouteLines(bend, rr);
    CHECK(nearFx(lines[0][3].y, 3.0) && nearFx(lines[1][10].y, 0.2));
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
    CHECK(m.routesAuthored() && gs.lanes.size() == 6);                   // a-b only: b-c is closed by the rock
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
    CHECK(gs.lanes.size() == 12 && m.canSend(*sim, b, c));
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
