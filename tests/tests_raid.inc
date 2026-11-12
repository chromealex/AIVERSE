// Extraction raids (RtsRaid.h, docs/raid.md): the sample's bot extracts, points / channels / contest / interrupts,
// interactables and loot, death drops, Lua API and veto, receipts, saves and rollback, rule errors.
#include "modules/rts/RtsRaid.h"
#include "modules/rts/RtsTopDown.h"

namespace {
const std::string kRaidDir = std::string(AIVERSE_SOURCE_DIR) + "/templates/extraction";

std::unique_ptr<Simulation> raidSample(const std::string& mapFile, uint64_t seed) {
    Json rules = Json::parseFile(kRaidDir + "/rules.json");
    Json map = Json::parseFile(kRaidDir + "/maps/" + mapFile);
    std::string err;
    game::embedScripts(rules, map, [&](const std::string& p, std::string& out) {
        std::ifstream f(kRaidDir + "/" + p, std::ios::binary);
        if (!f) return false;
        out.assign(std::istreambuf_iterator<char>(f), {});
        return true;
    }, &err);
    SimConfig cfg; cfg.seed = seed;
    auto sim = std::make_unique<Simulation>(cfg, std::make_unique<game::RtsModule>());
    sim->setup(rules, map);
    return sim;
}
int runBots(Simulation& sim, int seconds) {
    std::vector<game::RtsAI> bots;
    for (auto& p : sim.players()) bots.emplace_back(p.slot, sim.config().seed);
    const int limit = seconds * int(sim.tickRate());
    while (sim.result() == 0 && int(sim.tick()) < limit) { for (auto& b : bots) b.update(sim); sim.step(); }
    return sim.result();
}
game::RaidStore& raidStore(Simulation& sim) { return *sim.world().resource<game::RaidStore>(); }

// a small synthetic raid: two raiders, one point, a crate
const char* kRaidRules = R"J({
  "resources": {"coins": {"start": 0}},
  "items": {"gem": {"cost": {"coins": 30}}, "rock": {}},
  "types": {
    "runner": {"health": {"max": 100}, "move": {"speed": 3, "radius": 0.4}, "weapons": [{"range": 3, "damage": 10, "cooldown": 1}], "inventory": {"slots": 4}},
    "box": {"structure": {"footprint": [1, 1]}, "health": {"max": 10}, "invulnerable": true}
  },
  "raid": {
    "mode": "pvp", "duration": 120, "raiders": ["runner"], "pickupRadius": 2,
    "loot": {"box": {"entries": [{"item": "gem", "count": 2}]}},
    "interact": {"box": {"hold": 1, "range": 1, "loot": "box"}},
    "extraction": {"radius": 2, "channel": 2},
    "points": [{"id": "a", "pos": [20.5, 8.5], "opensAt": 1, "open": 6, "sleep": 4, "departure": 1}],
    "objectives": [{"id": "gems", "kind": "collect", "item": "gem", "count": 2, "reward": {"bonus": 7}}]
  }
})J";
std::string raidMap(const std::string& entities) {
    return flatMap(32, 16, R"(,"entities":[)" + entities + "]");
}
Command raidCmd(uint8_t player, uint16_t type, std::vector<Entity> units = {}, Vec3 p = {}, int64_t param = 0) {
    Command c; c.player = player; c.type = type; c.units = std::move(units); c.point = p; c.param = param;
    return c;
}
Vec3 cellAt(double x, double z) { return {Fixed::fromRaw(int64_t(x * 65536)), Fixed{}, Fixed::fromRaw(int64_t(z * 65536))}; }
} // namespace

