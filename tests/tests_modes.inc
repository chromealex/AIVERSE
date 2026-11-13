// ------------------------------------------------------------------ match modes (agent-bw-modes, RtsModes.cpp)
// Bunker Wars ModsModule / BattleRoyal / SuddenDeath / CustomRules / MoraleInfo / PlayerStatistic as data.
namespace modes_t {
const char* kTypes = R"("types": {
    "village": {"structure": {"footprint": [2, 2]}, "invulnerable": true, "garrison": {"max": 30, "unit": "shroom", "interval": 100}},
    "tower": {"structure": {"footprint": [2, 2]}, "invulnerable": true, "garrison": {"max": 30, "unit": "shroom", "interval": 100}},
    "shroom": {"move": {"speed": 3}, "entersGarrison": true}})";
std::string rules(const std::string& extra) { return std::string("{") + extra + (extra.empty() ? "" : ",") + kTypes + "}"; }
std::string rows(int w, int h) {
    std::string s;
    for (int y = 0; y < h; ++y) s += std::string(y ? "," : "") + "\"" + std::string(size_t(w), '.') + "\"";
    return s;
}
// players: list of teams; entities: JSON list body
std::string map(const std::vector<int>& teams, const std::string& entities, const std::string& extra = "") {
    std::string pl;
    for (size_t i = 0; i < teams.size(); ++i) pl += std::string(i ? "," : "") + R"({"name":"P)" + std::to_string(i + 1) + R"(","team":)" + std::to_string(teams[i]) + "}";
    return R"({"size":[40,20],"players":[)" + pl + R"(],"terrain":{"rows":[)" + rows(40, 20) + R"(]},"entities":[)" + entities + "]" + extra + "}";
}
std::string ent(const char* type, int owner, int x, int z, const char* extra = "") {
    return std::string(R"({"type":")") + type + R"(","owner":)" + std::to_string(owner) + R"(,"pos":[)" + std::to_string(x) + "," + std::to_string(z) + "],\"garrison\":5" + extra + "}";
}
Entity at(Simulation& sim, int x, int z) {
    Entity found;
    sim.world().each<game::Unit, Transform>([&](Entity e, game::Unit&, Transform& t) {
        if (dist2DTest(t.pos, Vec3{Fixed::fromInt(x), Fixed{}, Fixed::fromInt(z)}) < Fixed::fromInt(2)) found = e;
    });
    return found;
}
bool alive(Simulation& sim, uint8_t p) { auto* pi = sim.player(p); return pi && pi->alive; }
} // namespace modes_t

TEST(modes_domination_ffa_hold_timer) {
    using namespace modes_t;
    auto sim = makeSimJson(rules(R"("modes": {"domination": {"hold": 1.5}})"),
        map({1, 2, 3}, ent("tower", 1, 10, 5, R"(,"modePoint":"domination")") + "," + ent("tower", 1, 20, 5, R"(,"modePoint":"domination")") + "," +
                       ent("village", 2, 30, 15) + "," + ent("village", 3, 5, 15) + "," + ent("village", 1, 15, 15)));
    auto& gs = mod(*sim).state(*sim);
    CHECK(gs.modes.points.size() == 2);
    run(*sim, 1.0);
    CHECK(alive(*sim, 2) && alive(*sim, 3));                  // timers still running (set on the first owner seen)
    run(*sim, 1.0);
    CHECK(!alive(*sim, 2) && !alive(*sim, 3) && alive(*sim, 1));   // FFA: every side holding no point is out
    CHECK(sim->result() == 1 && gs.modes.outcome[2] == 2);
}

