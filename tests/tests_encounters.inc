// Boss encounters (rules "encounters", RtsEncounters.h over the boss-encounters library): parsing and errors,
// boss phases / patterns / adds / enrage on real units, telegraph strikes -> damage + rules statuses + immunity,
// ability-bound attacks, mob wander / hunt / flee orders, the personal boss schedule, determinism, saves, rollback,
// games without the block untouched, and the Golem Pit sample to a result.
#include "modules/rts/RtsEncounters.h"
#include "modules/rts/RtsAI.h"

namespace {
const char* kEncRules = R"J({
  "victory": "noUnits",
  "resources": {"gold": {"start": 0}},
  "statuses": {
    "chilled": {"mods": [{"stat": "speed", "mul": 0.5}]},
    "stunned": {"flags": ["stun"]},
    "burning": {"tick": {"damage": 5, "interval": 0.5}, "stacking": "independent", "maxStacks": 3}
  },
  "encounters": {
    "attacks": {
      "swipe": {"shape": "cone", "radius": 3, "angle": 120, "windup": 0.5, "recovery": 0.3, "cooldown": 1, "damage": 40, "maxRange": 3},
      "slam":  {"shape": "circle", "anchor": "aim", "radius": 2, "maxRange": 8, "windup": 0.6, "recovery": 0.3, "cooldown": 2,
                "damage": 50, "statuses": [{"status": "stunned", "duration": 0.5}, {"status": "burning", "duration": 2}]},
      "wave":  {"shape": "ring", "radius": 6, "inner": 3, "windup": 0.8, "cooldown": 4, "damage": 30, "statuses": ["chilled"]},
      "bite":  {"shape": "cone", "radius": 1.2, "angle": 90, "windup": 0.3, "recovery": 0.2, "cooldown": 1, "damage": 6},
      "zap":   {"shape": "circle", "anchor": "aim", "radius": 1.5, "maxRange": 9, "windup": 0.2, "recovery": 0.1, "damage": 25, "ability": "zap"}
    },
    "bosses": {
      "golem": {
        "engageRange": 12, "loseRange": 40, "approach": 2, "immune": ["stunned"],
        "phases": [
          {"pattern": [{"attack": "swipe", "maxRange": 3}, {"attack": "slam", "maxRange": 8}]},
          {"hpBelow": 0.6, "transition": 0.5, "damage": 1.5, "speed": 1.2,
           "pattern": [{"attack": "wave", "maxRange": 6}, {"attack": "slam", "maxRange": 8}],
           "adds": [{"unit": "crawler", "count": 3, "every": 5, "maxAlive": 4, "minRadius": 2, "maxRadius": 3}]},
          {"hpBelow": 0.3, "pattern": ["slam"]}
        ],
        "enrage": {"after": 60, "damage": 2}
      }
    },
    "mobs": {
      "crawler": {"aggroRange": 7, "loseRange": 10, "stopDistance": 1, "wanderMin": 1, "wanderMax": 3, "wanderDelay": 0.5,
                  "fleeBelow": 0.3, "fleeRecover": 0.6, "fleeRange": 6, "fleeTime": 2, "attack": "bite", "think": 0.1}
    }
  },
  "types": {
    "hero":    {"health": {"max": 100000}, "move": {"speed": 4, "radius": 0.45}, "sight": 12,
                "weapons": [{"range": 5, "damage": 1, "cooldown": 1}],
                "abilities": [{"id": "zap", "target": "point", "range": 9, "cooldown": 3, "autocast": false, "effects": []}]},
    "mage":    {"health": {"max": 100000}, "move": {"speed": 3, "radius": 0.4}, "sight": 10,
                "abilities": [{"id": "zap", "target": "point", "range": 9, "cooldown": 3, "autocast": false, "effects": []}]},
    "dummy":   {"health": {"max": 100000}, "move": {"speed": 0, "radius": 0.4}, "sight": 2},
    "golem":   {"health": {"max": 2000}, "move": {"speed": 2, "radius": 1}, "sight": 14},
    "crawler": {"health": {"max": 80}, "move": {"speed": 3, "radius": 0.35}, "sight": 8}
  }
})J";

