// In-match progression on real units (rules "progression", Progression.h; match-progression library package):
// kills -> xp orbs -> pickup -> level-up offer -> command / bot choice, perk stats on the unit pipelines, bullet perks
// through the projectile programs, on-hit payloads, recruits + summon circles, determinism, saves, rollback, Lua.
#include "engine/game/Progression.h"

namespace {
const char* kProgRules = R"J({
  "victory": "noStructures",
  "resources": {"gold": {"start": 0}},
  "statuses": {"burn": {"tags": ["debuff"], "tick": {"damage": 3, "interval": 0.5}}},
  "progression": {
    "players": [1],
    "units": [{"id": 1, "type": "drone", "hero": true}, {"id": 2, "type": "gunbot", "cost": 1}],
    "perks": [
      {"id": 1, "key": "damage", "maxSelections": 3, "aiPriority": 1, "effects": [{"stat": "outgoingDamage", "op": "multiply", "value": 1500}]},
      {"id": 2, "key": "vitality", "effects": [{"stat": "maxHealth", "op": "multiply", "value": 2000}]},
      {"id": 3, "key": "swift", "maxSelections": 0, "effects": [{"stat": "moveSpeed", "op": "add", "value": 1000}]},
      {"id": 4, "key": "split_shot", "bearer": 1, "bullet": {"modifiers": [{"kind": "split", "count": 3, "angle": 40}]}},
      {"id": 5, "key": "pierce", "maxSelections": 3, "bullet": {"modifiers": [{"kind": "flyThrough", "count": 1}]}},
      {"id": 6, "key": "burn", "hook": "on_burn_perk", "onHit": [{"type": "status", "status": "burn", "duration": 2}]},
      {"id": 7, "key": "armor", "effects": [{"stat": "incomingDamage", "op": "multiply", "value": 500}]},
      {"id": 8, "key": "rapid", "effects": [{"stat": "fireInterval", "op": "multiply", "value": 500}]},
      {"id": 9, "key": "regen", "effects": [{"stat": "healthRegen", "op": "add", "value": 20000}]},
      {"id": 10, "key": "magnet", "maxSelections": 0, "effects": [{"stat": "pickupRadius", "op": "add", "value": 4000}]}
    ],
    "xp": {"toNext": [3, 4, 4, 4, 4, 4, 4, 4, 4, 4]},
    "offers": {"default": ["anyPerk", "anyPerk", "anyPerk"], "levels": [{"level": 1, "variants": ["recruit"]}]},
    "killXp": {"mob": 2},
    "pickups": {"radius": 3},
    "squad": {"follow": 4},
    "summonPoints": [{"id": 1, "position": [20000, 0, 8000], "radiusMilli": 2000, "progressTicks": 20, "action": "restoreOrRecruit", "singleUse": false}]
  },
  "types": {
    "drone": {"health": {"max": 100}, "move": {"speed": 4, "radius": 0.4}, "sight": 8,
              "weapons": [{"range": 5, "damage": 20, "cooldown": 0.5, "projectileSpeed": 20}]},
    "gunbot": {"health": {"max": 60}, "move": {"speed": 3, "radius": 0.4}, "sight": 7,
               "weapons": [{"range": 4, "damage": 5, "cooldown": 1, "projectileSpeed": 16}]},
    "mob": {"health": {"max": 10}, "move": {"speed": 0, "radius": 0.4}, "sight": 2},
    "tank": {"health": {"max": 100000}, "move": {"speed": 0, "radius": 0.4}, "sight": 2},
    "core": {"structure": {"footprint": [1, 1]}, "health": {"max": 100000}, "sight": 1}
  }
})J";