TEST(modes_domination_coop_needs_all_points) {
    using namespace modes_t;
    const std::string r = rules(R"("modes": {"domination": {"hold": 1}})");
    // teams 1 (p1, p2) and 2 (p3, p4): team 1 holds both points with two players
    auto sim = makeSimJson(r, map({1, 1, 2, 2}, ent("tower", 1, 10, 5, R"(,"modePoint":"domination")") + "," + ent("tower", 2, 20, 5, R"(,"modePoint":"domination")") + "," +
                                                ent("village", 3, 30, 15) + "," + ent("village", 4, 5, 15)));
    run(*sim, 1.5);
    CHECK(!alive(*sim, 3) && !alive(*sim, 4) && sim->result() == 1);
    // one point each: nobody is out (holdAll)
    auto sim2 = makeSimJson(r, map({1, 1, 2, 2}, ent("tower", 1, 10, 5, R"(,"modePoint":"domination")") + "," + ent("tower", 3, 20, 5, R"(,"modePoint":"domination")") + "," +
                                                 ent("village", 4, 5, 15) + "," + ent("village", 2, 30, 15)));
    run(*sim2, 3);
    CHECK(alive(*sim2, 1) && alive(*sim2, 3) && sim2->result() == 0);
    // a neutral domination point blocks the check
    auto sim3 = makeSimJson(r, map({1, 2}, ent("tower", 1, 10, 5, R"(,"modePoint":"domination")") + "," + ent("tower", 0, 20, 5, R"(,"modePoint":"domination")") + "," +
                                           ent("village", 2, 30, 15)));
    run(*sim3, 3);
    CHECK(alive(*sim3, 2) && sim3->result() == 0);
}

TEST(modes_king_of_the_hill_countdown) {
    using namespace modes_t;
    auto sim = makeSimJson(rules(R"("modes": {"kingOfTheHill": {"score": 4, "rate": 2, "type": "tower", "garrison": 9}})"),
        map({1, 2}, ent("village", 1, 10, 5, R"(,"modePoint":"kingOfTheHill")") + "," + ent("village", 2, 30, 15)));
    auto& m = mod(*sim);
    auto& gs = m.state(*sim);
    Entity hill = at(*sim, 10, 5);
    CHECK(sim->world().get<game::Unit>(hill).type == uint16_t(m.rules().findType("tower")));   // became the mode type
    CHECK(sim->world().get<game::Garrison>(hill).count == 9);
    run(*sim, 1.0);
    CHECK(fxAbs(gs.modes.kohScore[1] - Fixed::fromInt(2)) < Fixed::fromRatio(1, 5) && gs.modes.kohScore[2] == Fixed::fromInt(4));
    CHECK(sim->result() == 0);
    run(*sim, 1.2);
    CHECK(sim->result() == 1 && !alive(*sim, 2) && gs.modes.kohScore[1].raw == 0);
}

TEST(modes_time_limit_speed_steps_and_points) {
    using namespace modes_t;
    const std::string r = rules(R"("modes": {"timeLimit": {"seconds": 3, "speed": [{"remaining": 2, "speed": 1.5}, {"remaining": 1, "speed": 2}]}})");
    auto sim = makeSimJson(r, map({1, 2}, ent("village", 1, 5, 5) + "," + ent("village", 1, 10, 5) + "," + ent("village", 2, 30, 15)));
    auto& m = mod(*sim);
    CHECK(m.speedScale(*sim) == 1.0);
    run(*sim, 1.1);
    CHECK(m.speedScale(*sim) == 1.5);
    run(*sim, 1.0);
    CHECK(m.speedScale(*sim) == 2.0 && sim->result() == 0);
    run(*sim, 1.0);
    CHECK(sim->result() == 1 && !alive(*sim, 2));             // most points wins when the time is up
    // equal points: a draw
    auto sim2 = makeSimJson(r, map({1, 2}, ent("village", 1, 5, 5) + "," + ent("village", 2, 30, 15)));
    run(*sim2, 3.2);
    CHECK(sim2->result() == -1);
    // points comparison: 2 vs 1 with coefficient 1.5 -> only team 1 exceeds the best (2 * 1.5 > 2, 1 * 1.5 < 2)
    auto sim3 = makeSimJson(rules(R"("modes": {"timeLimit": {"seconds": 1, "win": "compare", "coefficient": 1.5}})"),
                            map({1, 2}, ent("village", 1, 5, 5) + "," + ent("village", 1, 10, 5) + "," + ent("village", 2, 30, 15)));
    run(*sim3, 1.2);
    CHECK(sim3->result() == 1);
    // simulation advance(): the game speed runs more ticks per real second
    auto sim4 = makeSimJson(r, map({1, 2}, ent("village", 1, 5, 5) + "," + ent("village", 2, 30, 15)));
    run(*sim4, 2.05);
    const uint32_t t0 = sim4->tick();
    sim4->advance(0.5, 100);
    CHECK(sim4->tick() - t0 >= 19 && sim4->tick() - t0 <= 20);   // speed 2: 0.5 s real = 1 s of game
}

