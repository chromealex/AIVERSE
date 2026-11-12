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

// ------------------------------------------------------------------ stage 2: BW behaviours as generic keys
static const char* kSkillObj2Rules = R"({
  "victory": "noEntities",
  "lanes": {"left": 1, "right": 1, "spacing": 0.4},
  "resources": {"energy": {"start": 0}, "morale": {"start": 0, "capped": true, "storage": 8000, "levels": [{"at": 0}, {"at": 500}, {"at": 1000}, {"at": 2000}]}},
  "loadoutSlots": 10,
  "playerAbilities": [
    {"id": "killzone", "target": "point", "effects": [{"type": "area", "trigger": "periodic", "interval": 0.05, "lifetime": 3, "radius": 1, "grow": 2,
      "effects": [{"type": "kill", "kind": "unit", "radius": "area", "budget": 3}]}]},
    {"id": "amulet", "target": "structure", "effects": [{"type": "status", "duration": 10, "affects": "any", "status": {"id": "amu",
      "tick": {"interval": 0.05, "effects": [{"type": "sendBack", "mode": "reverse", "radius": 30, "inTransit": true, "heading": "to", "kind": "unit", "affects": "any", "budget": 2}]}}}]},
    {"id": "stars", "target": "none", "effects": [{"type": "resource", "resource": "morale", "levels": 1.5, "affects": "self"}]},
    {"id": "mute1", "target": "none", "effects": [{"type": "playerStatus", "duration": 10, "affects": "enemies", "status": {"id": "mute", "flags": ["silence"], "slots": [1]}}]},
    {"id": "a0", "target": "none", "effects": [{"type": "resource", "resource": "energy", "amount": 1, "affects": "self"}]},
    {"id": "a1", "target": "none", "effects": [{"type": "resource", "resource": "energy", "amount": 1, "affects": "self"}]},
    {"id": "coop", "target": "structure", "effects": [{"type": "status", "duration": 30, "affects": "own", "status": {"id": "coop",
      "onSpawn": [{"type": "status", "status": {"id": "chick", "mods": [{"stat": "speed", "mul": 2}]}, "shape": "target", "affects": "any"}]}}]},
    {"id": "haunt", "target": "structure", "effects": [{"type": "garrisonAdd", "amount": 10, "affects": "any"},
      {"type": "status", "duration": 2, "affects": "any", "status": {"id": "haunt", "endOnCapture": true, "onExpire": [{"type": "garrisonKill", "amount": 10, "shape": "target", "affects": "any"}]}}]},
    {"id": "hypno", "target": "structure", "effects": [{"type": "status", "duration": 20, "status": {"id": "hyp", "command": {"capture": true}}}]},
    {"id": "tele", "target": "structure", "effects": [{"type": "status", "duration": 30, "affects": "own", "status": {"id": "tel",
      "transfer": {"grab": 1, "fill": 1, "speed": 20, "percent": 0.5}}}]},
    {"id": "panic", "target": "structure", "effects": [{"type": "garrisonSend", "percent": 1, "maxPoints": 2, "runTo": {"affects": "own"}}]},
    {"id": "bombs", "target": "none", "effects": [{"type": "cast", "stagger": 1, "shape": "map", "kind": "structure", "affects": "enemies", "neutral": true,
      "effects": [{"type": "garrisonKill", "amount": 5, "shape": "target", "affects": "any"}]}]},
    {"id": "endgame", "target": "structure", "effects": [{"type": "status", "duration": 60, "affects": "any", "status": {"id": "eg",
      "onCaptured": [{"type": "defeat", "shape": "map", "affects": "enemies"}]}}]},
    {"id": "tornado", "target": "point", "effects": [{"type": "area", "motion": "orbit", "orbit": {"a": 4, "b": 2, "speed": 90, "expand": 1},
      "trigger": "periodic", "interval": 0.05, "lifetime": 4, "radius": 0.5}]},
    {"id": "freeze", "target": "point", "effects": [{"type": "status", "radius": 30, "kind": "unit", "inTransit": true, "duration": [2, 4],
      "status": {"id": "fz", "flags": ["stun"], "stacking": "keep"}}]}
  ],
  "types": {
    "spore": {"move": {"speed": 2.4, "radius": 0.15}, "health": {"max": 1}, "entersGarrison": true},
    "hut": {"structure": {"footprint": [2, 2]}, "invulnerable": true, "level": 1, "garrison": {"max": 80, "unit": "spore", "interval": 100000, "capturable": true}}
  }})";