std::string encMap(const std::string& entities, bool monsters = true) {
    std::string rows;
    for (int y = 0; y < 24; ++y) rows += std::string(y ? "," : "") + "\"" + std::string(40, '.') + "\"";
    return std::string(R"({"size":[40,24],"players":[{"name":"Hero","team":1},{"name":"Monsters","team":2)") + (monsters ? R"(,"monsters":true)" : "") +
           R"(}],"terrain":{"rows":[)" + rows + "]},\"entities\":[" + entities + "]}";
}
Fixed encDist(Vec3 a, Vec3 b) { return (a - b).xz().length(); }
game::EncounterStore& encStore(Simulation& sim) { return *mod(sim).encounters()->store(sim); }
int countEvents(Simulation& sim, double seconds, uint16_t type, std::vector<SimEvent>* out = nullptr) {
    int n = 0;
    for (int i = 0; i < int(seconds * sim.tickRate()); ++i) {
        sim.step();
        for (auto& e : sim.events()) if (e.type == type) { ++n; if (out) out->push_back(e); }
    }
    return n;
}
bool hasStatus(Simulation& sim, Entity e, const char* id) {
    const int s = mod(sim).rules().findStatus(id);
    auto* st = sim.world().tryGet<game::Statuses>(e);
    if (!st) return false;
    for (auto& in : st->list) if (in.def == s) return true;
    return false;
}
} // namespace