TEST(modes_sudden_death_phases) {
    using namespace modes_t;
    const std::string r = rules(R"("resources": {"mana": {"start": 0, "regen": 1}},
        "statuses": {"sd_boost": {"incomeMult": {"mana": 2}}},
        "modes": {"suddenDeath": {"at": 1, "preview": 1, "previewSpeed": 0.5, "end": 2, "counter": 0.5, "status": "sd_boost"}})");
    auto sim = makeSimJson(r, map({1, 2}, ent("village", 1, 5, 5) + "," + ent("village", 1, 10, 5) + "," + ent("village", 2, 30, 15)));
    auto& m = mod(*sim);
    auto& ms = m.state(*sim).modes;
    run(*sim, 0.6);
    CHECK(ms.sdPhase == 0 && ms.sdCounter);
    run(*sim, 0.5);
    CHECK(ms.sdPhase == 1 && m.speedScale(*sim) == 0.5);      // slowed preview
    run(*sim, 0.3);                                           // 0.3 s of game = 0.6 s real: still in the preview
    CHECK(ms.sdPhase == 1);
    run(*sim, 0.15);
    CHECK(ms.sdPhase == 2 && m.speedScale(*sim) == 1.0);
    const int boost = m.rules().findStatus("sd_boost");
    bool has = false;
    for (auto& in : m.state(*sim).players[2].statuses) has = has || in.def == boost;
    CHECK(has);
    const Fixed mana0 = stockOf(*sim, 1, "mana");
    run(*sim, 1.0);
    CHECK(stockOf(*sim, 1, "mana") - mana0 > Fixed::fromRatio(19, 10));   // factor 2 on income
    CHECK(sim->result() == 0);
    run(*sim, 1.1);
    CHECK(ms.sdPhase == 3 && sim->result() == 1 && m.state(*sim).modes.outcome[2] == 2);
}

TEST(modes_sudden_death_rule_sets_and_partial_tie) {
    using namespace modes_t;
    const std::string r = rules(R"("statuses": {"a": {}, "b": {}},
        "modes": {"suddenDeath": {"at": 0.5, "preview": 0, "end": 0.5, "ruleSets": [[{"status": "a", "when": [{"from": 0.2, "to": 0.4}]}], [{"status": "b"}]]}})");
    // three FFA players: 1 and 2 tie on two points, 3 has one -> 1 and 2 win, 3 draws: the match result is a draw
    auto sim = makeSimJson(r, map({1, 2, 3}, ent("village", 1, 5, 5) + "," + ent("village", 1, 10, 5) + "," + ent("village", 2, 30, 15) + "," +
                                             ent("village", 2, 30, 5) + "," + ent("village", 3, 20, 10)));
    auto& ms = mod(*sim).state(*sim).modes;
    CHECK(ms.sdRuleSet == 0 || ms.sdRuleSet == 1);
    run(*sim, 1.2);
    CHECK(sim->result() == -1 && ms.outcome[1] == 1 && ms.outcome[2] == 1 && ms.outcome[3] == 3);
}

TEST(modes_zones_shrink_and_neutralize) {
    using namespace modes_t;
    const std::string r = rules(R"("modes": {"zones": {"neutralType": "tower",
        "start": [{"rect": [0, 0, 20, 20], "stay": 1, "move": 2, "next": 0}, {"rect": [20, 0, 20, 20], "stay": 100}],
        "steps": [{"rect": [0, 0, 10, 20], "stay": 100}]}})");
    auto sim = makeSimJson(r, map({1, 2}, ent("village", 1, 5, 5) + "," + ent("village", 1, 13, 5) + "," + ent("village", 2, 30, 15)));
    auto& m = mod(*sim);
    game::ModeRect z;
    CHECK(m.modeZone(*sim, 1, z) && z.w == Fixed::fromInt(20));
    CHECK(m.modeZone(*sim, 2, z) && z.x == Fixed::fromInt(20));
    run(*sim, 2.0);                                           // halfway through the move: width 15
    CHECK(m.modeZone(*sim, 1, z) && fxAbs(z.w - Fixed::fromInt(15)) < Fixed::fromRatio(1, 10));
    CHECK(sim->world().get<Owner>(at(*sim, 13, 5)).player == 1);
    run(*sim, 1.2);                                           // the village at x = 13 is outside the zone now
    Entity v = at(*sim, 13, 5);
    CHECK(sim->world().get<Owner>(v).player == 0 && sim->world().get<game::Unit>(v).type == uint16_t(m.rules().findType("tower")));
    CHECK(sim->world().get<Owner>(at(*sim, 5, 5)).player == 1 && sim->world().get<Owner>(at(*sim, 30, 15)).player == 2);
}