static std::unique_ptr<Simulation> skillObj2Sim(const std::string& loadA = "") {
    std::string players = std::string("[{\"name\":\"A\",\"team\":1") + (loadA.empty() ? "" : ",\"loadout\":" + loadA) + "},{\"name\":\"B\",\"team\":2}]";
    return makeSimJson(kSkillObj2Rules, flatMap(40, 16, R"(,"entities":[
      {"type":"hut","owner":1,"pos":[4,8],"garrison":30},{"type":"hut","owner":2,"pos":[36,8],"garrison":20},
      {"type":"hut","owner":2,"pos":[30,13],"garrison":10},{"type":"hut","owner":0,"pos":[20,8],"garrison":40},{"type":"hut","owner":1,"pos":[10,3],"garrison":5}])", players));
}

TEST(skillobj2_budget_grow_heading) {
    auto sim = skillObj2Sim();
    World& w = sim->world();
    Entity a1 = hutAt(*sim, 4, 8), nh = hutAt(*sim, 20, 8);
    send(*sim, 1, a1, nh, 30);
    run(*sim, 1.5);
    const int n = int(groupOf(*sim, 1).size());
    CHECK(n >= 6);
    cast(*sim, 2, "killzone", w.get<Transform>(groupOf(*sim, 1)[0]).pos);
    run(*sim, 3.5);
    int left = 0;
    for (Entity e : groupOf(*sim, 1)) if (w.alive(e)) ++left;
    CHECK(n - left == 3);                                                 // budget: 3 in total over the area's life
    // amulet: only the units heading for the holder turn round, at most 2
    auto sim2 = skillObj2Sim();
    World& w2 = sim2->world();
    Entity b1 = hutAt(*sim2, 4, 8), bn = hutAt(*sim2, 20, 8), b2 = hutAt(*sim2, 10, 3);
    send(*sim2, 1, b1, bn, 30);
    run(*sim2, 0.6);
    send(*sim2, 1, b1, b2, 30);
    run(*sim2, 0.6);
    cast(*sim2, 2, "amulet", {}, bn);
    run(*sim2, 2);
    int reversed = 0, wrong = 0;
    for (Entity e : groupOf(*sim2, 1)) {
        auto& q = w2.get<game::Orders>(e).queue;
        if (q.empty()) continue;
        if (w2.get<game::GroupTag>(e).to == b1) { ++reversed; if (w2.get<game::GroupTag>(e).from == b2) ++wrong; }
    }
    CHECK(reversed == 2 && wrong == 0);
}