std::string progMap(const std::string& entities) {
    std::string rows;
    for (int y = 0; y < 16; ++y) rows += std::string(y ? "," : "") + "\"" + std::string(32, '.') + "\"";
    return R"({"size":[32,16],"players":[{"name":"A","team":1},{"name":"B","team":2}],"terrain":{"rows":[)" + rows +
           "]},\"entities\":[{\"type\":\"core\",\"owner\":1,\"pos\":[1.5,14.5]},{\"type\":\"core\",\"owner\":2,\"pos\":[30.5,1.5]}" + (entities.empty() ? "" : ",") + entities + "]}";
}
game::Progression& prog(Simulation& sim) { return *mod(sim).progression(); }
const aiverse::lib::mechanics::PlayerProgress& pp(Simulation& sim, uint8_t p) { return *prog(sim).progress(sim, p); }
void progCmd(Simulation& sim, uint8_t p, int64_t op, int card, uint32_t rev) {
    Command c; c.player = p; c.type = game::kCmdProgression; c.param = game::progressionParam(op, card, rev);
    sim.submit(c);
}
int64_t perkId(Simulation& sim, const char* key) { for (auto& [id, d] : prog(sim).rules().defs.perks) if (d.key == key) return id; return 0; }
int countUnits(Simulation& sim, const char* type, uint8_t owner) { return countType(sim, type, owner); }
} // namespace

TEST(progression_critical_hit_and_paid_bounty_metadata) {
    std::string rules = kProgRules;
    const std::string gold = R"("gold": {"start": 0})";
    rules.replace(rules.find(gold), gold.size(), R"("gold": {"start": 7, "capped": true, "storage": 10}, "wood": {"start": 0})");
    rules.replace(rules.find(R"(      {"id": 10, "key": "magnet")"), 0,
        R"(      {"id": 11, "key": "guaranteed_crit", "effects": [{"stat": "critChance", "op": "add", "value": 1000}]},
)");
    const std::string mob = R"("mob": {"health": {"max": 10})";
    rules.replace(rules.find(mob), mob.size(), R"("mob": {"bounty": {"gold": 25, "wood": 5}, "health": {"max": 10})");
    auto sim = makeSimJson(rules, progMap(R"({"type":"drone","owner":1,"pos":[4.5,8.5]},{"type":"tank","owner":2,"pos":[24.5,8.5]},{"type":"mob","owner":2,"pos":[26.5,8.5]})"));
    auto& m = mod(*sim);
    const Entity hero = prog(*sim).heroOf(*sim,1), tank = findType(*sim,"tank",2), victim = findType(*sim,"mob",2);
    CHECK(hero.valid() && tank.valid() && victim.valid());
    m.dealDamage(*sim,tank,5_fx,game::kDamagePure,hero,1);
    CHECK(sim->events().back().type == game::kEvHit);
    CHECK(!(sim->events().back().flags & game::kHitCritical));
    CHECK(prog(*sim).grantPerk(*sim,1,perkId(*sim,"guaranteed_crit"),-1));
    const Fixed hp=sim->world().get<game::Unit>(tank).hp;
    m.dealDamage(*sim,tank,5_fx,game::kDamagePure,hero,1);
    CHECK(sim->events().back().type == game::kEvHit);
    CHECK(sim->events().back().flags & game::kHitCritical);
    CHECK(sim->events().back().value == (10_fx).raw);
    CHECK(sim->world().get<game::Unit>(tank).hp == hp-10_fx);
    m.dealDamage(*sim,victim,20_fx,game::kDamagePure,hero,1);
    const uint64_t key=victim.packed();
    sim->step();
    const int goldId=m.rules().findResource("gold"), woodId=m.rules().findResource("wood");
    CHECK(goldId>=0 && woodId>=0);
    Fixed goldPaid{},woodPaid{}; int deaths=0,rewards=0;
    for(const auto& e:sim->events()) if(e.subjectKey==key) {
        if(e.type==game::kEvDied) { ++deaths; CHECK(e.value==m.rules().findType("mob")); }
        if(e.type==game::kEvBounty) {
            ++rewards; CHECK(e.player==1 && e.subjectOwner==2);
            if(e.resource==goldId)goldPaid+=Fixed::fromRaw(e.value);
            if(e.resource==woodId)woodPaid+=Fixed::fromRaw(e.value);
        }
    }
    CHECK(deaths==1 && rewards==2 && goldPaid==3_fx && woodPaid==5_fx);
    CHECK(m.state(*sim).players[1].stock[goldId]==10_fx);
    CHECK(m.state(*sim).players[1].stock[woodId]==5_fx);
    sim->step();
    for(const auto& e:sim->events()) CHECK(e.type!=game::kEvBounty || e.subjectKey!=key);
}