TEST(modes_timed_rules_and_levels) {
    using namespace modes_t;
    const std::string r = rules(R"("resources": {"morale": {"start": 0, "min": 0, "levels": [
            {"at": 0}, {"at": 100, "mods": [{"stat": "damage", "mul": 2}], "decay": {"every": 0.5, "amount": 10, "after": 1}}]}},
        "statuses": {"rush": {"mods": [{"stat": "speed", "mul": 2}]}},
        "timedRules": [{"status": "rush", "when": [{"from": 1, "to": 2}]}, {"status": "rush", "resource": "morale", "when": [{"levelBehind": 1}]}])");
    auto sim = makeSimJson(r, map({1, 2}, ent("village", 1, 5, 5) + "," + ent("village", 2, 30, 15) +
                                          R"(,{"type":"shroom","owner":1,"pos":[6,9]},{"type":"shroom","owner":2,"pos":[30,9]})"));
    auto& m = mod(*sim);
    auto& gs = m.state(*sim);
    const int rush = m.rules().findStatus("rush");
    auto has = [&](uint8_t p) { for (auto& in : gs.players[p].statuses) if (in.def == rush && in.until > sim->tick()) return true; return false; };
    Entity s1 = at(*sim, 6, 9);
    run(*sim, 0.5);
    CHECK(!has(1) && m.stat(*sim, s1, game::Stat::Damage) == m.stat(*sim, at(*sim, 30, 9), game::Stat::Damage));
    m.addResource(*sim, 1, uint16_t(m.rules().findResource("morale")), Fixed::fromInt(120));
    run(*sim, 0.1);
    CHECK(m.modeLevel(*sim, 1, 0) == 1 && m.stat(*sim, s1, game::Stat::Damage) == m.stat(*sim, at(*sim, 30, 9), game::Stat::Damage) * Fixed::fromInt(2));
    CHECK(has(2) && !has(1));                                  // player 2 is one level behind
    run(*sim, 0.8);
    CHECK(has(1));                                            // time slot [1, 2]
    CHECK(stockOf(*sim, 1, "morale") == Fixed::fromInt(120)); // quiet for less than `after`
    run(*sim, 1.0);
    CHECK(stockOf(*sim, 1, "morale") < Fixed::fromInt(120));  // decays after the quiet period
    run(*sim, 1.5);
    CHECK(!has(1) && m.modeLevel(*sim, 1, 0) == 0 && !has(2));   // out of the slot, back on level 0, nobody behind
    CHECK(m.matchStat(*sim, 1, game::kMsLevelUps) == 1 && m.matchStat(*sim, 1, game::kMsLevelDowns) == 1 && m.matchStat(*sim, 1, game::kMsMaxLevel) == 1);
}

