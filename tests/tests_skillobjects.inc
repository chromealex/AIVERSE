// ------------------------------------------------------------------ skill objects (agent-bw-skills, RtsSkillObjects.cpp)
// Rolling stones, jumping heads, mines left by units, magnets, souls: generic delivery keys `origins` / `consume` /
// `goal` / `onArrive` / `onExpire` / `onDeath`, effect `pull`.

static const char* kSkillObjRules = R"({
  "victory": "noEntities",
  "lanes": {"left": 1, "right": 1, "spacing": 0.4},
  "resources": {"mana": {"start": 100}},
  "statuses": {
    "magnet": {"tags": ["buff"], "tick": {"interval": 0.1, "effects": [
      {"type": "pull", "radius": 30, "inTransit": true, "affects": "all", "kind": "unit", "goal": "target", "speed": 2, "accel": 4, "maxSpeed": 10}]}}
  },
  "loadoutSlots": 30,
  "playerAbilities": [
    {"id": "petrify", "target": "group", "effects": [{"type": "area", "motion": "lane", "speed": 8, "radius": 0.5,
      "origins": {"shape": "group", "consume": true}, "onArrive": [{"type": "garrisonKill", "amount": 2, "affects": "any"}]}]},
    {"id": "heads", "target": "group", "effects": [{"type": "area", "goal": "groupTarget", "speed": 10, "radius": 0.5,
      "origins": {"shape": "group", "affects": "own", "consume": true}, "onArrive": [{"type": "garrisonKill", "amount": 1, "affects": "any"}]}]},
    {"id": "unitmines", "target": "group", "effects": [{"type": "area", "trigger": "contact", "maxTriggers": 1, "radius": 0.6, "lifetime": [1, 1.5],
      "origins": {"shape": "group", "consume": true}, "effects": [{"type": "kill", "shape": "target"}],
      "onExpire": [{"type": "spawn", "unit": "skeleton"}]}]},
    {"id": "magnet", "target": "structure", "effects": [{"type": "status", "status": "magnet", "duration": 30, "affects": "own"}]},
    {"id": "souls", "target": "structure", "effects": [{"type": "area", "motion": "follow", "radius": 40, "lifetime": 30, "trigger": "periodic", "interval": 5,
      "maxTriggers": 3, "affects": "enemies",
      "onDeath": [{"type": "projectile", "start": "point", "goal": "target", "speed": 20,
                   "effects": [{"type": "garrisonAdd", "amount": 1, "affects": "own"}]}]}]}
  ],
  "types": {
    "spore": {"move": {"speed": 2.4, "radius": 0.15}, "health": {"max": 1}, "entersGarrison": true},
    "hut": {"structure": {"footprint": [2, 2]}, "invulnerable": true, "level": 1, "garrison": {"max": 80, "unit": "spore", "interval": 100000, "capturable": true}},
    "skeleton": {"move": {"speed": 0, "radius": 0.3}, "health": {"max": 20}}
  }})";

static std::unique_ptr<Simulation> skillObjSim() {
    return makeSimJson(kSkillObjRules, flatMap(40, 16, R"(,"entities":[
      {"type":"hut","owner":1,"pos":[4,8],"garrison":30},{"type":"hut","owner":2,"pos":[36,8],"garrison":20},
      {"type":"hut","owner":2,"pos":[30,13],"garrison":10},{"type":"hut","owner":0,"pos":[20,8],"garrison":40}])"));
}
static std::vector<Entity> groupOf(Simulation& sim, uint8_t owner) {
    std::vector<Entity> g;
    sim.world().each<game::GroupTag, Owner>([&](Entity e, game::GroupTag&, Owner& o) { if (o.player == owner) g.push_back(e); });
    return g;
}
static int areaCount(Simulation& sim) { int n = 0; sim.world().each<game::Area>([&](Entity, game::Area&) { ++n; }); return n; }