TEST(raid_sample_bot_extracts_and_receipt) {
    auto sim = raidSample("salvage.json", 3);
    auto& m = mod(*sim);
    CHECK(m.raid() && m.raid()->active());
    const int res = runBots(*sim, 320);
    auto& st = raidStore(*sim);
    CHECK(res == 1);   // the raider's team won
    CHECK(st.phase == uint8_t(game::RaidPhase::Over) && st.players.size() == 1);
    const auto& p = st.players[0];
    CHECK(p.status == uint8_t(game::RaidStatus::Extracted));
    const Json r = Json::parse(st.receipts[0]);
    std::printf("  receipt: %s\n", st.receipts[0].c_str());
    CHECK(r.getStr("outcome") == "extracted" && r.getBool("final") && r.getBool("winner"));
    CHECK(r["objectives"]["rescue"].getBool("done"));
    CHECK(r["secured"]["data_core"].integer() == 1 && r.getBool("completed"));
    CHECK(r["reward"]["data_core"].integer() == 1 && r["reward"]["robot_parts"].integer() == 1 && r["reward"]["coins"].integer() >= 50);
    CHECK(r.getStr("id").rfind("raid-", 0) == 0 && r.getInt("value") > 0);
    // same seed: same match, same receipt (operation id included); another seed: another operation id
    auto again = raidSample("salvage.json", 3);
    runBots(*again, 320);
    CHECK(raidStore(*again).receipts[0] == st.receipts[0] && again->checksum() == sim->checksum());
    auto other = raidSample("salvage.json", 4);
    runBots(*other, 320);
    CHECK(Json::parse(raidStore(*other).receipts[0]).getStr("id") != r.getStr("id"));
    // the Lua hooks ran: robot freed, extraction recorded
    std::string state = sim->world().resource<game::ScriptStore>()->state;
    CHECK(state.find("freed") != std::string::npos);
}

TEST(raid_pvp_sample_finishes) {
    auto sim = raidSample("crossfire.json", 2);
    const int res = runBots(*sim, 260);
    auto& st = raidStore(*sim);
    CHECK(st.phase == uint8_t(game::RaidPhase::Over));
    CHECK(res != 0);
    int out = 0;
    for (auto& p : st.players) out += p.status == uint8_t(game::RaidStatus::Extracted);
    std::printf("  pvp: result %d, extracted %d, receipts %s | %s\n", res, out, st.receipts[0].c_str(), st.receipts[1].c_str());
    CHECK(out >= 1);
}

TEST(raid_point_cycle_channel_contest_interrupt) {
    auto sim = makeSimJson(kRaidRules, raidMap(R"({"type":"runner","owner":1,"pos":[19.5,8.5]},{"type":"runner","owner":2,"pos":[4.5,4.5]})"));
    auto& st = raidStore(*sim);
    CHECK(st.points.size() == 1 && st.points[0].phase == uint8_t(game::RaidPointPhase::Locked));
    run(*sim, 1.2);
    CHECK(st.points[0].phase == uint8_t(game::RaidPointPhase::Open));
    run(*sim, 1);
    CHECK(st.channels.size() == 1 && st.channels[0].player == 1 && st.channels[0].progress.raw > 0);
    // a hostile raider walks in: the channel is blocked (contest "block")
    Entity r2 = findType(*sim, "runner", 2);
    sim->world().get<Transform>(r2).pos = cellAt(21, 9);
    const Fixed before = st.channels[0].progress;
    run(*sim, 0.5);
    CHECK(raidStore(*sim).channels[0].progress == before);
    // damage interrupts (interruptOnDamage default true) - the hostile raider shoots
    CHECK(raidStore(*sim).players[0].status == uint8_t(game::RaidStatus::InRaid));
    sim->world().get<Transform>(r2).pos = cellAt(4.5, 4.5);
    Entity r1 = findType(*sim, "runner", 1);
    sim->world().get<game::Unit>(r1).hp = sim->world().get<game::Unit>(r1).hp - Fixed::fromInt(10);
    run(*sim, 0.1);
    CHECK(raidStore(*sim).channels.empty() || raidStore(*sim).channels[0].progress < Fixed::fromRatio(1, 5));
    run(*sim, 2.5);
    auto& p1 = *raidStore(*sim).find(1);
    CHECK(p1.status == uint8_t(game::RaidStatus::Extracted) && p1.point == 0);
    CHECK(countType(*sim, "runner", 1) == 0);
    // the window closes, departs, sleeps and opens again
    bool sawSleep = false, reopened = false;
    for (int i = 0; i < 20 * 14; ++i) {
        sim->step();
        const uint8_t ph = raidStore(*sim).points[0].phase;
        sawSleep |= ph == uint8_t(game::RaidPointPhase::Sleep);
        reopened |= sawSleep && ph == uint8_t(game::RaidPointPhase::Open);
    }
    CHECK(sawSleep && reopened);
}