TEST(skillobj2_levels_silence_slots_spawn_payloads) {
    auto sim = skillObj2Sim(R"(["a0","a1","stars","coop","haunt","tele"])");
    World& w = sim->world();
    auto& m = mod(*sim);
    auto& gs = m.state(*sim);
    const int morale = m.rules().findResource("morale");
    cast(*sim, 1, "stars", {});
    run(*sim, 0.3);
    CHECK(gs.players[1].stock[size_t(morale)] == Fixed::fromInt(750));     // 1.5 levels: 500 + half of 500..1000
    // per-slot silence: only loadout slot 1 (a1) is blocked
    cast(*sim, 2, "mute1", {});
    run(*sim, 0.3);
    CHECK(m.canUsePlayerAbility(*sim, 1, paIndex(*sim, "a0")) && !m.canUsePlayerAbility(*sim, 1, paIndex(*sim, "a1")));
    // onSpawn: units leaving the coop get the speed status
    Entity a1 = hutAt(*sim, 4, 8), nh = hutAt(*sim, 20, 8);
    cast(*sim, 1, "coop", {}, a1);
    run(*sim, 0.3);
    send(*sim, 1, a1, nh, 20);
    run(*sim, 1);
    bool fast = !groupOf(*sim, 1).empty();
    for (Entity e : groupOf(*sim, 1)) if (fxAbs(m.stat(*sim, e, game::Stat::Speed) - Fixed::fromRatio(48, 10)) > Fixed::fromRatio(1, 100)) fast = false;
    CHECK(fast);
    // haunted house: +10 for 2 s, then -10
    Entity a2 = hutAt(*sim, 10, 3);
    w.get<game::Garrison>(a2).count = 5;
    cast(*sim, 1, "haunt", {}, a2);
    run(*sim, 0.5);
    CHECK(w.get<game::Garrison>(a2).count == 15);
    run(*sim, 2);
    CHECK(w.get<game::Garrison>(a2).count == 5);
    // teleport: the next send from a1 moves half the garrison to a2 without walking
    w.get<game::Garrison>(a1).count = 20;
    cast(*sim, 1, "tele", {}, a1);
    run(*sim, 0.3);
    const int before = w.get<game::Garrison>(a2).count;
    send(*sim, 1, a1, a2, 50);
    run(*sim, 4);
    CHECK(w.get<game::Garrison>(a2).count == before + 10);
}

TEST(skillobj2_hypnosis_panic_bombs_endgame_orbit_freeze) {
    auto sim = skillObj2Sim();
    World& w = sim->world();
    Entity b1 = hutAt(*sim, 36, 8), b2 = hutAt(*sim, 30, 13), nh = hutAt(*sim, 20, 8);
    // hypnosis: player 1 sends out of b1 once; the units become player 1's
    cast(*sim, 1, "hypno", {}, b1);
    run(*sim, 0.3);
    send(*sim, 1, b1, nh, 50);
    run(*sim, 1.5);
    CHECK(countType(*sim, "spore", 1) >= 5 && countType(*sim, "spore", 2) == 0);
    send(*sim, 1, b1, nh, 50);                                            // used up
    run(*sim, 0.5);
    CHECK(w.get<game::Garrison>(b1).count >= 9);
    // panic: b2's garrison walks to player 2's own structures (b1)
    w.get<game::Garrison>(b2).count = 10;
    cast(*sim, 1, "panic", {}, b2);
    run(*sim, 0.5);
    CHECK(w.get<game::Garrison>(b2).count == 0 && countType(*sim, "spore", 2) >= 1);
    // staggered bombs: one target per second
    auto sim2 = skillObj2Sim();
    World& w2 = sim2->world();
    cast(*sim2, 1, "bombs", {});
    run(*sim2, 0.3);
    int hit = 0;
    for (Entity s : {hutAt(*sim2, 36, 8), hutAt(*sim2, 30, 13), hutAt(*sim2, 20, 8)}) if (w2.get<game::Garrison>(s).count < 10 || (s == hutAt(*sim2, 36, 8) && w2.get<game::Garrison>(s).count == 15)) ++hit;
    CHECK(hit == 1);
    run(*sim2, 2.2);
    CHECK(w2.get<game::Garrison>(hutAt(*sim2, 20, 8)).count == 35 && w2.get<game::Garrison>(hutAt(*sim2, 30, 13)).count == 5);
    // end game: capturing the marked structure defeats the enemies of the status owner
    auto sim3 = skillObj2Sim();
    World& w3 = sim3->world();
    Entity c1 = hutAt(*sim3, 4, 8), cn = hutAt(*sim3, 20, 8);
    w3.get<game::Garrison>(cn).count = 1;
    cast(*sim3, 1, "endgame", {}, cn);
    run(*sim3, 0.3);
    send(*sim3, 1, c1, cn, 50);
    run(*sim3, 10);
    CHECK(w3.get<Owner>(cn).player == 1 && !sim3->player(2)->alive);
    // orbit: an ellipse 4 x 2 around the click
    auto sim4 = skillObj2Sim();
    World& w4 = sim4->world();
    cast(*sim4, 1, "tornado", P(20, 3));
    run(*sim4, 2.3);
    bool on = false;
    w4.each<game::Area, Transform>([&](Entity, game::Area&, Transform& t) {
        Fixed dx = (t.pos.x - Fixed::fromInt(20)) / 4, dz = (t.pos.z - Fixed::fromInt(3)) / 2;
        on = fxAbs(dx * dx + dz * dz - Fixed::fromInt(1)) < Fixed::fromRatio(5, 100);
    });
    CHECK(on);
    // freeze keep + random duration in [2, 4]
    auto sim5 = skillObj2Sim();
    World& w5 = sim5->world();
    send(*sim5, 1, hutAt(*sim5, 4, 8), hutAt(*sim5, 20, 8), 30);
    run(*sim5, 1);
    cast(*sim5, 2, "freeze", P(8, 8));
    run(*sim5, 0.3);
    bool ok = true;
    for (Entity e : groupOf(*sim5, 1))
        for (auto& in : w5.get<game::Statuses>(e).list) {
            const uint32_t d = in.until - sim5->tick();
            if (d < 30 || d > 80) ok = false;
        }
    CHECK(ok);
}