TEST(skillobj_rolling_stones_continue_the_lane) {
    auto sim = skillObjSim();
    World& w = sim->world();
    Entity a1 = hutAt(*sim, 4, 8), nh = hutAt(*sim, 20, 8);
    w.get<game::Garrison>(a1).growth = Fixed{}; w.get<game::Garrison>(nh).growth = Fixed{};
    run(*sim, 0.5);
    send(*sim, 1, a1, nh, 20);
    run(*sim, 2);
    auto g = groupOf(*sim, 1);
    CHECK(g.size() >= 4);
    const int n = int(g.size());
    std::vector<uint16_t> lanePos;
    for (Entity e : g) lanePos.push_back(w.get<game::LaneWalker>(e).pos);
    cast(*sim, 2, "petrify", {}, g[0]);
    run(*sim, 0.3);                                                       // commands run after the input delay
    int alive = 0;
    for (Entity e : g) if (w.alive(e)) ++alive;
    CHECK(alive == 0);                                                    // every unit became a stone
    CHECK(areaCount(*sim) == n);
    // the stones keep rolling from where the units were (not from the lane start)
    bool fromUnits = true;
    w.each<game::Area>([&](Entity, game::Area& ar) { if (ar.lanePos == 0 && lanePos[0] > 1) fromUnits = false; if (ar.goal != nh) fromUnits = false; });
    CHECK(fromUnits);
    const int before = w.get<game::Garrison>(nh).count;
    run(*sim, 4);
    CHECK(areaCount(*sim) == 0);
    CHECK(w.get<game::Garrison>(nh).count == before - 2 * n);             // each stone hits the structure at the lane end
}

TEST(skillobj_jumping_heads_line_walkers) {
    auto sim = skillObjSim();
    World& w = sim->world();
    Entity a1 = hutAt(*sim, 4, 8), nh = hutAt(*sim, 20, 8);
    w.get<game::Garrison>(a1).growth = Fixed{}; w.get<game::Garrison>(nh).growth = Fixed{};
    run(*sim, 0.5);
    send(*sim, 1, a1, nh, 20);
    run(*sim, 1.5);
    auto g = groupOf(*sim, 1);
    const int n = int(g.size());
    CHECK(n >= 4);
    cast(*sim, 1, "heads", {}, g[0]);                                     // own group turns into heads flying at their target
    run(*sim, 0.3);
    CHECK(areaCount(*sim) == n);
    const int before = w.get<game::Garrison>(nh).count;
    Vec3 p0{};
    w.each<game::Area, Transform>([&](Entity, game::Area&, Transform& t) { p0 = t.pos; });
    run(*sim, 0.3);
    bool closer = true;
    w.each<game::Area, Transform>([&](Entity, game::Area&, Transform& t) { if (dist2DTest(t.pos, w.get<Transform>(nh).pos) >= dist2DTest(p0, w.get<Transform>(nh).pos)) closer = false; });
    CHECK(closer);
    run(*sim, 3);
    CHECK(areaCount(*sim) == 0);
    CHECK(w.get<game::Garrison>(nh).count == before - n);
}

TEST(skillobj_mines_left_by_units_expire) {
    auto sim = skillObjSim();
    World& w = sim->world();
    Entity a1 = hutAt(*sim, 4, 8), nh = hutAt(*sim, 20, 8);
    w.get<game::Garrison>(a1).growth = Fixed{};
    run(*sim, 0.5);
    send(*sim, 1, a1, nh, 10);
    run(*sim, 1.5);
    auto g = groupOf(*sim, 1);
    const int n = int(g.size());
    CHECK(n >= 2);
    cast(*sim, 2, "unitmines", {}, g[0]);
    run(*sim, 0.3);
    CHECK(areaCount(*sim) == n);                                          // a mine where each unit stood
    run(*sim, 0.5);
    CHECK(areaCount(*sim) == n);                                          // lifetime 1..1.5 s
    run(*sim, 1.0);
    CHECK(areaCount(*sim) == 0);
    CHECK(countType(*sim, "skeleton", 2) == n);                           // self-destruction (onExpire)
    // a mine that is used up does not self-destruct
    auto sim2 = skillObjSim();
    World& w2 = sim2->world();
    Entity c1 = hutAt(*sim2, 4, 8), cn = hutAt(*sim2, 20, 8);
    w2.get<game::Garrison>(c1).growth = Fixed{};
    run(*sim2, 0.5);
    send(*sim2, 1, c1, cn, 10);
    run(*sim2, 1.5);
    auto g2 = groupOf(*sim2, 1);
    cast(*sim2, 2, "unitmines", {}, g2[0]);
    run(*sim2, 0.3);
    std::vector<Vec3> mines;
    w2.each<game::Area, Transform>([&](Entity, game::Area&, Transform& t) { mines.push_back(t.pos); });
    send(*sim2, 1, c1, cn, 30);                                           // the next group: some step on the mines
    run(*sim2, 0.6);
    auto walkers = groupOf(*sim2, 1);
    CHECK(!walkers.empty() && !mines.empty());
    for (size_t i = 0; i < walkers.size() && i < mines.size(); ++i) w2.get<Transform>(walkers[i]).pos = mines[i];
    run(*sim2, 0.3);
    int died = 0;
    for (size_t i = 0; i < walkers.size() && i < mines.size(); ++i) if (!w2.alive(walkers[i])) ++died;
    CHECK(died >= 1);
    run(*sim2, 2);
    CHECK(countType(*sim2, "skeleton", 2) < int(g2.size()));
}