TEST(raid_crate_loot_pickup_objective_and_death_drop) {
    auto sim = makeSimJson(kRaidRules, raidMap(R"({"type":"runner","owner":1,"pos":[6.5,8.5]},{"type":"runner","owner":2,"pos":[28.5,2.5]},{"type":"box","owner":0,"pos":[7.5,8.5]})"));
    run(*sim, 0.2);
    CHECK(countType(*sim, "box", 0) == 1);
    run(*sim, 1.6);   // hold 1 s: opened, removed, 2 gems on the ground, picked up
    CHECK(countType(*sim, "box", 0) == 0);
    run(*sim, 0.5);
    Entity r1 = findType(*sim, "runner", 1);
    auto& bag = sim->world().get<game::ItemBag>(r1);
    int gems = 0;
    for (auto it : bag.items) gems += it == 0;
    CHECK(gems == 2);
    auto& st = raidStore(*sim);
    CHECK(st.find(1)->objDone[0] == 1);   // collect objective: carried counts
    // death: the bag drops (loseOnDeath "drop"), the player is out, the receipt lists the loss
    sim->world().get<game::Unit>(r1).hp = Fixed{};
    sim->world().get<game::Unit>(r1).lastAttacker = 2;
    run(*sim, 0.1);
    auto& p1 = *raidStore(*sim).find(1);
    CHECK(p1.status == uint8_t(game::RaidStatus::Dead) && p1.killer == 2);
    CHECK(mod(*sim).state(*sim).mech.groundItems.size() == 2);
    const Json r = Json::parse(mod(*sim).raid()->receiptJson(*sim, p1));
    CHECK(r.getStr("outcome") == "dead" && r["lost"]["gem"].integer() == 2 && r["reward"].size() == 0);
    // the other raider takes the loot
    Entity r2 = findType(*sim, "runner", 2);
    sim->world().get<Transform>(r2).pos = cellAt(6.5, 8.5);
    run(*sim, 0.3);
    int g2 = 0;
    for (auto it : sim->world().get<game::ItemBag>(r2).items) g2 += it == 0;
    CHECK(g2 == 2);
    // extract it by Lua-free force and check secured + reward
    CHECK(mod(*sim).raid()->extract(*sim, 2, 0));
    run(*sim, 0.1);
    auto& p2 = *raidStore(*sim).find(2);
    CHECK(raidStore(*sim).phase == uint8_t(game::RaidPhase::Over));
    const Json r2j = Json::parse(raidStore(*sim).receipts[1]);
    CHECK(r2j.getStr("outcome") == "extracted" && r2j["secured"]["gem"].integer() == 2 && r2j.getInt("value") == 60);
    CHECK(r2j["reward"]["gem"].integer() == 2 && r2j["reward"]["bonus"].integer() == 7 && r2j.getBool("winner"));
    CHECK(sim->result() == 2 && p2.status == uint8_t(game::RaidStatus::Extracted));
}

TEST(raid_lua_api_veto_and_commands) {
    std::string rules = kRaidRules;
    rules.insert(rules.rfind('}'), R"X(, "scripts": ["s.lua"], "$scriptSources": {"s.lua":
      "function raid_can_extract(player, point) return state.allow == true, 'not yet' end\n on_start = function() state.allow = false end\n on('raid_channel', function(e) state.ch = (state.ch or 0) + 1 end)\n every(2, function() if raid.phase() == 'running' then state.allow = true; raid.complete(1, 'gems') end end)"})X");
    auto sim = makeSimJson(rules, raidMap(R"({"type":"runner","owner":1,"pos":[20.5,8.5]},{"type":"runner","owner":2,"pos":[4.5,4.5]})"));
    run(*sim, 3.5);   // channel full at ~3 s, refused until state.allow (set at 2 s, so the second try passes)
    auto* p1 = raidStore(*sim).find(1);
    CHECK(p1->objDone[0] == 1);
    run(*sim, 3);
    CHECK(raidStore(*sim).find(1)->status == uint8_t(game::RaidStatus::Extracted));
    // abandon command: the other raider leaves, the raid ends, team 1 wins
    sim->submit(raidCmd(2, game::kCmdRaid, {}, {}, game::kRaidAbandon));
    run(*sim, 0.2);
    CHECK(raidStore(*sim).find(2)->status == uint8_t(game::RaidStatus::Abandoned));
    CHECK(raidStore(*sim).phase == uint8_t(game::RaidPhase::Over) && sim->result() == 1);
    sim->checksum();   // flushes the Lua state into the world
    const std::string st = sim->world().resource<game::ScriptStore>()->state;
    CHECK(st.find("ch") != std::string::npos);
}

