// Arsenal (rules "arsenal", RtsArsenal.h): weapon slots and ammo, free fire along the aim (walls stop it, the first
// body takes it, passive neutral cars too), hit-scan fists, running dry, switching, crates (ammo, money, health, gun
// shop) and their respawn, money for kills, losing the weapons on death, determinism, saves and rollback snapshots.
#include "modules/rts/RtsArsenal.h"

namespace {
const char* kArsenalRules = R"J({
  "victory": "noUnits",
  "resources": {"cash": {"start": 0}},
  "topDown": {"heroes": ["guy"], "respawn": 1, "move": {"accel": 0, "decel": 0}, "aim": {"snapCone": 0, "fires": true}, "autoFire": false},
  "arsenal": {
    "money": "cash", "deathFine": 30,
    "weapons": [
      {"weapon": "fists", "name": "Fists", "start": -1},
      {"weapon": "pistol", "name": "Pistol", "start": 3, "max": 20},
      {"weapon": "shotgun", "name": "Shotgun", "pellets": 3, "spread": 40, "max": 10},
      {"weapon": "rocket", "name": "Rocket launcher", "max": 5}
    ],
    "pickups": {
      "crate_pistol": {"weapon": "pistol", "ammo": 5, "respawn": 2},
      "crate_cash": {"money": 100, "respawn": 0},
      "crate_health": {"heal": 50},
      "shop_shotgun": {"weapon": "shotgun", "ammo": 4, "price": 150, "respawn": 1}
    }
  },
  "types": {
    "guy": {"health": {"max": 100}, "move": {"speed": 4, "radius": 0.3}, "sight": 12,
            "weapons": [
              {"id": "fists", "range": 0.8, "damage": 10, "cooldown": 0.4},
              {"id": "pistol", "range": 8, "damage": 20, "cooldown": 0.3, "projectileSpeed": 25, "flight": {"radius": 0.05, "maxDistance": 8}},
              {"id": "shotgun", "range": 5, "damage": 15, "cooldown": 0.8, "projectileSpeed": 25, "flight": {"radius": 0.05, "maxDistance": 5}},
              {"id": "rocket", "range": 10, "damage": 30, "cooldown": 1.5, "projectileSpeed": 12, "splash": 1.5, "flight": {"radius": 0.1, "maxDistance": 10}}
            ]},
    "ped":  {"health": {"max": 40}, "move": {"speed": 0.01, "radius": 0.3}, "sight": 4, "bounty": {"cash": 10}},
    "car":  {"health": {"max": 30}, "move": {"speed": 0, "radius": 0.4}, "sight": 2, "bounty": {"cash": 50}},
    "crate_pistol": {"health": {"max": 1}, "invulnerable": true, "move": {"speed": 0, "radius": 0.2, "layer": "air"}, "sight": 0},
    "crate_cash":   {"health": {"max": 1}, "invulnerable": true, "move": {"speed": 0, "radius": 0.2, "layer": "air"}, "sight": 0},
    "crate_health": {"health": {"max": 1}, "invulnerable": true, "move": {"speed": 0, "radius": 0.2, "layer": "air"}, "sight": 0},
    "shop_shotgun": {"health": {"max": 1}, "invulnerable": true, "move": {"speed": 0, "radius": 0.2, "layer": "air"}, "sight": 0},
    "dummy": {"health": {"max": 100000}, "move": {"speed": 0, "radius": 0.4}, "sight": 2}
  }
})J";

