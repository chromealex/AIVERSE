// Sensors (radar, sonar, jammers, stealth, underwater) and field operations (repair, reclaim, assist, patrol work,
// self-destruct, stockpiles, interceptors, ambient income, attack runs, repair pads). Included after tests_sc2.inc
// (uses the tests_mech.inc helpers). Everything is off unless the rules ask.
#include "../engine/data/JsonCompose.h"

static const char* kFoVic = R"("victory": {"mode": "score", "seconds": 10000, "keyLoses": false})";

static std::string foRules(const std::string& extra, const std::string& types) {
    return std::string(R"({"resources": {"metal": {"kind": "flow", "start": 1000, "storage": 100000}, "energy": {"kind": "flow", "start": 1000, "storage": 100000}}, )") +
           kFoVic + extra + R"(, "types": {)" + types + "}}";
}

TEST(sensors_radar_sonar_jammer_stealth) {
    const std::string types = R"(
        "radar": {"structure": {"footprint": [1, 1]}, "health": {"max": 100}, "sight": 3, "radar": 30, "sonar": 30},
        "scout": {"move": {"speed": 1}, "health": {"max": 50}, "sight": 2},
        "ghost": {"move": {"speed": 1}, "health": {"max": 50}, "sight": 2, "stealth": true},
        "sub":   {"move": {"speed": 1}, "health": {"max": 50}, "sight": 2, "underwater": true},
        "jam":   {"structure": {"footprint": [1, 1]}, "health": {"max": 100}, "sight": 2, "jammer": 6})";
    const std::string map = flatMap(48, 24, R"(,"entities":[{"type":"radar","owner":1,"pos":[4,12]},
        {"type":"scout","owner":2,"pos":[20,6]},{"type":"ghost","owner":2,"pos":[20,12]},{"type":"scout","owner":2,"pos":[24,20]},
        {"type":"jam","owner":2,"pos":[26,20]}])");
    auto sim = makeSimJson(foRules("", types), map);
    run(*sim, 0.5);
    game::Sensors* sn = mod(*sim).sensors();
    CHECK(sn != nullptr);
    Entity scout = unitNear(*sim, 20, 6), ghost = unitNear(*sim, 20, 12), jammed = unitNear(*sim, 24, 20);
    CHECK(sn->onRadar(*sim, scout, 1));           // far outside sight, inside the radar circle
    CHECK(!sn->onRadar(*sim, ghost, 1));          // stealth
    CHECK(!sn->onRadar(*sim, jammed, 1));         // under its own side's jammer
    CHECK(!sn->onRadar(*sim, scout, 2));          // own units are never contacts
    auto blips = sn->blips(*sim, 1);
    CHECK(blips.size() == 1 && blips[0] == scout);
    // the radar dies: nothing tracked
    sim->world().get<game::Unit>(findType(*sim, "radar", 1)).hp = Fixed{};
    run(*sim, 0.2);
    CHECK(!sn->onRadar(*sim, scout, 1));
}