TEST(raid_save_state_and_rollback) {
    auto sim = raidSample("salvage.json", 5);
    std::vector<game::RtsAI> bots;
    for (auto& p : sim->players()) bots.emplace_back(p.slot, 5);
    for (int i = 0; i < 20 * 60; ++i) { for (auto& b : bots) b.update(*sim); sim->step(); }
    const std::string bytes = sim->serialize();
    CHECK(!bytes.empty());
    auto copy = raidSample("salvage.json", 5);
    std::string err;
    CHECK(copy->deserialize(bytes, &err));
    SimSnapshot snap;
    sim->saveState(snap);
    const uint64_t h0 = sim->checksum();
    bool same = true;
    for (int i = 0; i < 200; ++i) { sim->step(); copy->step(); same &= sim->checksum() == copy->checksum(); }
    CHECK(same);
    sim->loadState(snap);
    CHECK(sim->checksum() == h0);
}

TEST(raid_rules_errors_and_untouched_games) {
    auto bad = [](const std::string& raid) {
        std::string r = std::string(R"({"items": {"gem": {}}, "types": {"runner": {"inventory": 2}}, "raid": )") + raid + "}";
        try { game::loadRules(Json::parse(r)); } catch (const std::exception& e) { return std::string(e.what()); }
        return std::string();
    };
    CHECK(bad(R"({"loot": {"t": [{"item": "nope"}]}})").find("unknown item 'nope'") != std::string::npos);
    CHECK(bad(R"({"points": [{"id": "p", "requires": ["x"]}]})").find("unknown objective 'x'") != std::string::npos);
    CHECK(bad(R"({"points": [{"id": "p", "follow": true}]})").find("call") != std::string::npos);
    CHECK(bad(R"({"interact": {"runner": {"loot": "none"}}})").find("unknown loot table") != std::string::npos);
    CHECK(bad(R"({"raiders": ["ghost"]})").find("unknown type 'ghost'") != std::string::npos);
    CHECK(bad(R"({})").empty());
    // a game without "raid" has no raid system and no store
    auto sim = makeSimJson(R"({"types": {"u": {"health": {"max": 10}}}})", flatMap(8, 8, R"(,"entities":[{"type":"u","owner":1,"pos":[2,2]}])"));
    CHECK(!mod(*sim).raid()->active() && !sim->world().resource<game::RaidStore>());
    bool hasRaidSystem = false;
    for (size_t i = 0; i < sim->systemCount(); ++i) hasRaidSystem |= sim->systemName(i) == "raid";
    CHECK(!hasRaidSystem);
}

// A top-down hero as the raider (agent-df-hero-net): it ignores move orders, so the raid bot steers its stick
// (TopDown::steerCommand -> kCmdTopDown) around the wall to the extraction point and channels there.
TEST(raid_bot_steers_a_topdown_hero_to_extraction) {
    const std::string rules = R"J({
      "resources": {"coins": {"start": 0}},
      "topDown": {"heroes": ["runner"], "move": {"accel": 0, "decel": 0}},
      "types": {
        "runner": {"health": {"max": 100}, "move": {"speed": 3, "radius": 0.4}, "weapons": [{"range": 3, "damage": 10, "cooldown": 1}]}
      },
      "raid": {
        "mode": "pve", "duration": 120, "raiders": ["runner"],
        "extraction": {"radius": 2, "channel": 2},
        "points": [{"id": "a", "pos": [24.5, 8.5], "opensAt": 1, "open": 60, "sleep": 4, "departure": 1}]
      }
    })J";
    // a wall at x = 14 (rows 3..15) between the hero and the point: the straight line is blocked
    std::string rows;
    for (int y = 0; y < 16; ++y) {
        std::string r(32, '.');
        if (y >= 3) r[14] = '#';
        rows += std::string(y ? "," : "") + "\"" + r + "\"";
    }
    const std::string map = R"({"size":[32,16],"players":[{"name":"A","team":1}],"terrain":{"rows":[)" + rows +
                            R"(]},"entities":[{"type":"runner","owner":1,"pos":[4.5,8.5]}]})";
    auto sim = makeSimJson(rules, map, 2);
    auto* td = mod(*sim).topDown();
    CHECK(td && td->active() && td->heroOf(*sim, 1).valid());
    runBots(*sim, 60);
    auto& st = raidStore(*sim);
    CHECK(!st.players.empty() && st.players[0].status == uint8_t(game::RaidStatus::Extracted));
    // it walked (stick input), not teleported: the hero's pilot saw kCmdTopDown commands
    CHECK(td->store(*sim)->pilots[1].inputTick > 0);
}