TEST(progression_rules_parse_and_errors) {
    auto sim = makeSimJson(kProgRules, progMap(""));
    const auto& R = prog(*sim).rules();
    CHECK(prog(*sim).active() && R.defs.perks.size() == 10 && R.unitType.at(1) == mod(*sim).rules().findType("drone"));
    CHECK(R.bulletPerks.size() == 2 && R.ext.at(6).onHit >= 0);
    // 2 progression types x 1 projectile weapon x 3 non-empty subsets of {split, pierce}
    CHECK(R.variants.size() == 6 && R.variantFor(uint16_t(mod(*sim).rules().findType("drone")), 0, 3) >= 0);
    CHECK(mod(*sim).rules().advancedEffects);   // the on-hit payload switched the effect engine on
    auto throws = [&](const std::string& from, const std::string& to) {
        std::string r = kProgRules;
        r.replace(r.find(from), from.size(), to);
        try { makeSimJson(r, progMap("")); return false; } catch (const std::exception&) { return true; }
    };
    CHECK(throws("\"type\": \"gunbot\"", "\"type\": \"ghost\""));                     // unknown unit type
    CHECK(throws("\"players\": [1],", "\"players\": [1], \"typo\": 1,"));            // unknown key
    CHECK(throws("{\"kind\": \"flyThrough\", \"count\": 1}", "{\"kind\": \"bounce\", \"count\": 1}"));   // unknown bullet modifier
    CHECK(throws("\"bullet\": {\"modifiers\"", "\"bullet\": {\"radius\": 1, \"modifiers\""));          // only modifiers in a bullet block
    CHECK(throws("\"value\": 1500", "\"value\": \"x\""));                             // library reader errors
    CHECK(throws("\"killXp\": {\"mob\": 2}", "\"killXp\": {\"ghost\": 2}"));
    // games without the block: untouched, commands ignored
    auto plain = makeSimJson(R"({"types": {"u": {"health": {"max": 10}, "move": {"speed": 1}}, "core": {"structure": {"footprint": [1, 1]}, "health": {"max": 10}}}})", progMap(""));
    CHECK(mod(*plain).progression() && !mod(*plain).progression()->active() && !mod(*plain).progression()->store(*plain));
    progCmd(*plain, 1, game::kProgChoose, 0, 0);
    run(*plain, 0.2);
}

