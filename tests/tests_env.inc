// Map environment (Bunker Wars port, RtsEnv.h, docs/game-format.md § Environment): weather, wind, worms,
// toads, bombers, Mouro, map scroller, off-map senders, auto skills, caves, graveyards + souls, walls,
// saves and determinism.
#include "modules/rts/RtsEnv.h"

namespace {
const char* kEnvRules = R"J({
  "lanes": {"left": 0, "right": 0, "spacing": 0.4},
  "resources": {"morale": {"start": 0}},
  "playerAbilities": [
    {"id": "boom", "target": "point", "effects": [{"type": "damage", "amount": 1000, "radius": 2, "affects": "any"}]},
    {"id": "bless", "target": "point", "effects": [{"type": "garrisonAdd", "amount": 5, "radius": 3, "affects": "any"}]}
  ],
  "types": {
    "village": {"structure": {"footprint": [2, 2]}, "invulnerable": true, "capturable": true, "garrison": {"max": 60, "unit": "shroom", "interval": 1}},
    "village2": {"structure": {"footprint": [2, 2]}, "invulnerable": true, "capturable": true, "level": 2, "garrison": {"max": 60, "unit": "shroom", "interval": 1}},
    "grave": {"structure": {"footprint": [2, 2]}, "invulnerable": true, "garrison": {"max": 999, "unit": "shroom", "interval": 0}},
    "wall": {"structure": {"footprint": [1, 1]}, "health": {"max": 100}, "countsForVictory": false},
    "shroom": {"move": {"speed": 2}, "entersGarrison": true, "health": {"max": 10}},
    "bomber": {"move": {"speed": 0, "layer": "air"}, "health": {"max": 100}, "countsForVictory": false},
    "worm_look": {"move": {"speed": 0}, "invulnerable": true, "countsForVictory": false}
  }
})J";

std::string envMap(const std::string& env, int w = 40, int h = 20, const std::string& extraEntities = "") {
    return flatMap(w, h, R"(,"entities":[{"type":"village","owner":1,"pos":[5,10],"garrison":40,"id":"home"},
        {"type":"village","owner":2,"pos":[35,10],"garrison":40,"id":"enemy"},
        {"type":"village","owner":0,"pos":[20,3],"garrison":10,"id":"cave_a"},
        {"type":"village","owner":0,"pos":[20,17],"garrison":10,"id":"cave_b"})" + extraEntities + R"(],
        "environment":)" + env);
}
void envUpgrades(std::string& rules) {   // village -> village2 upgrade chain for the toad downgrade
    const std::string k = "\"village\": {\"structure\"";
    rules.replace(rules.find(k), k.size(), "\"village\": {\"upgrades\": [\"village2\"], \"structure\"");
}
void envSendCmd(Simulation& sim, uint8_t p, Entity from, Entity to, int pct) {
    Command c; c.player = p; c.type = game::kCmdSend; c.units = {from}; c.target = to; c.param = pct; sim.submit(c);
}
int envEvents(Simulation& sim, int64_t kind, int64_t data = -1) {
    int n = 0;
    for (auto& ev : sim.events())
        if (ev.type == game::kEvEnv && (ev.value >> 16) == kind && (data < 0 || (ev.value & 0xFFFF) == data)) ++n;
    return n;
}
Entity envPoint(Simulation& sim, int i) { return mod(sim).environment()->point(sim, i); }
} // namespace

TEST(env_absent_keeps_the_game_unchanged) {
    auto sim = makeSimJson(kEnvRules, flatMap(20, 10, R"(,"entities":[{"type":"village","owner":1,"pos":[5,5]}])"));
    CHECK(mod(*sim).environment() == nullptr);
    CHECK(sim->world().resource<game::EnvState>() == nullptr);
    run(*sim, 1);
}