TEST(modes_presets_stats_save_and_errors) {
    using namespace modes_t;
    const std::string r = rules(R"("lanes": {"left": 2, "right": 1, "spacing": 0.4}, "matchModes": {"koh": {"modes": {"kingOfTheHill": {"score": 30}}, "victory": "noEntities"}, "plain": {}})");
    auto sim = makeSimJson(r, map({1, 2}, ent("village", 1, 10, 5, R"(,"modePoint":"kingOfTheHill")") + "," + ent("village", 2, 30, 15) + "," +
                                          std::string(R"({"type":"village","owner":0,"pos":[20,5],"garrison":0})"), R"(,"matchMode":"koh")"));
    auto& m = mod(*sim);
    CHECK(m.modes().koh && m.modes().stats && m.state(*sim).victory.mode == game::VictoryMode::NoEntities);
    auto simB = makeSimJson(r, map({1, 2}, ent("village", 1, 10, 5) + "," + ent("village", 2, 30, 15), R"(,"matchMode":"plain")"));
    CHECK(!mod(*simB).modes().koh);
    // statistics: a capture of a neutral village
    Command c; c.player = 1; c.type = game::kCmdSend; c.units = {at(*sim, 10, 5)}; c.target = at(*sim, 20, 5); c.param = 100;
    sim->submit(c);
    run(*sim, 1.0);
    // save / load mid-match: the copy goes on identically
    const std::string save = sim->serialize();
    auto copy = makeSimJson(r, map({1, 2}, ent("village", 1, 10, 5, R"(,"modePoint":"kingOfTheHill")") + "," + ent("village", 2, 30, 15) + "," +
                                           std::string(R"({"type":"village","owner":0,"pos":[20,5],"garrison":0})"), R"(,"matchMode":"koh")"));
    CHECK(copy->deserialize(save));
    CHECK(copy->checksum() == sim->checksum());
    run(*sim, 4.0); run(*copy, 4.0);
    CHECK(copy->checksum() == sim->checksum());
    CHECK(m.matchStat(*sim, 1, game::kMsCapturedNeutral) == 1 && m.matchStat(*sim, 1, game::kMsPoints) == 2);
    CHECK(sim->result() == 0 && m.state(*sim).modes.kohScore[1] < Fixed::fromInt(26));
    // readable errors
    auto err = [&](const std::string& extra) {
        try { makeSimJson(rules(extra), map({1, 2}, ent("village", 1, 5, 5))); } catch (const std::exception& e) { return std::string(e.what()); }
        return std::string();
    };
    CHECK(err(R"("modes": {"suddenDeath": {"status": "nope"}})").find("unknown status 'nope'") != std::string::npos);
    CHECK(err(R"("modes": {"zones": {"start": []}})").find("modes.zones") != std::string::npos);
    CHECK(err(R"("timedRules": [{"status": "x"}])").find("timedRules[0]") != std::string::npos);
    CHECK(err(R"("modes": {"domination": {"rule": "x"}})").find("rule must be") != std::string::npos);
}

TEST(modes_deterministic_match) {
    using namespace modes_t;
    const std::string r = rules(R"("modes": {"kingOfTheHill": {"score": 30}, "timeLimit": {"seconds": 20, "speed": [{"remaining": 10, "speed": 2}]},
        "suddenDeath": {"at": 5, "preview": 1, "end": 30}, "zones": {"start": [{"rect": [0, 0, 40, 20], "stay": 3, "move": 4, "next": 0}], "steps": [{"rect": [5, 2, 30, 16], "stay": 99}]}})");
    const std::string mp = map({1, 2}, ent("village", 1, 5, 5) + "," + ent("village", 2, 34, 15) + "," + ent("village", 0, 20, 10, R"(,"modePoint":"kingOfTheHill")"));
    auto a = makeSimJson(r, mp, 7), b = makeSimJson(r, mp, 7);
    for (int t = 0; t < 20 * 25; ++t) {
        if (t % 40 == 3)
            for (auto* s : {a.get(), b.get()}) {
                Command c; c.player = uint8_t(1 + (t / 40) % 2); c.type = game::kCmdSend;
                c.units = {at(*s, c.player == 1 ? 5 : 34, c.player == 1 ? 5 : 15)}; c.target = at(*s, 20, 10); c.param = 60;
                s->submit(c);
            }
        a->step(); b->step();
    }
    CHECK(a->checksum() == b->checksum() && a->result() != 0);
}

// ------------------------------------------------------------------ battle core (RtsCore.cpp): morale points, energy, production
namespace core_t {
std::string rules(const std::string& extra) {
    return std::string(R"({"lanes": {"left": 0, "right": 0},
      "resources": {"morale": {"start": 0, "min": 0, "capped": true, "storage": 8000, "levels": [
          {"at": 0, "decay": {"every": 1, "amount": 10, "after": 10}},
          {"at": 500, "mods": [{"tags": ["soldier"], "stat": "speed", "mul": 1.1}], "decay": {"every": 1, "amount": 20, "after": 9}},
          {"at": 1000}, {"at": 2000}, {"at": 4000}, {"at": 8000}]},
        "energy": {"start": 0, "capped": true, "storage": 500}},
      "battleCore": {"morale": {"resource": "morale", "unitPoints": 10, "towerPoints": 10},
                     "energy": {"resource": "energy", "perUnit": 0.45},
                     "production": {}, "neutralGrowth": true)") + extra + R"(},
      "types": {
        "house": {"structure": {"footprint": [2, 2]}, "invulnerable": true, "garrison": {"max": 20, "unit": "soldier", "interval": 1},
                  "moralePoints": {"catchNeutral": 100, "catchEnemy": 200, "lost": 50, "upgrade": 70}},
        "soldier": {"tags": ["soldier"], "move": {"speed": 3}, "entersGarrison": true}}})";
}
} // namespace core_t