TEST(progression_kills_orbs_offer_recruit_and_summon_restore) {
    auto sim = makeSimJson(kProgRules, progMap(R"({"type":"drone","owner":1,"pos":[4.5,8.5]},
        {"type":"mob","owner":2,"pos":[6.5,8.5]},{"type":"mob","owner":2,"pos":[6.5,9.5]})"));
    Entity hero = prog(*sim).heroOf(*sim, 1);
    CHECK(hero.valid() && hero == findType(*sim, "drone", 1));
    CHECK(pp(*sim, 1).deck.slots.size() == 2 && pp(*sim, 1).level == 0);   // the automatic deck: hero + gunbot
    // the drone shoots both mobs; each kill drops a 2 xp orb that the hero picks up (radius 3)
    int orbs = 0, collected = 0, offers = 0;
    for (int i = 0; i < 80; ++i) {
        sim->step();
        for (auto& e : sim->events()) {
            if (e.type == game::kEvProgOrb && (e.value >> 40) == int64_t(game::ProgOrbEv::Spawned)) ++orbs;
            if (e.type == game::kEvProgOrb && (e.value >> 40) == int64_t(game::ProgOrbEv::Collected)) ++collected;
            if (e.type == game::kEvProgOffer && (e.value >> 40) == int64_t(game::ProgOfferEv::Opened)) ++offers;
        }
    }
    CHECK(countUnits(*sim, "mob", 2) == 0 && orbs == 2 && collected == 2 && offers == 1);
    CHECK(pp(*sim, 1).xp == 4 && pp(*sim, 1).offer.open && pp(*sim, 1).offer.items.size() == 1);
    CHECK(prog(*sim).store(*sim)->kills[1] == 2);
    // level 1 offers only the recruit card; a stale revision is ignored, the right one spawns the ally
    const uint32_t rev = pp(*sim, 1).offer.revision;
    progCmd(*sim, 1, game::kProgChoose, 0, rev + 7);
    run(*sim, 0.1);
    CHECK(pp(*sim, 1).offer.open && countUnits(*sim, "gunbot", 1) == 0);
    progCmd(*sim, 1, game::kProgChoose, 0, rev);
    bool recruited = false;
    for (int i = 0; i < 5; ++i) {
        sim->step();
        for (auto& e : sim->events()) if (e.type == game::kEvProgRecruit && e.player == 1 && e.value == 1) recruited = true;
    }
    CHECK(recruited && countUnits(*sim, "gunbot", 1) == 1 && pp(*sim, 1).level == 1 && pp(*sim, 1).xp == 1 && !pp(*sim, 1).offer.open);
    Entity ally = findType(*sim, "gunbot", 1);
    if (!ally.valid()) return;
    const game::ProgUnit* pu = prog(*sim).store(*sim)->find(ally);
    CHECK(pu && pu->slot == 1 && pu->bearer == game::kProgBearerAlly);
    // the ally dies; the hero stands in the summon circle (20, 8) for 20 ticks and gets it back there
    sim->world().get<game::Unit>(ally).hp = Fixed{};
    run(*sim, 0.1);
    CHECK(countUnits(*sim, "gunbot", 1) == 0);
    sim->world().get<Transform>(hero).pos = {Fixed::fromInt(20), Fixed{}, Fixed::fromInt(8)};
    bool restored = false;
    for (int i = 0; i < 30 && !restored; ++i) {
        sim->step();
        for (auto& e : sim->events()) if (e.type == game::kEvProgSummon && (e.value >> 32) == int64_t(aiverse::lib::mechanics::SummonEventKind::Confirmed)) restored = true;
    }
    CHECK(restored && countUnits(*sim, "gunbot", 1) == 1);
    Entity back = findType(*sim, "gunbot", 1);
    CHECK(back.valid() && (sim->world().get<Transform>(back).pos - Vec3{Fixed::fromInt(20), Fixed{}, Fixed::fromInt(8)}).xz().length() < Fixed::fromInt(2));
    CHECK(prog(*sim).store(*sim)->find(back) && prog(*sim).store(*sim)->find(back)->slot == 1);
}

TEST(progression_perk_stats_on_real_units) {
    auto sim = makeSimJson(kProgRules, progMap(R"({"type":"drone","owner":1,"pos":[4.5,8.5]},{"type":"drone","owner":2,"pos":[24.5,8.5]})"));
    auto& m = mod(*sim);
    Entity hero = prog(*sim).heroOf(*sim, 1), other = findType(*sim, "drone", 2);
    auto grant = [&](const char* key) { return prog(*sim).grantPerk(*sim, 1, perkId(*sim, key), -1); };
    CHECK(grant("damage") && m.stat(*sim, hero, game::Stat::Damage) == Fixed::fromRatio(3, 2));
    CHECK(grant("damage") && m.stat(*sim, hero, game::Stat::Damage) == Fixed::fromRatio(9, 4));   // sequential packages
    CHECK(grant("swift") && m.stat(*sim, hero, game::Stat::Speed) == Fixed::fromInt(5));
    CHECK(grant("rapid") && m.stat(*sim, hero, game::Stat::AttackSpeed) == Fixed::fromInt(2));     // half the fire interval
    // max health doubles and keeps the health fraction
    sim->world().get<game::Unit>(hero).hp = Fixed::fromInt(50);
    CHECK(grant("vitality") && m.stat(*sim, hero, game::Stat::MaxHp) == Fixed::fromInt(200) && sim->world().get<game::Unit>(hero).hp == Fixed::fromInt(100));
    // incoming damage x0.5 once "armor" is taken
    m.dealDamage(*sim, hero, Fixed::fromInt(20), 0, other, 2);
    CHECK(sim->world().get<game::Unit>(hero).hp == Fixed::fromInt(80));
    CHECK(grant("armor"));
    m.dealDamage(*sim, hero, Fixed::fromInt(20), 0, other, 2);
    CHECK(sim->world().get<game::Unit>(hero).hp == Fixed::fromInt(70));
    // regeneration 20 hp / s
    run(*sim, 0.5);
    CHECK(grant("regen"));
    const Fixed before = sim->world().get<game::Unit>(hero).hp;
    run(*sim, 1.0);
    CHECK(sim->world().get<game::Unit>(hero).hp >= before + Fixed::fromInt(15));
    // units outside the progression (player 2 takes no part) are untouched
    CHECK(m.stat(*sim, other, game::Stat::Damage) == Fixed::fromInt(1) && m.stat(*sim, other, game::Stat::MaxHp) == Fixed::fromInt(100));
    // pipeline explanation for tools / Lua
    CHECK(prog(*sim).statOf(*sim, hero, aiverse::lib::mechanics::perk_stat::OutgoingDamage, 1000) == 2250);
    CHECK(prog(*sim).statOf(*sim, other, aiverse::lib::mechanics::perk_stat::OutgoingDamage, 1000) == 1000);
}

TEST(progression_bullet_perks_and_on_hit_payloads) {
    auto sim = makeSimJson(kProgRules, progMap(R"({"type":"drone","owner":1,"pos":[4.5,8.5]},
        {"type":"tank","owner":2,"pos":[8.5,8.5]},{"type":"tank","owner":2,"pos":[10.5,8.5]},{"type":"tank","owner":2,"pos":[9.5,6.5]})"));
    auto& pg = prog(*sim);
    CHECK(pg.grantPerk(*sim, 1, perkId(*sim, "pierce"), -1) && pg.grantPerk(*sim, 1, perkId(*sim, "pierce"), -1));
    CHECK(pg.grantPerk(*sim, 1, perkId(*sim, "split_shot"), -1) && pg.grantPerk(*sim, 1, perkId(*sim, "burn"), -1));
    int variantShots = 0, maxPierce = 0, maxProjectiles = 0;
    bool burning = false;
    const int burn = mod(*sim).rules().findStatus("burn");
    for (int i = 0; i < 60; ++i) {
        sim->step();
        int n = 0;
        sim->world().each<game::Projectile>([&](Entity, game::Projectile& p) {
            ++n;
            if (p.weapon > -2) return;
            ++variantShots;
            const auto* f = pg.variantFlight(p.weapon);
            if (f && f->modifiers.size() == 2 && f->modifiers[0].kind == projectile::ProjectileModKind::Split)
                maxPierce = std::max<int>(maxPierce, p.program.remaining[1]);
        });
        maxProjectiles = std::max(maxProjectiles, n);
        sim->world().each<game::Statuses, game::Unit>([&](Entity, game::Statuses& st, game::Unit&) { for (auto& in : st.list) if (in.def == burn) burning = true; });
    }
    CHECK(variantShots > 0 && maxPierce == 2);   // two pierce picks: two fly-throughs on one shot
    CHECK(maxProjectiles >= 3);                   // the split made children
    CHECK(burning);                                // the perk's on-hit payload set the targets on fire
}

TEST(progression_bot_choices_determinism_save_rollback_and_lua) {
    std::string rules = kProgRules;
    rules.replace(rules.find("\"pickups\": {\"radius\": 3}"), 24, "\"pickups\": {\"radius\": 12}");   // the hero shoots from range
    // Lua: the perk hook and the events see the choices (simulation script), the veto keeps "swift" out of offers
    rules.replace(rules.find("\"victory\""), 9, R"("scripts": ["prog.lua"], "victory")");
    const char* lua = R"L(
state.hooks = 0
state.offers = 0
function on_burn_perk(e) state.hooks = state.hooks + 1 end
function progression_can_offer(player, key) return key ~= "swift" end
on("progression_offer", function(e) state.offers = state.offers + 1 end)
)L";
    std::string map = progMap(R"({"type":"drone","owner":1,"pos":[4.5,8.5]},{"type":"drone","owner":2,"pos":[27.5,12.5]})");
    for (int i = 0; i < 12; ++i) map.insert(map.rfind(']'), ",{\"type\":\"mob\",\"owner\":2,\"pos\":[" + std::to_string(8 + (i % 6) * 2) + ".5," + std::to_string(6 + (i / 6) * 4) + ".5]}");
    Json rj = Json::parse(rules);
    rj.member("$scriptSources").set("prog.lua", Json::string(lua));
    auto make = [&](uint64_t seed) {
        SimConfig cfg; cfg.seed = seed;
        auto s = std::make_unique<Simulation>(cfg, std::make_unique<game::GameCore>());
        s->setup(rj, Json::parse(map));
        return s;
    };
    auto play = [&](Simulation& sim, int ticks, std::vector<uint64_t>* sums) {
        game::ComputerPlayer a(1, 7);
        for (int i = 0; i < ticks; ++i) {
            a.update(sim);
            sim.step();
            if (sums && i % 50 == 0) sums->push_back(sim.checksum());
        }
    };
    auto s1 = make(5), s2 = make(5);
    std::vector<uint64_t> c1, c2;
    play(*s1, 1200, &c1);
    play(*s2, 1200, &c2);
    CHECK(c1 == c2);
    // the bot took the recruit and perks (every level it banked was spent)
    CHECK(pp(*s1, 1).level >= 2);
    CHECK(countUnits(*s1, "gunbot", 1) + int(prog(*s1).store(*s1)->summons.allies.size()) >= 1);
    for (auto& o : pp(*s1, 1).mods) CHECK(o.perk != perkId(*s1, "swift"));   // vetoed by progression_can_offer
    // save state: a loaded copy plays on bit-identically
    std::string save = s1->serialize();
    auto s3 = make(5);
    std::string err;
    CHECK(s3->deserialize(save, &err));
    CHECK(s3->checksum() == s1->checksum());
    CHECK(pp(*s3, 1).level == pp(*s1, 1).level && pp(*s3, 1).mods.size() == pp(*s1, 1).mods.size());
    prog(*s1).grantXp(*s1, 1, 40, false); prog(*s3).grantXp(*s3, 1, 40, false);
    play(*s1, 200, nullptr); play(*s3, 200, nullptr);
    CHECK(s3->checksum() == s1->checksum());
    // rollback snapshot: restore, replay the same commands, same state
    SimSnapshot snap;
    s1->saveState(snap);
    prog(*s1).grantXp(*s1, 1, 40, false);
    play(*s1, 100, nullptr);
    const uint64_t after = s1->checksum();
    const auto level = pp(*s1, 1).level;
    s1->loadState(snap);
    prog(*s1).grantXp(*s1, 1, 40, false);
    play(*s1, 100, nullptr);
    CHECK(s1->checksum() == after && pp(*s1, 1).level == level);
    // a different seed draws different offers but stays valid
    auto s4 = make(6);
    play(*s4, 600, nullptr);
    CHECK(aiverse::lib::mechanics::valid(prog(*s4).store(*s4)->perks));
}

TEST(progression_lua_api_and_view_cards) {
    std::string rules = kProgRules;
    rules.replace(rules.find("\"victory\""), 9, R"("scripts": ["prog.lua"], "victory")");
    const char* lua = R"L(
state.log = {}
on("progression_offer", function(e)
  local o = progression.offer(e.player)
  if o then state.cards = #o.cards; state.kind = o.cards[1].kind; state.unit = o.cards[1].unit end
end)
on("progression_recruit", function(e) state.recruited = e.value end)
)L";
    Json rj = Json::parse(rules);
    rj.member("$scriptSources").set("prog.lua", Json::string(lua));
    SimConfig cfg; cfg.seed = 2;
    auto sim = std::make_unique<Simulation>(cfg, std::make_unique<game::GameCore>());
    sim->setup(rj, Json::parse(progMap(R"({"type":"drone","owner":1,"pos":[4.5,8.5]})")));
    CHECK(prog(*sim).grantXp(*sim, 1, 3, false));
    run(*sim, 0.1);
    // the offer event reached Lua with the card list
    auto* ss = sim->world().resource<game::ScriptStore>();
    mod(*sim).scripts()->flush(*sim);
    CHECK(ss && ss->state.find("recruit") != std::string::npos);   // state.kind = "recruit"
    CHECK(ss && ss->state.find("gunbot") != std::string::npos);    // state.unit
    // game.command from Lua: {type = "progression", op = "choose", card = 1, revision = r}
    const uint32_t rev = pp(*sim, 1).offer.revision;
    Command c; c.player = 1; c.type = game::kCmdProgression; c.param = game::progressionParam(game::kProgChoose, 0, rev);
    sim->submit(c);
    run(*sim, 0.5);
    mod(*sim).scripts()->flush(*sim);
    CHECK(countUnits(*sim, "gunbot", 1) == 1 && pp(*sim, 1).level == 1);
    CHECK(ss->state.find("recruit") != std::string::npos);
}