TEST(sensors_radar_targeting_and_underwater_weapons) {
    auto rules = [](bool targeting) {
        return foRules(targeting ? R"(, "radar": {"targeting": true})" : "", R"(
        "radar": {"structure": {"footprint": [1, 1]}, "health": {"max": 100}, "sight": 3, "radar": 30},
        "gun":   {"structure": {"footprint": [1, 1]}, "health": {"max": 500}, "sight": 4, "weapons": [{"range": 14, "damage": 20, "cooldown": 1}]},
        "dummy": {"move": {"speed": 0.01}, "health": {"max": 1000}, "sight": 1})");
    };
    const std::string map = flatMap(48, 24, R"(,"entities":[{"type":"radar","owner":1,"pos":[4,4]},{"type":"gun","owner":1,"pos":[10,12]},{"type":"dummy","owner":2,"pos":[22,12]}])");
    for (bool on : {false, true}) {
        auto sim = makeSimJson(rules(on), map);
        Entity d = findType(*sim, "dummy", 2);
        run(*sim, 4);
        CHECK((hpOf(*sim, d) < Fixed::fromInt(1000)) == on);   // fires at a radar contact beyond its sight only when asked
        if (on) CHECK(mechRoundTrip(*sim, rules(on), map));
    }
    // underwater targets: only "underwater" weapons
    const std::string uw = foRules("", R"(
        "tank": {"move": {"speed": 0.01}, "health": {"max": 500}, "sight": 8, "weapons": [{"range": 6, "damage": 20, "cooldown": 1}]},
        "torp": {"move": {"speed": 0.01}, "health": {"max": 500}, "sight": 8, "weapons": [{"range": 6, "damage": 20, "cooldown": 1, "underwater": true}]},
        "sub":  {"move": {"speed": 0.01}, "health": {"max": 1000}, "sight": 2, "underwater": true})");
    auto sim = makeSimJson(uw, flatMap(48, 24, R"(,"entities":[{"type":"tank","owner":1,"pos":[10,6]},{"type":"sub","owner":2,"pos":[14,6]},
        {"type":"torp","owner":1,"pos":[10,18]},{"type":"sub","owner":2,"pos":[14,18]}])"));
    Entity s1 = unitNear(*sim, 14, 6), s2 = unitNear(*sim, 14, 18);
    sim->submit(mechCmd(1, game::kCmdAttack, {unitNear(*sim, 10, 6)}, s1));
    run(*sim, 3);
    CHECK(hpOf(*sim, s1) == Fixed::fromInt(1000));
    CHECK(hpOf(*sim, s2) < Fixed::fromInt(1000));
}

TEST(fieldops_repair_reclaim_assist_patrol) {
    const std::string types = R"(
        "con":  {"move": {"speed": 3, "radius": 0.4}, "health": {"max": 100}, "sight": 8, "builds": ["lab"], "buildPower": 2, "assist": true, "patrolWork": true,
                 "gather": {"metal": {"carry": 10, "time": 1}}},
        "lab":  {"structure": {"footprint": [2, 2]}, "health": {"max": 1000}, "produces": ["tank"], "buildPower": 1},
        "tank": {"move": {"speed": 2}, "health": {"max": 200}, "sight": 5, "cost": {"metal": 100, "energy": 500, "time": 20}},
        "rock": {"invulnerable": true, "resourceNode": {"resource": "metal", "amount": 40}})";
    const std::string rules = foRules(R"(, "costModel": "progressive", "repair": {"cost": 0, "units": true}, "reclaim": {"units": true, "refund": 1})", types);
    const std::string map = flatMap(48, 24, R"(,"entities":[{"type":"con","owner":1,"pos":[6,6]},{"type":"tank","owner":1,"pos":[9,6]},
        {"type":"tank","owner":2,"pos":[6,16]},{"type":"lab","owner":1,"pos":[30,6]},{"type":"con","owner":1,"pos":[34,9]},{"type":"rock","owner":0,"pos":[20,20]}])");
    // repair a mobile unit for free
    {
        auto sim = makeSimJson(rules, map);
        Entity con = unitNear(*sim, 6, 6), tank = unitNear(*sim, 9, 6);
        sim->world().get<game::Unit>(tank).hp = Fixed::fromInt(50);
        const Fixed metal = stockOf(*sim, 1, "metal");
        sim->submit(mechCmd(1, game::kCmdRepair, {con}, tank));
        run(*sim, 15);
        CHECK(hpOf(*sim, tank) == Fixed::fromInt(200));
        CHECK(stockOf(*sim, 1, "metal") == metal);
        // reclaim the enemy tank: metal back, no wreck, no death event
        Entity enemy = unitNear(*sim, 6, 16);
        sim->submit(mechCmd(1, game::kCmdReclaim, {con}, enemy));
        bool died = false;
        for (int i = 0; i < 20 * 20 && sim->world().alive(enemy); ++i) { sim->step(); for (auto& ev : sim->events()) if (ev.type == game::kEvDied && ev.a == enemy) died = true; }
        run(*sim, 0.2);
        CHECK(!sim->world().alive(enemy) && !died);
        CHECK(stockOf(*sim, 1, "metal") > metal + Fixed::fromInt(90));
    }
    // a guarding builder speeds the factory up
    auto progressAfter = [&](bool assist) {
        auto sim = makeSimJson(rules, map);
        Entity lab = findType(*sim, "lab", 1);
        sim->submit(mechCmd(1, game::kCmdTrain, {lab}, kNullEntity, {}, mod(*sim).rules().findType("tank")));
        if (assist) sim->submit(mechCmd(1, game::kCmdGuard, {unitNear(*sim, 34, 9)}, lab));
        run(*sim, 5);
        auto& q = sim->world().get<game::Production>(lab).queue;
        return q.empty() ? Fixed::fromInt(1) : q[0].progress;
    };
    const Fixed alone = progressAfter(false), helped = progressAfter(true);
    CHECK(alone > Fixed{} && helped > alone * 2);
    // patrol work: a builder patrolling past a wounded ally repairs it, then a rock, and keeps patrolling
    {
        auto sim = makeSimJson(rules, map);
        Entity con = unitNear(*sim, 6, 6), tank = unitNear(*sim, 9, 6);
        sim->world().get<game::Unit>(tank).hp = Fixed::fromInt(100);
        sim->submit(mechCmd(1, game::kCmdPatrol, {con}, kNullEntity, at(14, 6)));
        run(*sim, 12);
        CHECK(hpOf(*sim, tank) == Fixed::fromInt(200));
        auto& q = sim->world().get<game::Orders>(con).queue;
        CHECK(!q.empty() && q.back().type == game::OrderType::Patrol);
        CHECK(mechRoundTrip(*sim, rules, map));
    }
}