game::Arsenal& ars(Simulation& sim) { return *mod(sim).arsenal(); }
const game::ArsenalHand& hand1(Simulation& sim) { return *ars(sim).hand(sim, 1); }
Entity typed(Simulation& sim, const char* id) {
    Entity out = kNullEntity;
    auto& types = mod(sim).rules().types;
    sim.world().each<game::Unit, Transform>([&](Entity e, game::Unit& u, Transform&) {
        if (types[u.type].id == id && (!out.valid() || e.index < out.index)) out = e;
    });
    return out;
}
Fixed cash(Simulation& sim) { return mod(sim).state(sim).players[1].stock[0]; }
Fixed arsHp(Simulation& sim, Entity e) { return sim.world().get<game::Unit>(e).hp; }
void shoot(Simulation& sim, Vec3 dir) { steer(sim, 1, {}, 0, true, dir); }
void holdFire(Simulation& sim) { steer(sim, 1, {}, 0); }
void weaponKey(Simulation& sim, int64_t param) { Command c; c.player = 1; c.type = game::kCmdTopDownWeapon; c.param = param; sim.submit(c); }
const char* kFar = R"({"type":"dummy","owner":2,"pos":[30.5,1.5]})";
std::string arsenalMap(const std::string& ents, bool wall = true) { return topDownMap(ents + "," + kFar, wall); }
} // namespace

TEST(arsenal_rules_parse_and_errors) {
    std::vector<std::string> ids = {"guy", "crate"}, res = {"cash"};
    auto r = game::Arsenal::parse(Json::parse(R"({"weapons": ["fists", {"weapon": "gun", "start": 5, "pellets": 2, "spread": 90}],
        "pickups": {"crate": {"weapon": "gun", "ammo": 3, "price": 10}}})"), ids, res);
    CHECK(r.enabled && r.weapons.size() == 2 && r.weapons[0].name == "fists" && r.weapons[1].start == 5 && r.weapons[1].pellets == 2);
    CHECK(r.weapons[1].spread == Fixed::fromRatio(1, 4) && r.money == 0 && r.pickups.size() == 1 && r.pickups[0].weapon == 1 && r.pickups[0].type == 1);
    CHECK(!game::Arsenal::parse(Json(), ids, res).enabled);
    auto fails = [&](const char* json, const char* what) {
        try { game::Arsenal::parse(Json::parse(json), ids, res); } catch (const std::exception& e) { return std::string(e.what()).find(what) != std::string::npos; }
        return false;
    };
    CHECK(fails(R"({"weapons": []})", "list at least one weapon slot"));
    CHECK(fails(R"({"weapons": ["a", "a"]})", "listed twice"));
    CHECK(fails(R"({"weapons": [{"weapon": "a", "pellets": 0}]})", "pellets: 1..16"));
    CHECK(fails(R"({"weapons": ["a"], "money": "gold"})", "unknown resource 'gold'"));
    CHECK(fails(R"({"weapons": ["a"], "pickups": {"box": {"money": 5}}})", "unknown type 'box'"));
    CHECK(fails(R"({"weapons": ["a"], "pickups": {"crate": {"weapon": "b"}}})", "'b' is not an arsenal slot"));
    CHECK(fails(R"({"weapons": ["a"], "pickups": {"crate": {}}})", "gives nothing"));
    CHECK(fails(R"({"weapons": [{"weapon": "a", "start": -2}]})", "-1 (unlimited)"));
    // every hero type needs every slot's weapon; the block needs a hero
    bool threw = false;
    try { makeSimJson(R"({"topDown": {"heroes": ["u"]}, "arsenal": {"weapons": ["laser"]}, "types": {"u": {"health": {"max": 10}, "move": {"speed": 1}}}})", topDownMap(R"({"type":"u","owner":1,"pos":[2.5,2.5]})")); }
    catch (const std::exception& e) { threw = std::string(e.what()).find("hero type 'u' has no weapon 'laser'") != std::string::npos; }
    CHECK(threw);
    threw = false;
    try { makeSimJson(R"({"arsenal": {"weapons": ["a"]}, "types": {"u": {"health": {"max": 10}, "move": {"speed": 1}}}})", topDownMap("")); }
    catch (const std::exception& e) { threw = std::string(e.what()).find("needs the 'topDown' block") != std::string::npos; }
    CHECK(threw);
}