TEST(env_weather_and_wind_cycles) {
    auto sim = makeSimJson(kEnvRules, envMap(R"({"weather": ["rain", "lightning"],
        "weatherParams": {"puddlesFillSpeed": 2, "puddlesDrySpeed": 2, "rainWaitTime": 1, "noRainWaitTime": 1,
                          "lightningKillTime": 0.5, "lightningKillFactor": 100, "lightningTime": 30, "lightningPointKillValue": 3},
        "wind": {"minIdleDuration": 0.5, "maxIdleDuration": 0.5, "minBlowDuration": 1, "maxBlowDuration": 1, "minForce": 1, "maxForce": 1},
        "zones": [{"id": "pond", "kind": "water", "rect": [10, 8, 20, 4], "slowdown": 2}]})"), 3);
    auto& m = mod(*sim);
    auto* env = m.environment();
    CHECK(env != nullptr);
    auto* st = sim->world().resource<game::EnvState>();
    bool puddleFull = false, puddleDry = false, blew = false, struck = false;
    Fixed windSeen{};
    for (int t = 0; t < 20 * 8; ++t) {
        sim->step();
        if (st->puddlesState == 1) puddleFull = true;
        if (st->puddlesState == 3) puddleDry = true;
        if (envEvents(*sim, game::kEnvWind) && st->windBlow.raw > 0) blew = true;
        if (st->windBlow.raw > 0) windSeen = fxMax(windSeen, fxAbs(env->windMultiplier(*sim, Vec3{Fixed::fromInt(1), Fixed{}, Fixed{}})) + fxAbs(env->windMultiplier(*sim, Vec3{Fixed{}, Fixed{}, Fixed::fromInt(1)})));
        if (envEvents(*sim, game::kEnvLightning)) struck = true;
    }
    CHECK(puddleFull && puddleDry);   // fill -> wait -> dry -> wait
    CHECK(blew && windSeen > Fixed::fromRatio(1, 2));
    CHECK(st->windDir >= 1);
    CHECK(struck);                    // lightning every 0.5 s with a 100% chance
    // the same seed gives the same weather (sim RNG only)
    auto b = makeSimJson(kEnvRules, envMap(R"({"weather": ["rain", "lightning"], "weatherParams": {"puddlesFillSpeed": 2, "puddlesDrySpeed": 2, "rainWaitTime": 1, "noRainWaitTime": 1, "lightningKillTime": 0.5, "lightningKillFactor": 100, "lightningTime": 30, "lightningPointKillValue": 3}, "wind": {"minIdleDuration": 0.5, "maxIdleDuration": 0.5, "minBlowDuration": 1, "maxBlowDuration": 1, "minForce": 1, "maxForce": 1}, "zones": [{"id": "pond", "kind": "water", "rect": [10, 8, 20, 4], "slowdown": 2}]})"), 3);
    for (int t = 0; t < 20 * 8; ++t) b->step();
    CHECK(b->checksum() == sim->checksum());
}

TEST(env_water_zone_slows_and_blizzard_kills_walkers) {
    auto sim = makeSimJson(kEnvRules, envMap(R"({"weather": ["blizzard"],
        "weatherParams": {"blizzardFillTime": 100, "blizzardUnitKillSpeed": 1, "blizzardUnitKillFactor": [1, 1]},
        "zones": [{"id": "pond", "kind": "water", "rect": [6, 6, 26, 8], "slowdown": 2}]})"));
    auto& m = mod(*sim);
    Entity home = envPoint(*sim, 0), enemy = envPoint(*sim, 1);
    run(*sim, 0.2);
    envSendCmd(*sim, 1, home, enemy, 50);
    Fixed slowSeen{}; int walkersMax = 0;
    for (int t = 0; t < 20 * 3; ++t) {
        sim->step();
        int walkers = 0;
        sim->world().each<game::LaneWalker, game::Unit>([&](Entity e, game::LaneWalker&, game::Unit&) {
            ++walkers;
            Vec3 p = sim->world().get<Transform>(e).pos;
            if (p.x > Fixed::fromRatio(65, 10) && p.x < Fixed::fromInt(30)) slowSeen = fxMax(slowSeen, Fixed::fromInt(2) - m.stat(*sim, e, game::Stat::Speed));
        });
        walkersMax = std::max(walkersMax, walkers);
    }
    CHECK(walkersMax > 0);
    CHECK(slowSeen >= Fixed::fromRatio(9, 10));   // speed 2 / water slowdown 2 (no rain: puddles stay full)
    run(*sim, 2);
    int walkers = 0;
    sim->world().each<game::LaneWalker>([&](Entity, game::LaneWalker&) { ++walkers; });
    CHECK(walkers == 0);                          // frozen by the blizzard within a second on the way
    CHECK(sim->world().get<game::Garrison>(enemy).count >= 40);
}

TEST(env_caves_share_owner_and_garrison) {
    auto sim = makeSimJson(kEnvRules, envMap(R"({"caves": [["cave_a", "cave_b"]]})"));
    auto* env = mod(*sim).environment();
    World& w = sim->world();
    Entity ca = envPoint(*sim, 2), cb = envPoint(*sim, 3), home = envPoint(*sim, 0);
    CHECK(ca.valid() && cb.valid());
    CHECK(!env->sendAllowed(*sim, ca, cb));
    envSendCmd(*sim, 1, home, ca, 100);
    run(*sim, 12);
    CHECK(w.get<Owner>(ca).player == 1);
    CHECK(w.get<Owner>(cb).player == 1);   // caught one cave = caught all
    CHECK(w.get<game::Garrison>(ca).count == w.get<game::Garrison>(cb).count);
    w.get<game::Garrison>(ca).count += 7;  // any change lands on every cave of the group
    run(*sim, 0.1);
    CHECK(w.get<game::Garrison>(cb).count == w.get<game::Garrison>(ca).count);
}