// ---- stage 2b: redirect (collect), random swap, kill aura (hitCap, skillImmune), betrayal (convert payload, targetChance),
// blizzard (cast offsets)
static const char* kSkillObj3Rules = R"({
  "victory": "noEntities",
  "lanes": {"left": 1, "right": 1, "spacing": 0.4},
  "resources": {"energy": {"start": 0}},
  "playerAbilities": [
    {"id": "collect", "target": "structure", "effects": [{"type": "status", "duration": 30, "affects": "own", "status": {"id": "col", "collect": {"addPerSecond": 0.25}}}]},
    {"id": "swp", "target": "structure", "effects": [{"type": "swap", "affects": "own", "with": {"affects": "enemies"},
      "effects": [{"type": "status", "duration": 5, "shape": "target", "kind": "structure", "affects": "own", "status": {"id": "sdef", "mods": [{"stat": "defense", "mul": 2}]}}]}]},
    {"id": "aura", "target": "point", "effects": [{"type": "status", "radius": 30, "kind": "unit", "inTransit": true, "affects": "own",
      "status": {"id": "ka", "flags": ["skillImmune"], "hitCap": 0.5}}]},
    {"id": "zap", "target": "point", "effects": [{"type": "kill", "radius": 30, "kind": "unit", "affects": "enemies"}]},
    {"id": "betray", "target": "point", "effects": [
      {"type": "convert", "radius": 30, "kind": "unit", "inTransit": true, "maxTargets": 3,
       "effects": [{"type": "status", "shape": "target", "kind": "unit", "affects": "own", "status": {"id": "bt", "mods": [{"stat": "speed", "mul": 2}]}}]},
      {"type": "kill", "radius": 30, "kind": "unit", "inTransit": true, "targetChance": 1}]},
    {"id": "inv", "target": "structure", "effects": [{"type": "garrisonKill", "amount": 1, "affects": "any"}, {"type": "status", "duration": 30, "affects": "any",
      "status": {"id": "inv", "flags": ["invulnerable"], "endWhenEmpty": true, "tick": {"interval": 0.5, "effects": [{"type": "garrisonKill", "amount": 1, "shape": "target", "affects": "any"}]},
                 "mods": [{"stat": "defense", "lerp": 0.5}]}}]},
    {"id": "sac", "target": "point", "effects": [{"type": "status", "radius": 30, "kind": "unit", "inTransit": true, "affects": "own",
      "status": {"id": "sac", "deathGain": {"energy": 2}}}]},
    {"id": "slow2", "target": "point", "effects": [{"type": "status", "radius": 30, "kind": "unit", "inTransit": true, "affects": "enemies",
      "status": {"id": "sl2", "stacking": "independent", "mods": [{"stat": "speed", "mul": 0.5, "floor": 0.3333}]}}]},
    {"id": "rain", "target": "point", "effects": [{"type": "cast", "offsets": [[0, 0], [3, 0], [6, 0]], "shuffle": true, "shots": 3, "stagger": 1,
      "effects": [{"type": "area", "delay": 0.2, "radius": 0.5, "effects": [{"type": "garrisonKill", "amount": 5, "radius": 1, "kind": "structure", "affects": "any"}]}]}]}
  ],
  "types": {
    "spore": {"move": {"speed": 2.4, "radius": 0.15}, "health": {"max": 1}, "entersGarrison": true},
    "hut": {"structure": {"footprint": [2, 2]}, "invulnerable": true, "level": 1, "garrison": {"max": 80, "unit": "spore", "interval": 1, "capturable": true}}
  }})";