TEST(skillobj_magnet_pulls_groups_into_the_structure) {
    auto sim = skillObjSim();
    World& w = sim->world();
    Entity a1 = hutAt(*sim, 4, 8), b2 = hutAt(*sim, 30, 13), nh = hutAt(*sim, 20, 8);
    for (Entity e : {a1, b2, nh}) w.get<game::Garrison>(e).growth = Fixed{};
    w.get<Owner>(nh).player = 2;
    w.get<game::Garrison>(nh).count = 50;
    run(*sim, 0.5);
    cast(*sim, 2, "magnet", {}, nh);
    run(*sim, 0.1);
    send(*sim, 1, a1, b2, 20);
    run(*sim, 0.5);
    auto g = groupOf(*sim, 1);
    CHECK(!g.empty());
    bool pulled = true;
    for (Entity e : g) if (!w.alive(e) || !w.has<game::Pulled>(e) || w.get<game::GroupTag>(e).to != nh) pulled = false;
    CHECK(pulled);
    run(*sim, 6);
    int left = 0;
    for (Entity e : g) if (w.alive(e)) ++left;
    CHECK(left == 0);
    CHECK(w.get<game::Garrison>(nh).count < 50 && w.get<game::Garrison>(nh).lost > 0);   // they assaulted the magnet, not b2
    CHECK(w.get<game::Garrison>(b2).count == 10 && w.get<game::Garrison>(b2).lost == 0);
    // velocity profile: start 2, + 4/s, at most 10
    auto sim2 = skillObjSim();
    World& w2 = sim2->world();
    Entity c1 = hutAt(*sim2, 4, 8), cn = hutAt(*sim2, 20, 8), c2 = hutAt(*sim2, 30, 13);
    w2.get<Owner>(cn).player = 2;
    run(*sim2, 0.5);
    cast(*sim2, 2, "magnet", {}, cn);
    run(*sim2, 0.1);
    send(*sim2, 1, c1, c2, 20);
    run(*sim2, 0.3);
    auto g2 = groupOf(*sim2, 1);
    CHECK(!g2.empty() && w2.has<game::Pulled>(g2[0]));
    const game::Pulled p = w2.get<game::Pulled>(g2[0]);
    CHECK(p.vel > Fixed::fromInt(2) && p.vel <= Fixed::fromInt(10));
}

TEST(skillobj_souls_fly_into_the_collector) {
    auto sim = skillObjSim();
    World& w = sim->world();
    Entity a1 = hutAt(*sim, 4, 8), b1 = hutAt(*sim, 36, 8), nh = hutAt(*sim, 20, 8);
    for (Entity e : {a1, b1, nh}) w.get<game::Garrison>(e).growth = Fixed{};
    run(*sim, 0.5);
    cast(*sim, 2, "souls", {}, b1);
    send(*sim, 1, a1, nh, 30);
    run(*sim, 1.5);
    auto g = groupOf(*sim, 1);
    CHECK(g.size() >= 5);
    const int before = w.get<game::Garrison>(b1).count;
    for (Entity e : g) w.get<game::Unit>(e).hp = Fixed{};                  // they die inside the collector's range
    run(*sim, 3);
    CHECK(w.get<game::Garrison>(b1).count == before + 3);                 // maxTriggers: at most 3 souls
    CHECK(areaCount(*sim) == 0);
}

TEST(skillobj_save_load_and_determinism) {
    auto script = [](bool reload) {
        auto sim = skillObjSim();
        World& w = sim->world();
        Entity a1 = hutAt(*sim, 4, 8), nh = hutAt(*sim, 20, 8), b2 = hutAt(*sim, 30, 13);
        w.get<Owner>(nh).player = 2;
        run(*sim, 0.5);
        cast(*sim, 2, "magnet", {}, nh);
        send(*sim, 1, a1, b2, 30);
        run(*sim, 0.6);
        auto g = groupOf(*sim, 1);
        if (!g.empty()) cast(*sim, 2, "unitmines", {}, g[0]);
        send(*sim, 1, a1, nh, 30);
        run(*sim, 0.4);
        if (reload) {
            const std::string bytes = sim->serialize();
            auto b = skillObjSim();
            std::string err;
            CHECK(b->deserialize(bytes, &err));
            CHECK(b->checksum() == sim->checksum());
            sim = std::move(b);
        }
        run(*sim, 3);
        return sim->checksum();
    };
    CHECK(script(false) == script(false));
    CHECK(script(false) == script(true));
}