TEST(env_graveyard_collects_souls_and_sends_them) {
    auto sim = makeSimJson(kEnvRules, envMap(R"({"graveyard": {"type": "grave", "graveyardLimit": 6, "awaitDuration": 0.5},
        "souls": {"flyVelocity": 20, "attackerSoulDivider": 1, "defenderSoulDivider": 1}, "pointFire": {"fireDuration": 3}})", 40, 20,
        R"(,{"type":"grave","owner":0,"pos":[20,10],"garrison":0,"id":"grave"})"));
    auto* env = mod(*sim).environment();
    World& w = sim->world();
    Entity home = envPoint(*sim, 0), enemy = envPoint(*sim, 1), gy = envPoint(*sim, 4);
    CHECK(gy.valid() && !env->sendAllowed(*sim, home, gy));
    envSendCmd(*sim, 1, home, enemy, 50);
    bool sentOut = false, fire = false;
    auto* st = w.resource<game::EnvState>();
    for (int t = 0; t < 20 * 25; ++t) {
        sim->step();
        if (envEvents(*sim, game::kEnvGraveyard, 2)) sentOut = true;
        for (auto& f : st->fires) if (f.e == enemy && f.timer.raw > 0) fire = true;
    }
    CHECK(fire);       // the assaulted building burns
    CHECK(sentOut);    // souls of the assault filled the graveyard, it marched out to the last action buildings
    CHECK(!st->lastAction[2].empty() && st->lastAction[2].back() == 1);
}

TEST(env_worm_toad_and_bomber) {
    std::string rules = kEnvRules;
    envUpgrades(rules);
    auto sim = makeSimJson(rules, envMap(R"({
        "splines": [{"id": "path", "points": [[6, 6], [34, 6], [34, 14], [6, 14]], "loop": true}],
        "worms": [{"paths": ["path"], "onTheGroundCycles": [[30, 30]], "undergroundCycles": [[0.2, 0.2]], "spawnArmyCycles": [[3, 3]],
                   "info": {"speed": 3, "killZoneEnd": 2, "killZoneWidth": 4, "visionDistance": 6, "minDistanceToBuilding": 4,
                            "traceWidth": 2, "tailData": [{"shift": 0}, {"shift": 1}, {"shift": 1}]}, "look": "worm_look"}],
        "zones": [{"id": "swamp", "kind": "toad", "rect": [0, 0, 40, 20]}],
        "toad": {"pos": [20, 10], "attackDuration": 0.2, "postAttackDuration": 0.1, "maxUnits": 3},
        "flyBomb": {"rallyPoints": [{"start": [0, 10], "end": [40, 10]}], "flyVelocity": 10, "minIdleDuration": 0.1, "maxIdleDuration": 0.1,
                    "minDropBombPeriod": 0.3, "maxDropBombPeriod": 0.3, "minDropBombHeight": 1, "maxDropBombHeight": 1, "gravity": -20,
                    "explosionSkill": "boom", "type": "bomber"}})", 40, 20,
        R"(,{"type":"village2","owner":2,"pos":[20,10],"garrison":5,"id":"big"})"));
    auto& m = mod(*sim);
    World& w = sim->world();
    auto* st = w.resource<game::EnvState>();
    Entity home = envPoint(*sim, 0), enemy = envPoint(*sim, 1), big = envPoint(*sim, 4);
    int toadAttacks = 0, toadExplosions = 0, bombs = 0, wormArmy = 0, wormUp = 0;
    for (int t = 0; t < 20 * 20; ++t) {
        if (t % 20 == 0) envSendCmd(*sim, 1, home, enemy, 30);
        sim->step();
        toadAttacks += envEvents(*sim, game::kEnvToadAttack);
        toadExplosions += envEvents(*sim, game::kEnvToadExplode);
        bombs += envEvents(*sim, game::kEnvBomb, 1);
        for (auto& ev : sim->events())
            if (ev.type == game::kEvEnv && (ev.value >> 16) == game::kEnvWorm) {
                const int s = int((ev.value >> 8) & 0xFF);
                if (s == 6) ++wormArmy;
                if (s == 0) ++wormUp;
            }
    }
    CHECK(toadAttacks >= 3);
    CHECK(toadExplosions >= 1);
    CHECK(!w.alive(big) || m.rules().types[w.get<game::Unit>(big).type].id == "village");   // exploded: back to level 1
    CHECK(bombs >= 3);
    CHECK(wormArmy >= 1 && wormUp >= 1);   // army from under ground, then it crawls
    CHECK(st->worms.size() == 1 && st->worms[0].seg.size() == 3);
    std::vector<game::Environment::Body> bodies;
    m.environment()->bodies(*sim, bodies);
    if (st->worms[0].state != 4) CHECK(!bodies.empty());
}