TEST(fieldops_self_destruct) {
    const std::string rules = foRules(R"(, "selfDestruct": {"time": 2})", R"("tank": {"move": {"speed": 2}, "health": {"max": 200}})");
    const std::string map = flatMap(24, 12, R"(,"entities":[{"type":"tank","owner":1,"pos":[4,4]},{"type":"tank","owner":1,"pos":[8,4]}])");
    auto sim = makeSimJson(rules, map);
    Entity a = unitNear(*sim, 4, 4), b = unitNear(*sim, 8, 4);
    sim->submit(mechCmd(1, game::kCmdSelfDestruct, {a, b}));
    run(*sim, 1);
    CHECK(mod(*sim).fieldOps()->selfDestructAt(*sim, a) > sim->tick());
    sim->submit(mechCmd(1, game::kCmdSelfDestruct, {b}));   // again: disarmed
    run(*sim, 0.1);
    CHECK(mechRoundTrip(*sim, rules, map, 20));
    run(*sim, 1.5);
    CHECK(!sim->world().alive(a) && sim->world().alive(b));
    // without rules "selfDestruct" the command does nothing
    auto plain = makeSimJson(foRules("", R"("tank": {"move": {"speed": 2}, "health": {"max": 200}})"), map);
    Entity c = unitNear(*plain, 4, 4);
    plain->submit(mechCmd(1, game::kCmdSelfDestruct, {c}));
    run(*plain, 8);
    CHECK(plain->world().alive(c));
}