TEST(progression_offers_at_summon_circles) {
    std::string rules = kProgRules;
    rules.replace(rules.find("\"players\": [1],"), 15, "\"players\": [1], \"offerAt\": \"summonPoints\",");
    auto sim = makeSimJson(rules, progMap(R"({"type":"drone","owner":1,"pos":[4.5,8.5]})"));
    Entity hero = prog(*sim).heroOf(*sim, 1);
    CHECK(prog(*sim).grantXp(*sim, 1, 3, false));
    run(*sim, 0.3);
    CHECK(!pp(*sim, 1).offer.open && !prog(*sim).atCircle(*sim, 1));   // banked: no cards away from a circle
    progCmd(*sim, 1, game::kProgOpen, 0, 0);                              // and no forced open either
    run(*sim, 0.3);
    CHECK(!pp(*sim, 1).offer.open);
    sim->world().get<Transform>(hero).pos = {Fixed::fromInt(20), Fixed{}, Fixed::fromInt(8)};
    run(*sim, 0.2);
    CHECK(prog(*sim).atCircle(*sim, 1) && pp(*sim, 1).offer.open);
    sim->world().get<Transform>(hero).pos = {Fixed::fromInt(4), Fixed{}, Fixed::fromInt(8)};
    run(*sim, 0.2);
    CHECK(!pp(*sim, 1).offer.open && pp(*sim, 1).xp == 3 && pp(*sim, 1).level == 0);   // walking away keeps the xp
}