TEST(encounters_rules_parse_and_errors) {
    auto sim = makeSimJson(kEncRules, encMap(R"({"type":"hero","owner":1,"pos":[5.5,5.5]})"));
    auto* en = mod(*sim).encounters();
    CHECK(en && en->active() && en->store(*sim));
    const auto& R = en->rules();
    CHECK(R.attacks.size() == 5 && R.bosses.size() == 1 && R.mobs.size() == 1 && R.speedMods);
    const auto& slam = R.telegraphs.at(R.findAttack("slam") + 1);
    CHECK(slam.anchor == game::lg::AreaAnchor::Aim && slam.windup == 12 && slam.damage == 50000 && slam.statuses.size() == 2);
    const auto& cone = R.telegraphs.at(R.findAttack("swipe") + 1);
    CHECK(cone.area.halfAngle == Fixed::fromInt(120) / 720);
    const auto& golem = R.bossSet.begin()->second;
    CHECK(golem.phases.size() == 3 && golem.phases[1].hpBelowMilli == 600 && golem.phases[1].damageMilli == 1500 && golem.enrage.afterTicks == 1200);
    CHECK(en->monsterPlayer(2) && !en->monsterPlayer(1));
    CHECK(en->statusKind(mod(*sim).rules().findStatus("stunned")) == game::EncStatusKind::Stun);
    CHECK(en->statusKind(mod(*sim).rules().findStatus("chilled")) == game::EncStatusKind::Slow);
    CHECK(en->statusKind(mod(*sim).rules().findStatus("burning")) == game::EncStatusKind::Burn);
    auto throws = [&](const std::string& from, const std::string& to) {
        std::string r = kEncRules;
        size_t at = r.find(from);
        if (at == std::string::npos) return false;
        r.replace(at, from.size(), to);
        try { makeSimJson(r, encMap("")); return false; } catch (const std::exception& e) { return std::string(e.what()).find("encounters") != std::string::npos; }
    };
    CHECK(throws(R"("golem": {
        "engageRange")", R"("ghost": {
        "engageRange")"));                                                           // unknown unit type
    CHECK(throws(R"("pattern": ["slam"])", R"("pattern": ["kick"])"));               // unknown attack
    CHECK(throws(R"("statuses": ["chilled"])", R"("statuses": ["frozen"])"));        // unknown status
    CHECK(throws(R"("shape": "ring")", R"("shape": "square")"));                     // shape
    CHECK(throws(R"("damage": 40, "maxRange": 3})", R"("damage": 40, "maxRange": 3, "sweep": true})"));   // sweep: lines only
    CHECK(throws(R"({"hpBelow": 0.3, "pattern")", R"({"pattern")"));                 // later phases need a trigger
    CHECK(throws(R"("ability": "zap")", R"("ability": "nope")"));                    // no unit has that ability
    // no block: nothing
    auto plain = makeSimJson(R"({"types": {"u": {"health": {"max": 10}, "move": {"speed": 1}}}})", encMap(R"({"type":"u","owner":1,"pos":[3.5,3.5]})"));
    CHECK(mod(*plain).encounters() && !mod(*plain).encounters()->active() && !mod(*plain).encounters()->store(*plain));
    run(*plain, 0.5);
}

TEST(encounters_boss_phases_attacks_adds_and_defeat) {
    auto sim = makeSimJson(kEncRules, encMap(R"({"type":"dummy","owner":1,"pos":[20.5,6.5]},{"type":"golem","owner":2,"pos":[20.5,14.5]})"));
    const Entity dummy = findType(*sim, "dummy", 1), golem = findType(*sim, "golem", 2);
    CHECK(dummy.valid() && golem.valid());
    std::vector<SimEvent> started;
    CHECK(countEvents(*sim, 0.2, game::kEvBossEngaged) == 1);
    const game::EncBossState* b = encStore(*sim).boss(golem.packed());
    CHECK(b && b->engaged && b->target == dummy.packed() && b->brain.phase == 0);
    // phase 1: walks in, telegraphs slams / swipes that hit the dummy (8 tiles apart: slam range first)
    CHECK(countEvents(*sim, 6, game::kEvTelegraphStart, &started) >= 2);
    CHECK(sim->world().get<game::Unit>(dummy).hp < Fixed::fromInt(100000));
    CHECK(sim->world().get<game::Unit>(golem).hp == Fixed::fromInt(2000));
    // phase 2 by hp: event, multipliers (speed through the stat hook), add waves capped by maxAlive
    sim->world().get<game::Unit>(golem).hp = Fixed::fromInt(1100);
    std::vector<SimEvent> phases;
    countEvents(*sim, 0.2, game::kEvBossPhase, &phases);
    CHECK(phases.size() == 1 && phases[0].value == 1 && phases[0].a == golem);
    b = encStore(*sim).boss(golem.packed());
    CHECK(b->brain.phase == 1 && b->damageMilli == 1500 && b->speedMilli == 1200);
    CHECK(mod(*sim).stat(*sim, golem, game::Stat::Speed) == Fixed::fromRatio(24, 10));
    run(*sim, 0.6);
    CHECK(countType(*sim, "crawler", 2) == 3 && encStore(*sim).adds.size() == 3 && encStore(*sim).mobs.size() == 3);
    run(*sim, 5.2);
    CHECK(countType(*sim, "crawler", 2) == 4);   // next wave: 1 more (maxAlive 4)
    // phase 3, then death: defeated event, brain stops
    sim->world().get<game::Unit>(golem).hp = Fixed::fromInt(500);
    phases.clear();
    countEvents(*sim, 0.2, game::kEvBossPhase, &phases);
    CHECK(phases.size() == 1 && phases[0].value == 2);
    sim->world().get<game::Unit>(golem).hp = Fixed{};
    CHECK(countEvents(*sim, 0.2, game::kEvBossDefeated) == 1);
    CHECK(!encStore(*sim).boss(golem.packed()));
}

TEST(encounters_telegraph_damage_statuses_immunity_and_abilities) {
    auto sim = makeSimJson(kEncRules, encMap(R"({"type":"mage","owner":1,"pos":[10.5,10.5]},{"type":"dummy","owner":2,"pos":[14.5,10.5]})", false));
    auto* en = mod(*sim).encounters();
    const Entity hero = findType(*sim, "mage", 1), target = findType(*sim, "dummy", 2);
    const Vec3 tp = sim->world().get<Transform>(target).pos;
    // a slam cast by hand: telegraph start now, strike after the 0.6 s windup with damage and both statuses
    CHECK(en->cast(*sim, hero, en->rules().findAttack("slam"), tp, target));
    CHECK(!en->cast(*sim, hero, 99, tp, target));
    std::vector<SimEvent> strikes;
    CHECK(countEvents(*sim, 0.5, game::kEvTelegraphStrike, &strikes) == 0);
    CHECK(sim->world().get<game::Unit>(target).hp == Fixed::fromInt(100000));
    countEvents(*sim, 0.2, game::kEvTelegraphStrike, &strikes);
    CHECK(strikes.size() == 1 && strikes[0].b == target && (strikes[0].value >> 16) == 50);
    CHECK(hasStatus(*sim, target, "stunned") && hasStatus(*sim, target, "burning"));
    CHECK(mod(*sim).isStunned(*sim, target));
    CHECK(sim->world().get<game::Unit>(hero).hp == Fixed::fromInt(100000));   // never the caster
    run(*sim, 2.5);
    CHECK(!hasStatus(*sim, target, "stunned") && sim->world().get<game::Unit>(target).hp <= Fixed::fromInt(100000 - 50 - 15));   // burn ticks
    // an ability whose id an attack names starts it (cooldown / effects stay the ability's)
    const uint32_t before = uint32_t(encStore(*sim).nextCast);
    mod(*sim).castAbility(*sim, hero, 0, kNullEntity, tp);
    std::vector<SimEvent> st;
    countEvents(*sim, 0.1, game::kEvTelegraphStart, &st);
    CHECK(uint32_t(encStore(*sim).nextCast) == before + 1 && st.size() == 1 && st[0].a == hero && st[0].value == en->rules().findAttack("zap"));
}

TEST(encounters_boss_immunity_on_a_monster_boss) {
    auto sim = makeSimJson(kEncRules, encMap(R"({"type":"mage","owner":1,"pos":[20.5,10.5]},{"type":"golem","owner":2,"pos":[20.5,16.5]})"));
    auto* en = mod(*sim).encounters();
    const Entity hero = findType(*sim, "mage", 1), golem = findType(*sim, "golem", 2);
    run(*sim, 0.1);
    CHECK(encStore(*sim).boss(golem.packed()));
    CHECK(en->cast(*sim, hero, en->rules().findAttack("slam"), sim->world().get<Transform>(golem).pos, golem));
    run(*sim, 0.8);
    CHECK(!hasStatus(*sim, golem, "stunned") && hasStatus(*sim, golem, "burning"));
}

TEST(encounters_mobs_wander_hunt_flee) {
    auto sim = makeSimJson(kEncRules, encMap(R"({"type":"dummy","owner":1,"pos":[37.5,21.5]},{"type":"crawler","owner":2,"pos":[6.5,6.5]})"));
    const Entity crawler = findType(*sim, "crawler", 2), dummy = findType(*sim, "dummy", 1);
    const Vec3 home = sim->world().get<Transform>(crawler).pos;
    run(*sim, 3);
    const game::EncMobState* ms = encStore(*sim).mob(crawler.packed());
    CHECK(ms && (ms->brain.mode == game::lg::MobMode::Wander || ms->brain.mode == game::lg::MobMode::Idle));
    Fixed moved{};
    for (int i = 0; i < 40; ++i) { run(*sim, 0.25); moved = fxMax(moved, encDist(sim->world().get<Transform>(crawler).pos, home)); }
    CHECK(moved > Fixed::fromRatio(1, 2) && moved < Fixed::fromInt(5));   // wanders around home, inside the ring (+ arrival slack)
    // a hostile close by: hunt, walk in, bite
    const Vec3 cp = sim->world().get<Transform>(crawler).pos;
    sim->world().get<Transform>(dummy).pos = {cp.x + Fixed::fromInt(4), Fixed{}, cp.z};
    std::vector<SimEvent> bites;
    countEvents(*sim, 4, game::kEvTelegraphStrike, &bites);
    ms = encStore(*sim).mob(crawler.packed());
    CHECK(ms && ms->brain.mode == game::lg::MobMode::Hunt && ms->brain.target == int64_t(dummy.packed()) + 1);
    CHECK(!bites.empty() && bites[0].b == dummy);
    // low hp: flee away from the threat
    sim->world().get<game::Unit>(crawler).hp = Fixed::fromInt(10);
    const Fixed d0 = encDist(sim->world().get<Transform>(crawler).pos, sim->world().get<Transform>(dummy).pos);
    run(*sim, 1.0);
    ms = encStore(*sim).mob(crawler.packed());
    CHECK(ms && ms->brain.mode == game::lg::MobMode::Flee);
    CHECK(encDist(sim->world().get<Transform>(crawler).pos, sim->world().get<Transform>(dummy).pos) > d0);
    // monsters only: the same crawler of a normal player gets no brain
    auto plain = makeSimJson(kEncRules, encMap(R"({"type":"crawler","owner":2,"pos":[6.5,6.5]})", false));
    run(*plain, 1);
    CHECK(encStore(*plain).mobs.empty());
}

TEST(encounters_schedule_personal_boss_and_wave_gate) {
    std::string rules = kEncRules;
    const std::string mobs = R"("mobs": {)";
    rules.replace(rules.find(mobs), mobs.size(), R"("schedule": {"personal": true, "spawnDistance": [6, 8],
        "windows": [{"start": 1, "end": 30, "breather": 5, "variants": ["golem"]}]},
    "mobs": {)");
    auto sim = makeSimJson(rules, encMap(R"({"type":"hero","owner":1,"pos":[20.5,12.5]},{"type":"crawler","owner":2,"pos":[2.5,2.5]})"));
    auto* en = mod(*sim).encounters();
    CHECK(en->wavesAllowed(*sim, 1));
    std::vector<SimEvent> win;
    countEvents(*sim, 1.2, game::kEvBossWindow, &win);
    CHECK(win.size() == 2 && (win[0].value >> 16) == 0 && (win[1].value >> 16) == 1 && win[1].player == 1);
    const Entity boss = win[1].a;
    CHECK(sim->world().alive(boss) && sim->world().get<Owner>(boss).player == 2);
    const Fixed d = encDist(sim->world().get<Transform>(boss).pos, sim->world().get<Transform>(findType(*sim, "hero", 1)).pos);
    CHECK(d >= Fixed::fromInt(5) && d <= Fixed::fromInt(9));
    const game::EncBossState* b = encStore(*sim).boss(boss.packed());
    CHECK(b && b->participant == 1 && b->window == 1);
    CHECK(!en->wavesAllowed(*sim, 1));   // pauseWaves while the personal boss lives
    sim->world().get<game::Unit>(boss).hp = Fixed{};
    CHECK(countEvents(*sim, 0.2, game::kEvBossDefeated) == 1);
    CHECK(!en->wavesAllowed(*sim, 1));   // breather
    run(*sim, 5);
    CHECK(en->wavesAllowed(*sim, 1));
}

TEST(encounters_determinism_save_and_rollback) {
    const std::string map = encMap(R"({"type":"hero","owner":1,"pos":[20.5,4.5]},{"type":"golem","owner":2,"pos":[20.5,16.5]},
        {"type":"crawler","owner":2,"pos":[6.5,12.5]},{"type":"crawler","owner":2,"pos":[33.5,12.5]})");
    auto play = [&](Simulation& sim, int ticks, std::vector<uint64_t>* sums) {
        game::RtsAI a(1, 7), b(2, 7);
        for (int i = 0; i < ticks; ++i) {
            a.update(sim); b.update(sim);
            sim.step();
            if (sums && i % 40 == 0) sums->push_back(sim.checksum());
        }
    };
    auto s1 = makeSimJson(kEncRules, map, 5), s2 = makeSimJson(kEncRules, map, 5);
    s1->world().get<game::Unit>(findType(*s1, "golem", 2)).hp = Fixed::fromInt(1100);   // straight into phase 2: adds, waves
    s2->world().get<game::Unit>(findType(*s2, "golem", 2)).hp = Fixed::fromInt(1100);
    std::vector<uint64_t> c1, c2;
    play(*s1, 600, &c1);
    play(*s2, 600, &c2);
    CHECK(c1 == c2);
    CHECK(!encStore(*s1).adds.empty() || countType(*s1, "crawler", 2) > 0);
    // save: a loaded copy plays on bit-identically (casts, brains, adds, mobs survive)
    std::string save = s1->serialize();
    auto s3 = makeSimJson(kEncRules, map, 5);
    std::string err;
    CHECK(s3->deserialize(save, &err));
    CHECK(s3->checksum() == s1->checksum());
    CHECK(encStore(*s3).bosses.size() == encStore(*s1).bosses.size() && encStore(*s3).casts.size() == encStore(*s1).casts.size()
          && encStore(*s3).nextCast == encStore(*s1).nextCast);
    run(*s1, 2); run(*s3, 2);
    CHECK(s3->checksum() == s1->checksum());
    // rollback snapshot: restore and replay, same state
    SimSnapshot snap;
    s1->saveState(snap);
    run(*s1, 2);
    const uint64_t after = s1->checksum();
    const size_t casts = encStore(*s1).casts.size();
    s1->loadState(snap);
    run(*s1, 2);
    CHECK(s1->checksum() == after && encStore(*s1).casts.size() == casts);
    // the encounter state is part of the checksum
    const uint64_t base = s1->checksum();
    encStore(*s1).nextCast += 1;
    CHECK(s1->checksum() != base);
}

TEST(encounters_golem_pit_sample_ends_with_a_result) {
    const std::string dir = std::string(AIVERSE_SOURCE_DIR) + "/templates/boss_arena";
    Json rules = Json::parseFile(dir + "/rules.json");
    rules.set("scripts", Json::array());   // the sample's story script needs the project reader (headless covers it)
    for (uint64_t seed : {1, 2}) {
        SimConfig cfg; cfg.seed = seed;
        Simulation sim(cfg, std::make_unique<game::RtsModule>());
        sim.setup(rules, Json::parseFile(dir + "/maps/pit.json"));
        game::RtsAI a(1, seed), b(2, seed);
        int phases = 0, telegraphs = 0, statuses = 0;
        while (sim.result() == 0 && sim.tick() < 20u * 600) {
            a.update(sim); b.update(sim);
            sim.step();
            for (auto& e : sim.events()) {
                if (e.type == game::kEvBossPhase) ++phases;
                if (e.type == game::kEvTelegraphStart) ++telegraphs;
                if (e.type == game::kEvStatusStart) ++statuses;
            }
        }
        CHECK(sim.result() != 0);
        CHECK(phases >= 1 && telegraphs >= 5 && statuses >= 1);
    }
}