TEST(arsenal_free_fire_kills_pays_and_runs_dry) {
    auto sim = makeSimJson(kArsenalRules, arsenalMap(R"({"type":"guy","owner":1,"pos":[4.5,8.5]},{"type":"ped","owner":2,"pos":[8.5,8.5]})"));
    auto& w = sim->world();
    Entity ped = typed(*sim, "ped");
    run(*sim, 0.1);
    // the best weapon held is in hand at the start
    CHECK(hand1(*sim).current == 1 && hand1(*sim).ammo == std::vector<int32_t>({-1, 3, 0, 0}));
    // no target lock: aiming north (away from the ped) fires a shot into the void
    shoot(*sim, kNorth);
    run(*sim, 0.25);
    CHECK(hand1(*sim).ammo[1] == 2 && arsHp(*sim, ped) == Fixed::fromInt(40));
    holdFire(*sim);
    run(*sim, 0.6);
    CHECK(arsHp(*sim, ped) == Fixed::fromInt(40));
    // aiming east: two pistol rounds kill the ped (40 hp, 20 a round) and its bounty is paid
    shoot(*sim, kEast);
    run(*sim, 1.0);
    CHECK(!w.alive(ped) || arsHp(*sim, ped).raw <= 0);
    run(*sim, 0.2);
    CHECK(cash(*sim) == Fixed::fromInt(10));
    // the pistol ran dry: fists in hand, still firing, with nothing to hit
    CHECK(hand1(*sim).ammo[1] == 0 && hand1(*sim).current == 0);
    holdFire(*sim);
    weaponKey(*sim, game::kWeaponNext);   // nothing else to switch to
    run(*sim, 0.1);
    CHECK(hand1(*sim).current == 0);
}

TEST(arsenal_walls_stop_bullets_and_parked_cars_take_them) {
    // ped behind the wall at x = 12: the round hits the wall
    auto sim = makeSimJson(kArsenalRules, arsenalMap(R"({"type":"guy","owner":1,"pos":[9.5,8.5]},{"type":"ped","owner":2,"pos":[14.5,8.5]})"));
    Entity ped = typed(*sim, "ped");
    shoot(*sim, kEast);
    run(*sim, 1.5);
    CHECK(hand1(*sim).ammo[1] == 0 && arsHp(*sim, ped) == Fixed::fromInt(40));
    // a passive neutral car between the hero and a ped takes the rounds and pays its bounty when it blows up
    auto s2 = makeSimJson(kArsenalRules, arsenalMap(R"({"type":"guy","owner":1,"pos":[3.5,8.5]},{"type":"car","owner":0,"pos":[6.5,8.5]},{"type":"ped","owner":2,"pos":[9.5,8.5]})"));
    Entity car = typed(*s2, "car"), ped2 = typed(*s2, "ped");
    shoot(*s2, kEast);
    run(*s2, 0.25);
    CHECK(arsHp(*s2, car) == Fixed::fromInt(10) && arsHp(*s2, ped2) == Fixed::fromInt(40));
    run(*s2, 1.5);
    CHECK(!s2->world().alive(car) || arsHp(*s2, car).raw <= 0);
    CHECK(cash(*s2) == Fixed::fromInt(50));
    CHECK(arsHp(*s2, ped2) == Fixed::fromInt(20));   // the third round flew on to the ped
}