// A computer-steered top-down hero (agent-df-hero-net) walks over xp orbs nearby and, with a level banked, to a summon
// circle (offerAt "summonPoints"), where the cards open and the bot picks one.
TEST(progression_bot_hero_collects_orbs_and_walks_to_a_circle) {
    std::string rules = kProgRules;
    rules.replace(rules.find("\"players\": [1],"), 15, "\"players\": [1], \"offerAt\": \"summonPoints\",");
    rules.replace(rules.find("\"victory\": \"noStructures\","), 26, "\"victory\": \"noStructures\", \"topDown\": {\"heroes\": [\"drone\"], \"bot\": {\"progression\": true}},");
    auto sim = makeSimJson(rules, progMap(R"({"type":"drone","owner":1,"pos":[4.5,8.5]})"));
    CHECK(mod(*sim).topDown()->active() && mod(*sim).topDown()->heroOf(*sim, 1).valid());
    game::ComputerPlayer bot(1, 3);
    auto play = [&](double s) { for (int i = 0; i < int(s * 20); ++i) { bot.update(*sim); sim->step(); } };
    // an orb 6 units away: the hero walks over it
    prog(*sim).store(*sim)->orbQueue.push_back({1, 2, {Fixed::fromRatio(105, 10), Fixed{}, Fixed::fromRatio(85, 10)}});
    play(4.0);
    CHECK(pp(*sim, 1).xp + int64_t(pp(*sim, 1).level) * 3 >= 2);
    // a level banked: off to the circle at (20, 8); the cards open there and the bot chooses
    CHECK(prog(*sim).grantXp(*sim, 1, 3, false));
    for (int i = 0; i < 20 * 8 && pp(*sim, 1).level < 1; ++i) { bot.update(*sim); sim->step(); }
    CHECK(pp(*sim, 1).level >= 1);
    const Vec3 at = sim->world().get<Transform>(prog(*sim).heroOf(*sim, 1)).pos;   // where the card was picked
    CHECK((at - Vec3{Fixed::fromInt(20), Fixed{}, Fixed::fromInt(8)}).xz().length() < Fixed::fromInt(3));
}