TEST(core_morale_points_energy_and_levels) {
    using namespace modes_t;
    auto sim = makeSimJson(core_t::rules(""),
        map({1, 2}, std::string(R"({"type":"house","owner":1,"pos":[5,5],"garrison":20},{"type":"house","owner":0,"pos":[12,5],"garrison":3,"values":{"easy":{"neutralMax":3}}},)") +
                    R"({"type":"house","owner":2,"pos":[30,15],"garrison":20})"));
    auto& m = mod(*sim);
    Command c; c.player = 1; c.type = game::kCmdSend; c.units = {at(*sim, 5, 5)}; c.target = at(*sim, 12, 5); c.param = -10;   // exactly 10 units
    sim->submit(c);
    run(*sim, 4);
    // 3 assaulting units lost against 3 defenders: attacker -10 each (floor 0), energy 0.45 x (6 - 0 - 1) x 3; the capture +100
    CHECK(sim->world().get<Owner>(at(*sim, 12, 5)).player == 1);
    CHECK(stockOf(*sim, 1, "morale") == Fixed::fromInt(100));
    CHECK(fxAbs(stockOf(*sim, 1, "energy") - Fixed::fromRatio(675, 100)) < Fixed::fromRatio(1, 100));
    CHECK(m.modeLevel(*sim, 1, 0) == 0);
    // level 1 (500): energy per lost unit drops to x4
    m.addResource(*sim, 1, uint16_t(m.rules().findResource("morale")), Fixed::fromInt(450));
    run(*sim, 0.1);
    CHECK(m.modeLevel(*sim, 1, 0) == 1);
}

TEST(core_production_limit_and_neutral_growth) {
    using namespace modes_t;
    // player 1: one house (cap 20) holding 12 units = 60 %: rate 0.75 -> 0.75 units per second
    auto sim = makeSimJson(core_t::rules(""),
        map({1, 2}, std::string(R"({"type":"house","owner":1,"pos":[5,5],"garrison":12},{"type":"house","owner":0,"pos":[12,5],"garrison":2,)") +
                    R"("values":{"easy":{"garrison":2,"neutralMax":5},"hard":{"garrison":4,"neutralMax":9}}},{"type":"house","owner":2,"pos":[30,15],"garrison":2})"));
    auto& w = sim->world();
    run(*sim, 4.05);
    CHECK(w.get<game::Garrison>(at(*sim, 5, 5)).count == 15);   // 12 + 4 x 0.75
    CHECK(w.get<game::Garrison>(at(*sim, 30, 15)).count == 6);  // 2 + 4 (10 % of the cap: full rate)
    CHECK(w.get<game::Garrison>(at(*sim, 12, 5)).count == 5);   // neutral: up to its maximum
    run(*sim, 5.9);
    CHECK(w.get<game::Garrison>(at(*sim, 5, 5)).count == 17);   // >= 75 %: rate 0.5
    run(*sim, 10);
    CHECK(w.get<game::Garrison>(at(*sim, 5, 5)).count == 20);   // at the cap: rate 0
    // difficulty picks the values
    auto sim2 = makeSimJson(core_t::rules(""),
        map({1, 2}, std::string(R"({"type":"house","owner":1,"pos":[5,5],"garrison":1},{"type":"house","owner":0,"pos":[12,5],"garrison":2,)") +
                    R"("values":{"easy":{"garrison":2,"neutralMax":5},"hard":{"garrison":4,"neutralMax":9}}},{"type":"house","owner":2,"pos":[30,15],"garrison":2})", R"(,"difficulty":"hard")"));
    CHECK(sim2->world().get<game::Garrison>(at(*sim2, 12, 5)).count == 4);
    run(*sim2, 8);
    CHECK(sim2->world().get<game::Garrison>(at(*sim2, 12, 5)).count == 9);
}