TEST(fieldops_stockpile_and_interceptor) {
    auto rules = [](bool antiMissile) {
        return foRules(R"(, "costModel": "progressive")", std::string(R"(
        "silo": {"structure": {"footprint": [2, 2]}, "health": {"max": 1000}, "tags": ["missile"],
                 "stockpile": {"max": 2, "time": 2, "cost": {"metal": 100}, "abilities": ["launch"]},
                 "abilities": [{"id": "launch", "target": "point", "range": 60, "cooldown": 0.5,
                   "effects": [{"type": "projectile", "speed": 4, "effects": [{"type": "damage", "amount": 500, "radius": 2, "affects": "any"}]}]}]},
        "hq":   {"structure": {"footprint": [2, 2]}, "health": {"max": 2000}})") + (antiMissile ? R"(,
        "anti": {"structure": {"footprint": [2, 2]}, "health": {"max": 1000}, "intercept": {"radius": 8, "tags": ["missile"]},
                 "stockpile": {"max": 1, "time": 1}})" : ""));
    };
    std::string ents = R"(,"entities":[{"type":"silo","owner":1,"pos":[4,10]},{"type":"hq","owner":2,"pos":[40,10]})";
    for (bool anti : {false, true}) {
        const std::string map = flatMap(48, 24, ents + (anti ? R"(,{"type":"anti","owner":2,"pos":[36,10]}])" : "]"));
        auto sim = makeSimJson(rules(anti), map);
        Entity silo = findType(*sim, "silo", 1), hq = findType(*sim, "hq", 2);
        game::FieldOps* fo = mod(*sim).fieldOps();
        CHECK(!mod(*sim).canCast(*sim, silo, 0));    // no charge yet
        run(*sim, 2.5);
        CHECK(fo->stockpile(*sim, silo) == 1);
        CHECK(stockOf(*sim, 1, "metal") < Fixed::fromInt(1000));   // paid while building
        run(*sim, 3);
        CHECK(fo->stockpile(*sim, silo) == 2);       // capped at max
        sim->submit(mechCmd(1, game::kCmdAbility, {silo}, kNullEntity, at(40, 10), 0));
        run(*sim, 0.2);
        CHECK(fo->stockpile(*sim, silo) == 1);
        run(*sim, 12);
        CHECK((hpOf(*sim, hq) < Fixed::fromInt(2000)) == !anti);   // the interceptor stops the missile
        if (anti) CHECK(fo->stockpile(*sim, findType(*sim, "anti", 2)) <= 1);
        CHECK(mechRoundTrip(*sim, rules(anti), map));
    }
}

TEST(fieldops_ambient_income) {
    const std::string rules = foRules(R"(, "ambient": {"wind": {"min": 5, "max": 25, "every": [1, 2]}, "tidal": 10})", R"(
        "windgen": {"structure": {"footprint": [1, 1]}, "health": {"max": 100}, "ambientIncome": {"wind": {"energy": 1}}},
        "tidegen": {"structure": {"footprint": [1, 1]}, "health": {"max": 100}, "ambientIncome": {"tidal": {"energy": 2}}})");
    const std::string map = flatMap(24, 12, R"(,"entities":[{"type":"windgen","owner":1,"pos":[4,4]},{"type":"tidegen","owner":2,"pos":[8,4]}],
        "ambient": {"tidal": 15})");
    auto sim = makeSimJson(rules, map);
    const size_t en = size_t(mod(*sim).rules().findResource("energy"));
    std::vector<Fixed> seen;
    for (int i = 0; i < 10; ++i) {
        run(*sim, 1);
        const Fixed inc = mod(*sim).state(*sim).players[1].income[en];
        CHECK(inc >= Fixed::fromInt(5) && inc <= Fixed::fromInt(25));
        if (std::find(seen.begin(), seen.end(), inc) == seen.end()) seen.push_back(inc);
        CHECK(mod(*sim).state(*sim).players[2].income[en] == Fixed::fromInt(30));   // the map's tide over the rules one
    }
    CHECK(seen.size() >= 3);   // the wind changes
    CHECK(mechRoundTrip(*sim, rules, map));
}

TEST(fieldops_attack_runs_and_repair_pads) {
    const std::string rules = foRules("", R"(
        "bomber": {"move": {"speed": 4, "layer": "air"}, "health": {"max": 200}, "sight": 10, "attackRun": {"overshoot": 5}, "seekRepair": 0.5,
                   "weapons": [{"range": 1.5, "damage": 30, "cooldown": 0.5, "targets": ["ground"]}]},
        "pad":    {"structure": {"footprint": [2, 2]}, "health": {"max": 500}, "repairPad": {"heal": 50, "radius": 1.5}},
        "dummy":  {"move": {"speed": 0.01}, "health": {"max": 5000}, "sight": 1})");
    const std::string map = flatMap(48, 24, R"(,"entities":[{"type":"bomber","owner":1,"pos":[6,12]},{"type":"dummy","owner":2,"pos":[24,12]},{"type":"pad","owner":1,"pos":[4,4]}])");
    auto sim = makeSimJson(rules, map);
    Entity b = findType(*sim, "bomber", 1), d = findType(*sim, "dummy", 2);
    sim->submit(mechCmd(1, game::kCmdAttack, {b}, d));
    Fixed maxX{}, minXAfter = Fixed::fromInt(99);
    bool passed = false;
    for (int i = 0; i < 20 * 15; ++i) {
        sim->step();
        const Fixed x = posOf(*sim, b).x;
        maxX = fxMax(maxX, x);
        if (maxX > Fixed::fromInt(27)) { passed = true; minXAfter = fxMin(minXAfter, x); }
    }
    CHECK(passed);                                   // flew over and past the target
    CHECK(minXAfter < Fixed::fromInt(22));           // and came back for another pass
    CHECK(hpOf(*sim, d) < Fixed::fromInt(5000));
    CHECK(mechRoundTrip(*sim, rules, map));
    // a wounded idle bomber flies to the pad and is healed there
    sim->submit(mechCmd(1, game::kCmdStop, {b}));
    sim->world().get<game::Unit>(b).hp = Fixed::fromInt(40);
    sim->world().get<game::Unit>(d).hp = Fixed{};
    run(*sim, 20);
    CHECK(hpOf(*sim, b) == Fixed::fromInt(200));
}

TEST(fieldops_bad_data_errors) {
    auto threw = [](const std::string& rules, const char* what) {
        try { auto s = makeSimJson(rules, flatMap(8, 8, "")); } catch (const std::exception& e) { return std::string(e.what()).find(what) != std::string::npos; }
        return false;
    };
    CHECK(threw(foRules("", R"("r": {"radar": -1})"), "radar"));
    CHECK(threw(foRules("", R"("s": {"stockpile": {"max": 0}})"), "stockpile"));
    CHECK(threw(foRules("", R"("s": {"intercept": {"radius": 5, "tags": ["nope"]}})"), "nope"));
    CHECK(threw(foRules("", R"("s": {"ambientIncome": {"sun": {"energy": 1}}})"), "sun"));
    CHECK(threw(foRules("", R"("s": {"ambientIncome": {"wind": {"gold": 1}}})"), "gold"));
    CHECK(threw(foRules(R"(, "ambient": {"wind": {"min": 9, "max": 1}})", R"("s": {})"), "wind"));
}

// The sample game uses the mechanics and its bots play them: radar towers, geothermal plants on the vents, air
// plants and aircraft; a radar contact is tracked beyond sight; the match ends with a winner.
TEST(fieldops_metaltide_bots_use_the_mechanics) {
    const std::string dir = std::string(AIVERSE_TEMPLATES_DIR) + "/metaltide/";
    Json rulesJson = readComposedJson(dir + "rules.json", fileReader());
    Json map = readComposedJson(dir + "maps/twin_craters.json", fileReader());
    SimConfig cfg; cfg.seed = 2;
    Simulation sim(cfg, std::make_unique<game::GameCore>());
    sim.setup(rulesJson, map);
    CHECK(mod(sim).sensors() && mod(sim).fieldOps());
    std::vector<game::ComputerPlayer> bots;
    for (auto& p : sim.players()) bots.emplace_back(p.slot, 2);
    std::set<std::string> built;
    int blipTicks = 0;
    for (int t = 0; t < 20 * 1800 && sim.result() == 0; ++t) {
        for (auto& b : bots) b.update(sim);
        sim.step();
        for (auto& ev : sim.events())
            if (ev.type == game::kEvBuildDone || ev.type == game::kEvTrainDone) built.insert(mod(sim).rules().types[size_t(ev.value)].id);
        if (t % 200 == 0 && !mod(sim).sensors()->blips(sim, 1).empty()) ++blipTicks;
    }
    std::string list;
    for (auto& b : built) list += b + " ";
    std::printf("    built / trained: %s\n", list.c_str());
    CHECK(built.count("radar_tower") && built.count("geothermal"));
    CHECK(built.count("air_plant_arm") || built.count("air_plant_core"));
    CHECK(blipTicks > 0);
    CHECK(sim.result() != 0);
}