// fireCount (bullets per shot), an "everyNth" perk (its on-hit only on every second shot), a knockback on-hit
// payload, and "pauseOnOffer" (the battle holds while an offer is open; the choice still reaches it).
TEST(progression_fire_count_every_nth_knockback_and_pause) {
    std::string rules = kProgRules;
    rules.replace(rules.find(R"(      {"id": 10, "key": "magnet")"), 0,
        R"(      {"id": 11, "key": "double", "effects": [{"stat": "fireCount", "op": "add", "value": 1000}]},
      {"id": 12, "key": "push", "everyNth": 2, "onHit": [{"type": "knockback", "amount": 1, "fromCaster": true}]},
)");
    rules.replace(rules.find(R"("tank": {"health")"), 0, R"("walker": {"health": {"max": 100000}, "move": {"speed": 1, "radius": 0.4}, "sight": 2},
    )");
    auto sim = makeSimJson(rules, progMap(R"({"type":"drone","owner":1,"pos":[4.5,8.5]},{"type":"walker","owner":2,"pos":[8.5,8.5]})"));
    auto& pg = prog(*sim);
    CHECK(pg.rules().anyNth && pg.rules().ext.at(12).everyNth == 2);
    const Entity hero = pg.heroOf(*sim, 1), walker = findType(*sim, "walker", 2);
    CHECK(pg.fireCount(*sim, hero) == 1);
    CHECK(pg.grantPerk(*sim, 1, perkId(*sim, "double"), -1) && pg.fireCount(*sim, hero) == 2);
    CHECK(pg.grantPerk(*sim, 1, perkId(*sim, "push"), -1));
    std::set<uint64_t> seen;
    int attacks = 0, bullets = 0, pushing = 0;
    const Fixed x0 = sim->world().get<Transform>(walker).pos.x;
    for (int i = 0; i < 80; ++i) {
        sim->step();
        for (auto& e : sim->events()) if (e.type == game::kEvAttack && e.a == hero) ++attacks;
        sim->world().each<game::Projectile>([&](Entity p, game::Projectile& pr) {
            if (pr.source != hero || !seen.insert(p.packed()).second) return;
            ++bullets;
            if (pr.onHit >= 0) ++pushing;
        });
    }
    CHECK(attacks >= 4 && bullets == 2 * attacks);                    // two bullets per shot
    CHECK(pushing >= bullets / 2 - 2 && pushing <= bullets / 2 + 2);   // the on-hit rides every second shot
    CHECK(sim->world().get<Transform>(walker).pos.x > x0 + Fixed::fromInt(1));   // knocked back (it walks at 1 cell / s toward nothing)
    // errors: everyNth needs a bullet or an on-hit
    std::string bad = rules;
    bad.replace(bad.find(R"("key": "double",)"), 16, R"("key": "double", "everyNth": 3,)");
    bool threw = false;
    try { makeSimJson(bad, progMap("")); } catch (const std::exception& e) { threw = std::string(e.what()).find("everyNth") != std::string::npos; }
    CHECK(threw);
    // pauseOnOffer: the battle holds while the cards are open
    std::string paused = rules;
    paused.replace(paused.find(R"("players": [1],)"), 15, R"("players": [1], "pauseOnOffer": true,)");
    auto ps = makeSimJson(paused, progMap(R"({"type":"drone","owner":1,"pos":[4.5,8.5]},{"type":"walker","owner":2,"pos":[20.5,3.5]})"));
    auto& pp2 = prog(*ps);
    const Entity w2 = findType(*ps, "walker", 2);
    Command mv; mv.player = 2; mv.type = game::kCmdMove; mv.units = {w2}; mv.point = {Fixed::fromInt(28), Fixed{}, Fixed::fromRatio(35, 10)};
    ps->submit(mv);
    run(*ps, 0.5);
    const Fixed before = ps->world().get<Transform>(w2).pos.x;
    CHECK(before > Fixed::fromRatio(205, 10));
    CHECK(pp2.grantXp(*ps, 1, 3, false));
    run(*ps, 0.2);
    CHECK(pp(*ps, 1).offer.open && pp2.pausesGameplay(*ps));
    const Fixed held = ps->world().get<Transform>(w2).pos.x;
    run(*ps, 1.0);
    CHECK(ps->world().get<Transform>(w2).pos.x == held);   // nothing moves
    progCmd(*ps, 1, game::kProgChoose, 0, pp(*ps, 1).offer.revision);
    run(*ps, 1.0);
    CHECK(!pp(*ps, 1).offer.open && ps->world().get<Transform>(w2).pos.x > held);
}