static std::unique_ptr<Simulation> skillObj3Sim() {
    return makeSimJson(kSkillObj3Rules, flatMap(40, 16, R"(,"entities":[
      {"type":"hut","owner":1,"pos":[4,8],"garrison":30},{"type":"hut","owner":2,"pos":[36,8],"garrison":20},
      {"type":"hut","owner":2,"pos":[30,13],"garrison":10},{"type":"hut","owner":0,"pos":[20,8],"garrison":40},{"type":"hut","owner":1,"pos":[10,3],"garrison":5}])",
      "[{\"name\":\"A\",\"team\":1},{\"name\":\"B\",\"team\":2}]"));
}

TEST(skillobj3_collect_swap_aura_betrayal_rain) {
    // collect: (10,3) feeds (4,8) and grows 0.25 / s itself
    {
        auto sim = skillObj3Sim();
        World& w = sim->world();
        Entity a = hutAt(*sim, 4, 8), b = hutAt(*sim, 10, 3);
        cast(*sim, 1, "collect", {}, a);
        run(*sim, 0.3);
        const int a0 = w.get<game::Garrison>(a).count, b0 = w.get<game::Garrison>(b).count;
        run(*sim, 4);
        const int da = w.get<game::Garrison>(a).count - a0, db = w.get<game::Garrison>(b).count - b0;
        CHECK(da >= 7 && da <= 9);
        CHECK(db >= 0 && db <= 1);
    }
    // swap with a random enemy structure; both get the after-swap status (own, relative to the caster)
    {
        auto sim = skillObj3Sim();
        World& w = sim->world();
        Entity a = hutAt(*sim, 10, 3), e1 = hutAt(*sim, 36, 8), e2 = hutAt(*sim, 30, 13);
        cast(*sim, 1, "swp", {}, a);
        run(*sim, 0.3);
        CHECK(w.get<Owner>(a).player == 2);
        const bool one = (w.get<Owner>(e1).player == 1) != (w.get<Owner>(e2).player == 1);
        CHECK(one);
        Entity mine = w.get<Owner>(e1).player == 1 ? e1 : e2;
        CHECK(w.get<game::Garrison>(mine).count <= 7 && !w.get<game::Statuses>(mine).list.empty());
        CHECK(w.get<game::Garrison>(a).count >= 10);
    }
    // kill aura: skill filters skip the units; a weapon hit is absorbed and cuts the time left to 0.5 s
    {
        auto sim = skillObj3Sim();
        World& w = sim->world();
        send(*sim, 1, hutAt(*sim, 4, 8), hutAt(*sim, 20, 8), 30);
        run(*sim, 1);
        cast(*sim, 1, "aura", P(8, 8));
        run(*sim, 0.3);
        const int n = int(groupOf(*sim, 1).size());
        CHECK(n > 0);
        cast(*sim, 2, "zap", P(8, 8));
        run(*sim, 0.3);
        CHECK(int(groupOf(*sim, 1).size()) == n);
        Entity u = groupOf(*sim, 1)[0];
        game::WeaponDef wd;
        mod(*sim).dealDamage(*sim, u, Fixed::fromInt(100), 0, kNullEntity, 2, &wd);
        CHECK(w.alive(u) && w.get<game::Unit>(u).hp.raw > 0);
        CHECK(w.get<game::Statuses>(u).list[0].until <= sim->tick() + 10);
        run(*sim, 0.6);
        mod(*sim).dealDamage(*sim, u, Fixed::fromInt(100), 0, kNullEntity, 2, &wd);
        CHECK(w.get<game::Unit>(u).hp.raw <= 0);
    }
    // sacrifice: the owner gains per unit killed; Slowdown2: each cast halves the speed, never below 1/3
    {
        auto sim = skillObj3Sim();
        send(*sim, 1, hutAt(*sim, 4, 8), hutAt(*sim, 20, 8), 30);
        run(*sim, 1);
        const int n = int(groupOf(*sim, 1).size());
        Entity u = groupOf(*sim, 1)[0];
        const Fixed s0 = mod(*sim).stat(*sim, u, game::Stat::Speed);
        cast(*sim, 2, "slow2", P(8, 8));
        run(*sim, 0.3);
        CHECK(fxAbs(mod(*sim).stat(*sim, u, game::Stat::Speed) - s0 / 2) < Fixed::fromRatio(1, 100));
        cast(*sim, 2, "slow2", P(8, 8));
        run(*sim, 0.3);
        CHECK(fxAbs(mod(*sim).stat(*sim, u, game::Stat::Speed) - s0 * Fixed::fromRatio(3333, 10000)) < Fixed::fromRatio(1, 100));
        cast(*sim, 1, "sac", P(8, 8));
        run(*sim, 0.3);
        const Fixed e0 = stockOf(*sim, 1, "energy");
        cast(*sim, 2, "zap", P(8, 8));
        run(*sim, 0.3);
        CHECK(stockOf(*sim, 1, "energy") - e0 == Fixed::fromInt(2 * n));
    }
    // betrayal: 3 walkers change sides with the speed status, the rest die (chance 1)
    {
        auto sim = skillObj3Sim();
        World& w = sim->world();
        send(*sim, 1, hutAt(*sim, 4, 8), hutAt(*sim, 20, 8), 30);
        run(*sim, 1.5);
        CHECK(groupOf(*sim, 1).size() > 3);
        cast(*sim, 2, "betray", P(8, 8));
        run(*sim, 0.3);
        auto g2 = groupOf(*sim, 2);
        int alive1 = 0;
        for (Entity e : groupOf(*sim, 1)) if (w.alive(e) && w.get<game::Unit>(e).hp.raw > 0) ++alive1;
        CHECK(g2.size() == 3 && alive1 == 0);
        bool st = true;
        for (Entity e : g2) st = st && !w.get<game::Statuses>(e).list.empty();
        CHECK(st);
    }
    // invulnerability upkeep: 1 unit at once and 1 per 0.5 s, ends when nothing is left; defence lerps halfway to 1
    {
        auto sim = skillObj3Sim();
        World& w = sim->world();
        Entity nh = hutAt(*sim, 20, 8);
        const Fixed d0 = mod(*sim).stat(*sim, nh, game::Stat::Defense);
        cast(*sim, 1, "inv", {}, nh);
        run(*sim, 0.3);
        const Fixed d1 = mod(*sim).stat(*sim, nh, game::Stat::Defense);
        CHECK(fxAbs(d1 - (d0 + (Fixed::fromInt(1) - d0) / 2)) < Fixed::fromRatio(1, 100));
        CHECK(w.get<game::Garrison>(nh).count == 39);
        run(*sim, 2.0);
        CHECK(w.get<game::Garrison>(nh).count == 35);
        w.get<game::Garrison>(nh).count = 1;
        run(*sim, 1.2);
        CHECK(w.get<game::Garrison>(nh).count == 0 && w.get<game::Statuses>(nh).list.empty());
    }
    // rain: three points 1 s apart in a random order; only the one on the hut hits it
    {
        auto sim = skillObj3Sim();
        World& w = sim->world();
        Entity nh = hutAt(*sim, 20, 8);
        cast(*sim, 1, "rain", P(20, 8));
        run(*sim, 3.5);
        CHECK(w.get<game::Garrison>(nh).count == 35);
    }
}