TEST(arsenal_rocket_goes_off_at_the_wall) {
    // the wall is at x = 12; a ped stands next to it, off the line of fire, and a car too
    auto sim = makeSimJson(kArsenalRules, arsenalMap(R"({"type":"guy","owner":1,"pos":[6.5,8.5]},{"type":"ped","owner":2,"pos":[11.5,9.5]},{"type":"car","owner":0,"pos":[11.4,7.4]},{"type":"ped","owner":2,"pos":[9.5,12.5]})"));
    run(*sim, 0.1);
    ars(*sim).store(*sim)->hands[1].ammo[3] = 1;
    weaponKey(*sim, game::kWeaponSlot + 3);
    run(*sim, 0.25);
    CHECK(hand1(*sim).current == 3);
    shoot(*sim, kEast);
    int ends = 0, launches = 0;   // the shot is reported at the muzzle, its end where the rocket went off (effects, sounds)
    Vec3 endAt{}, launchAt{};
    for (int i = 0; i < 30; ++i) {
        sim->step();
        for (const auto& ev : sim->events()) {
            if (ev.type == game::kEvShotEnd) { ++ends; endAt = ev.pos; CHECK(ev.value == 3 && ev.a == mod(*sim).topDown()->heroOf(*sim, 1)); }
            if (ev.type == game::kEvAttack) { ++launches; launchAt = ev.pos; }
        }
    }
    CHECK(ends == 1 && launches == 1 && endAt.x > Fixed::fromInt(11) && endAt.x < Fixed::fromInt(12));
    CHECK(launchAt.x > Fixed::fromRatio(68, 10) && launchAt.x < Fixed::fromInt(7));
    Entity near = typed(*sim, "ped"), car = typed(*sim, "car");
    Entity far = kNullEntity;
    sim->world().each<game::Unit, Transform>([&](Entity e, game::Unit&, Transform& t) { if (t.pos.z > Fixed::fromInt(12)) far = e; });
    CHECK(arsHp(*sim, near) == Fixed::fromInt(10) && far.valid() && arsHp(*sim, far) == Fixed::fromInt(40));
    CHECK(!car.valid() || arsHp(*sim, car).raw <= 0);          // 30 hp car: gone, and paid for
    CHECK(cash(*sim) == Fixed::fromInt(50) && hand1(*sim).ammo[3] == 0 && hand1(*sim).current == 1);
}

TEST(arsenal_fists_hit_scan_in_reach_only) {
    const char* rules = kArsenalRules;
    auto sim = makeSimJson(rules, arsenalMap(R"({"type":"guy","owner":1,"pos":[4.5,8.5]},{"type":"ped","owner":2,"pos":[5.3,8.5]},{"type":"ped","owner":2,"pos":[4.5,11.5]})"));
    Entity near = typed(*sim, "ped");
    Entity far = kNullEntity;
    sim->world().each<game::Unit, Transform>([&](Entity e, game::Unit&, Transform& t) { if (t.pos.z > Fixed::fromInt(11)) far = e; });
    weaponKey(*sim, game::kWeaponSlot + 0);
    run(*sim, 0.25);
    CHECK(hand1(*sim).current == 0);
    shoot(*sim, kEast);
    run(*sim, 0.5);                                   // two punches (cooldown 0.4)
    CHECK(arsHp(*sim, near) == Fixed::fromInt(20));
    shoot(*sim, kSouth);                              // three cells away: out of reach
    run(*sim, 1.0);
    CHECK(arsHp(*sim, far) == Fixed::fromInt(40) && hand1(*sim).ammo[0] == -1);
}