TEST(env_scroller_outmap_mouro_and_autoskills) {
    auto sim = makeSimJson(kEnvRules, envMap(R"({
        "outmapUnits": {"sources": ["cave_a"], "moveActions": [{"time": 0.5, "from": "cave_a", "to": "home", "count": 6}]},
        "mapScroller": {"screen": [40, 20], "actions": [{"type": "waitTime", "seconds": 1}, {"type": "moveScreen", "position": [5, 0], "duration": 1},
                                                     {"type": "convertAll", "to": "player"}]},
        "splines": [{"id": "fire", "points": [[8, 2], [38, 2]], "skill": "bless", "velocity": 20, "emitPeriod": 1}],
        "mouro": {"castPos": [20, 10], "actionHolders": [{"startsFrom": 0.2, "actions": [
            {"name": "army", "type": "addUnits", "sourcePoints": ["cave_b"], "targetPoints": ["home"], "unitsPerPoint": 8, "duration": 1, "castAt": 0.1}]}]}})"));
    World& w = sim->world();
    auto* st = w.resource<game::EnvState>();
    Entity home = envPoint(*sim, 0), enemy = envPoint(*sim, 1);
    int mouroActions = 0, scroll = 0, abilities = 0;
    for (int t = 0; t < 20 * 4; ++t) {
        sim->step();
        mouroActions += envEvents(*sim, game::kEnvMouroAction);
        scroll += envEvents(*sim, game::kEnvScroll);
        for (auto& ev : sim->events()) if (ev.type == game::kEvPlayerAbility && ev.player == 0) ++abilities;
    }
    CHECK(st->outmapDone);
    CHECK(mouroActions >= 1);
    CHECK(scroll >= 3);
    CHECK(st->screenPos.x == Fixed::fromInt(5));
    CHECK(w.get<Owner>(enemy).player == 1 && w.get<Owner>(home).player == 1);   // convertAll to the player
    CHECK(abilities >= 2);   // the auto skill fired at the spline start every second
}

TEST(env_walls_count_hits_and_fall) {
    auto sim = makeSimJson(kEnvRules, envMap(R"({"walls": [{"point": "wall", "hp": 3, "destroyDuration": 0.5}]})", 40, 20,
        R"(,{"type":"wall","owner":0,"pos":[20,10],"id":"wall"})"));
    auto* env = mod(*sim).environment();
    World& w = sim->world();
    Entity wall = envPoint(*sim, 4);
    CHECK(w.get<Owner>(wall).player == game::kHostileNeutral);
    for (int i = 0; i < 3; ++i) { Fixed d = Fixed::fromInt(1000); CHECK(env->damageOverride(*sim, wall, d) && d.raw == 0); }
    CHECK(w.get<Owner>(wall).player == 0);   // rubble: nobody shoots it
    run(*sim, 1);
    CHECK(!w.alive(wall));
}

TEST(env_save_load_continues_identically) {
    const std::string env = R"({"weather": ["rain", "lightning"], "weatherParams": {"lightningKillTime": 0.5, "lightningKillFactor": 50},
        "wind": {"minIdleDuration": 0.3, "maxIdleDuration": 1, "minBlowDuration": 1, "maxBlowDuration": 2, "minForce": 0.5, "maxForce": 1},
        "graveyard": {"type": "grave", "graveyardLimit": 5}, "caves": [["cave_a", "cave_b"]],
        "splines": [{"id": "path", "points": [[6, 6], [34, 6], [34, 14]], "loop": false}],
        "worms": [{"paths": ["path"], "onTheGroundCycles": [[2, 3]], "undergroundCycles": [[1, 2]], "info": {"speed": 3}}]})";
    const std::string map = envMap(env, 40, 20, R"(,{"type":"grave","owner":0,"pos":[20,10],"garrison":0})");
    auto a = makeSimJson(kEnvRules, map, 9);
    for (int t = 0; t < 20 * 6; ++t) {
        if (t % 30 == 0) envSendCmd(*a, 1, envPoint(*a, 0), envPoint(*a, 1), 40);
        if (t % 30 == 15) envSendCmd(*a, 2, envPoint(*a, 1), envPoint(*a, 0), 40);
        a->step();
    }
    const std::string bytes = a->serialize();
    CHECK(!bytes.empty());
    auto b = makeSimJson(kEnvRules, map, 9);
    std::string err;
    CHECK(b->deserialize(bytes, &err));
    CHECK(b->checksum() == a->checksum());
    for (int t = 0; t < 20 * 6; ++t) { a->step(); b->step(); }
    CHECK(b->checksum() == a->checksum());
    CHECK(a->serialize() == b->serialize());
}