TEST(core_koth_vars_and_checksums_unchanged_without_core) {
    using namespace modes_t;
    auto sim = makeSimJson(rules(R"("modes": {"kingOfTheHill": {"score": 10}})"),
        map({1, 2}, ent("village", 1, 10, 5, R"(,"modePoint":"kingOfTheHill")") + "," + ent("village", 2, 30, 15)));
    run(*sim, 1.0);
    auto& vars = mod(*sim).state(*sim).trig.vars;
    CHECK(vars.count("koth_1") && vars.count("koth_2") && vars.count("koth_max"));
    CHECK(vars["koth_2"] == Fixed::fromInt(10) && vars["koth_1"] < Fixed::fromInt(10) && vars["koth_max"] == Fixed::fromInt(10));
    CHECK(!mod(*sim).rules().core.any);
}

TEST(modes_team_conquest_rule_sets_and_hud_vars) {
    using namespace modes_t;
    // team conquest: player 2 has nothing, but its teammate 1 still has a village -> both stay
    const std::string r = rules(R"("victory": "noEntities", "statuses": {"rush": {}}, "ruleSets": {"league3": [{"status": "rush"}]},
        "modes": {"conquest": {"team": true}, "timeLimit": {"seconds": 50}, "suddenDeath": {"at": 10, "preview": 2, "end": 5}})");
    auto sim = makeSimJson(r, map({1, 1, 2}, ent("village", 1, 5, 5) + "," + ent("village", 3, 30, 15), R"(,"ruleSets":["league3"])"));
    run(*sim, 1.0);
    CHECK(alive(*sim, 1) && alive(*sim, 2) && alive(*sim, 3));
    auto& m = mod(*sim);
    const int rush = m.rules().findStatus("rush");
    bool has = false;
    for (auto& in : m.state(*sim).players[3].statuses) has = has || in.def == rush;
    CHECK(has);
    auto& V = m.state(*sim).trig.vars;
    CHECK(fxAbs(V["mode_time_left"] - Fixed::fromInt(49)) < Fixed::fromRatio(1, 10));
    CHECK(V["mode_sd_phase"].raw == 0 && fxAbs(V["mode_sd_left"] - Fixed::fromInt(9)) < Fixed::fromRatio(1, 10));
    // without team conquest player 2 is out
    auto sim2 = makeSimJson(rules(R"("victory": "noEntities")"), map({1, 1, 2}, ent("village", 1, 5, 5) + "," + ent("village", 3, 30, 15)));
    run(*sim2, 1.0);
    CHECK(alive(*sim2, 1) && !alive(*sim2, 2));
    // unknown rule set: a readable error
    bool threw = false;
    try { makeSimJson(r, map({1, 2}, ent("village", 1, 5, 5), R"(,"ruleSets":["nope"])")); } catch (const std::exception& e) { threw = std::string(e.what()).find("nope") != std::string::npos; }
    CHECK(threw);
}

TEST(core_player_morale_gain_multiplier) {
    using namespace modes_t;
    auto mp = [](const char* gain) {
        return std::string(R"({"size":[40,20],"players":[{"name":"A","team":1,"moraleGain":)") + gain + R"(},{"name":"B","team":2}],"terrain":{"rows":[)" + rows(40, 20) +
               R"(]},"entities":[{"type":"house","owner":1,"pos":[5,5],"garrison":20},{"type":"house","owner":0,"pos":[12,5],"garrison":0,"values":{"easy":{"neutralMax":0}}},{"type":"house","owner":2,"pos":[30,15],"garrison":5}]})";
    };
    for (const char* g : {"1", "1.5"}) {
        auto sim = makeSimJson(core_t::rules(""), mp(g));
        Command c; c.player = 1; c.type = game::kCmdSend; c.units = {at(*sim, 5, 5)}; c.target = at(*sim, 12, 5); c.param = -1;
        sim->submit(c);
        run(*sim, 4);
        CHECK(stockOf(*sim, 1, "morale") == Fixed::fromInt(std::string(g) == "1" ? 100 : 150));   // catchNeutral 100 x gain
    }
}