TEST(arsenal_crates_ammo_money_health_shop_and_respawn) {
    auto sim = makeSimJson(kArsenalRules, arsenalMap(R"({"type":"guy","owner":1,"pos":[2.5,8.5]},
        {"type":"crate_pistol","owner":0,"pos":[4.5,8.5]},{"type":"shop_shotgun","owner":0,"pos":[6.5,8.5]},
        {"type":"crate_cash","owner":0,"pos":[8.5,8.5]},{"type":"crate_health","owner":0,"pos":[10.5,8.5]})", false));
    auto& w = sim->world();
    Entity hero = mod(*sim).topDown()->heroOf(*sim, 1);
    Entity pistol = typed(*sim, "crate_pistol"), shop = typed(*sim, "shop_shotgun"), money = typed(*sim, "crate_cash"), health = typed(*sim, "crate_health");
    CHECK(ars(*sim).isCrate(*sim, pistol) && ars(*sim).isCrate(*sim, shop) && !ars(*sim).isCrate(*sim, hero));
    w.get<game::Unit>(hero).hp = Fixed::fromInt(40);
    // walk east over the row: ammo, the shop (no money yet: skipped), money, health
    steer(*sim, 1, kEast);
    run(*sim, 2.2);
    steer(*sim, 1, {}, 0);
    run(*sim, 0.1);
    CHECK(hand1(*sim).ammo[1] == 8 && (w.get<game::Unit>(pistol).flags & game::kUnitInCargo));
    CHECK(hand1(*sim).ammo[2] == 0 && !(w.get<game::Unit>(shop).flags & game::kUnitInCargo));
    CHECK(cash(*sim) == Fixed::fromInt(100) && (w.get<game::Unit>(money).flags & game::kUnitInCargo));
    CHECK(arsHp(*sim, hero) == Fixed::fromInt(90) && (w.get<game::Unit>(health).flags & game::kUnitInCargo));
    // the ammo crate is back after 2 s; the money crate never comes back (respawn 0)
    run(*sim, 2.0);
    CHECK(!(w.get<game::Unit>(pistol).flags & game::kUnitInCargo) && (w.get<game::Unit>(money).flags & game::kUnitInCargo));
    // buy the shotgun: paid, in hand (a weapon the hero did not have)
    mod(*sim).addResource(*sim, 1, 0, Fixed::fromInt(100));
    steer(*sim, 1, Vec3{Fixed::fromInt(-1), Fixed{}, Fixed{}});
    run(*sim, 1.2);
    steer(*sim, 1, {}, 0);
    run(*sim, 0.1);
    CHECK(hand1(*sim).ammo[2] == 4 && hand1(*sim).current == 2 && cash(*sim) == Fixed::fromInt(50));
    // a full pistol leaves the ammo crate where it is
    auto st = ars(*sim).store(*sim);
    st->hands[1].ammo[1] = 20;
    steer(*sim, 1, Vec3{Fixed::fromInt(-1), Fixed{}, Fixed{}});
    run(*sim, 1.2);
    CHECK(!(w.get<game::Unit>(pistol).flags & game::kUnitInCargo));
    // the shotgun fires a fan of three pellets per shell
    steer(*sim, 1, {}, 0);
    run(*sim, 0.1);
    int before = 0, pellets = 0;
    w.each<game::Projectile>([&](Entity, game::Projectile&) { ++before; });
    weaponKey(*sim, game::kWeaponSlot + 2);
    shoot(*sim, kNorth);
    for (int i = 0; i < 8; ++i) {   // one shell (cooldown 0.8 s)
        sim->step();
        int n = 0;
        w.each<game::Projectile>([&](Entity, game::Projectile&) { ++n; });
        pellets = std::max(pellets, n);
    }
    CHECK(before == 0 && pellets == 3 && hand1(*sim).ammo[2] == 3);
}

TEST(arsenal_switching_skips_empty_slots) {
    auto sim = makeSimJson(kArsenalRules, arsenalMap(R"({"type":"guy","owner":1,"pos":[4.5,8.5]})"));
    run(*sim, 0.1);
    auto* st = ars(*sim).store(*sim);
    CHECK(hand1(*sim).current == 1);
    weaponKey(*sim, game::kWeaponNext);   // shotgun empty: wraps to fists
    run(*sim, 0.25);
    CHECK(hand1(*sim).current == 0);
    weaponKey(*sim, game::kWeaponPrev);   // backwards skips the empty shotgun: pistol
    run(*sim, 0.25);
    CHECK(hand1(*sim).current == 1);
    weaponKey(*sim, game::kWeaponSlot + 2);   // empty: ignored
    run(*sim, 0.25);
    CHECK(hand1(*sim).current == 1);
    st->hands[1].ammo[2] = 2;
    weaponKey(*sim, game::kWeaponNext);
    run(*sim, 0.25);
    CHECK(hand1(*sim).current == 2);
}

TEST(arsenal_dying_loses_the_weapons_and_pays_the_fine) {
    auto sim = makeSimJson(kArsenalRules, arsenalMap(R"({"type":"guy","owner":1,"pos":[4.5,8.5]},{"type":"dummy","owner":1,"pos":[2.5,2.5]})"));
    auto& w = sim->world();
    run(*sim, 0.1);
    auto* st = ars(*sim).store(*sim);
    st->hands[1].ammo = {-1, 15, 6, 2};
    st->hands[1].current = 2;
    mod(*sim).addResource(*sim, 1, 0, Fixed::fromInt(100));
    Entity hero = mod(*sim).topDown()->heroOf(*sim, 1);
    w.get<game::Unit>(hero).hp = Fixed{};
    run(*sim, 1.5);
    Entity again = mod(*sim).topDown()->heroOf(*sim, 1);
    CHECK(again.valid() && again != hero);
    CHECK(hand1(*sim).hero == again && hand1(*sim).ammo == std::vector<int32_t>({-1, 3, 0, 0}) && hand1(*sim).current == 1);
    CHECK(cash(*sim) == Fixed::fromInt(70));
}

TEST(arsenal_determinism_save_and_rollback) {
    const std::string map = arsenalMap(R"({"type":"guy","owner":1,"pos":[3.5,8.5]},{"type":"ped","owner":2,"pos":[8.5,8.5]},
        {"type":"car","owner":0,"pos":[6.5,6.5]},{"type":"crate_pistol","owner":0,"pos":[3.5,6.5]})");
    auto play = [&](Simulation& sim, std::vector<uint64_t>* sums) {
        for (int i = 0; i < 300; ++i) {
            if (i == 5) shoot(sim, kEast);
            if (i == 40) steer(sim, 1, kNorth, 1000, true, Vec3{Fixed::fromInt(1), Fixed{}, Fixed::fromInt(-1)});
            if (i == 90) weaponKey(sim, game::kWeaponPrev);
            if (i == 120) shoot(sim, kSouth);
            if (i == 200) holdFire(sim);
            sim.step();
            if (sums && i % 30 == 0) sums->push_back(sim.checksum());
        }
    };
    auto s1 = makeSimJson(kArsenalRules, map, 3), s2 = makeSimJson(kArsenalRules, map, 3);
    std::vector<uint64_t> c1, c2;
    play(*s1, &c1);
    play(*s2, &c2);
    CHECK(c1 == c2 && c1.front() != c1.back());
    std::string save = s1->serialize();
    auto s3 = makeSimJson(kArsenalRules, map, 3);
    std::string err;
    CHECK(s3->deserialize(save, &err));
    CHECK(s3->checksum() == s1->checksum() && ars(*s3).hand(*s3, 1)->ammo == hand1(*s1).ammo);
    shoot(*s1, kEast); shoot(*s3, kEast);
    run(*s1, 1.0); run(*s3, 1.0);
    CHECK(s3->checksum() == s1->checksum());
    SimSnapshot snap;
    s1->saveState(snap);
    weaponKey(*s1, game::kWeaponNext);
    shoot(*s1, kNorth);
    run(*s1, 1.0);
    const uint64_t after = s1->checksum();
    s1->loadState(snap);
    weaponKey(*s1, game::kWeaponNext);
    shoot(*s1, kNorth);
    run(*s1, 1.0);
    CHECK(s1->checksum() == after);
}

TEST(arsenal_games_without_the_block_are_untouched) {
    auto sim = makeSimJson(kTopDownRules, topDownMap(R"({"type":"drone","owner":1,"pos":[6.5,8.5]},{"type":"dummy","owner":2,"pos":[30.5,1.5]})"));
    CHECK(mod(*sim).arsenal() && !mod(*sim).arsenal()->active() && !mod(*sim).arsenal()->store(*sim));
    weaponKey(*sim, game::kWeaponNext);   // ignored
    run(*sim, 0.2);
    CHECK(sim->world().alive(mod(*sim).topDown()->heroOf(*sim, 1)));
}
