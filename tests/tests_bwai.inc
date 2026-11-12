// agent-bw-ai: Bunker Wars computer player (rules ai.controller "bw", modules/rts/RtsBwAI*)
#include "modules/rts/RtsBwAI.h"
static const std::string kBwaiShroom = std::string(AIVERSE_SOURCE_DIR) + "/templates/shroomwars";

static Json bwaiRules(const std::string& bw = "") {
    Json r = Json::parseFile(kBwaiShroom + "/rules.json");
    r.set("ai", Json::parse(std::string(R"({"controller":"bw")") + (bw.empty() ? "" : ",\"bw\":" + bw) + "}"));
    return r;
}
static Json bwaiMap(const std::string& p1 = "", const std::string& p2 = "") {
    Json m = Json::parseFile(kBwaiShroom + "/maps/twin_valleys.json");
    Json* players = m.find("players");
    if (!p1.empty()) players->at(0).set("bot", Json::string(p1));
    if (!p2.empty()) players->at(1).set("bot", Json::string(p2));
    return m;
}
static std::unique_ptr<Simulation> bwaiSim(const Json& rules, const Json& map, uint64_t seed) {
    SimConfig cfg; cfg.seed = seed;
    auto sim = std::make_unique<Simulation>(cfg, std::make_unique<game::RtsModule>());
    sim->setup(rules, map);
    return sim;
}
static bool bwaiThrows(const Json& rules, const Json& map, const char* needle) {
    try { bwaiSim(rules, map, 1); } catch (const std::exception& e) { return std::string(e.what()).find(needle) != std::string::npos; }
    return false;
}
struct BwaiMatch { int result = 0; uint32_t ticks = 0; size_t commands = 0, sends = 0, upgrades = 0, skills = 0; std::vector<uint64_t> sums; };
static BwaiMatch bwaiMatch(const Json& rules, const Json& map, uint64_t seed, uint32_t maxTicks) {
    auto sim = bwaiSim(rules, map, seed);
    std::vector<game::RtsAI> bots;
    for (auto& p : sim->players()) bots.emplace_back(p.slot, seed);
    BwaiMatch r;
    while (sim->result() == 0 && sim->tick() < maxTicks) {
        for (auto& b : bots) b.update(*sim);
        sim->step();
        if (sim->tick() % 200 == 0) r.sums.push_back(sim->checksum());
    }
    r.result = sim->result(); r.ticks = sim->tick(); r.commands = sim->executedCommands().size();
    for (auto& c : sim->executedCommands()) {
        r.sends += c.type == game::kCmdSend; r.upgrades += c.type == game::kCmdUpgrade; r.skills += c.type == game::kCmdPlayerAbility;
    }
    r.sums.push_back(sim->checksum());
    return r;
}

TEST(bwai_config) {
    CHECK(game::loadRules(bwaiRules()).bwAI);
    CHECK(game::loadRules(bwaiRules(R"({"difficulty":"expert","players":{"2":"easy"},"skills":{"freeze":"Freeze"},"profiles":{"boss":{"base":"expert","minGroupSize":8}}})")).bwAI);
    auto fails = [](const Json& r, const char* needle) {
        try { game::loadRules(r); } catch (const std::exception& e) { return std::string(e.what()).find(needle) != std::string::npos; }
        return false;
    };
    Json noController = Json::parseFile(kBwaiShroom + "/rules.json");
    noController.set("ai", Json::parse(R"({"bw":{}})"));
    CHECK(fails(noController, "ai.bw"));
    CHECK(fails(bwaiRules(R"({"typo":1})"), "unknown key 'typo'"));
    CHECK(fails(bwaiRules(R"({"difficulty":"godlike"})"), "unknown profile 'godlike'"));
    CHECK(fails(bwaiRules(R"({"skills":{"freeze":"Nope"}})"), "unknown bot skill"));
    CHECK(fails(bwaiRules(R"({"skills":{"nope":"Freeze"}})"), "unknown player ability 'nope'"));
    CHECK(fails(bwaiRules(R"({"profiles":{"x":{"algorithm":"neural"}}})"), "unknown algorithm"));
    CHECK(bwaiThrows(bwaiRules(), bwaiMap("impossible"), "unknown profile 'impossible'"));
    Json m = bwaiMap();
    m.set("ai", Json::parse(R"({"triggers":[{"elements":[{"conditions":[{"type":"timer","name":"t"}],"actions":[{"type":"setMorale"}]}]}]})"));
    CHECK(bwaiThrows(bwaiRules(), m, "unknown action"));
    m.set("ai", Json::parse(R"({"debuts":[{"actions":[{"tick":5,"send":[0,99,3]}]}],"triggers":[{"elements":[{"conditions":[{"type":"time","cmp":">=","value":2}],"actions":[{"type":"botAttack","targets":[3]}]}]}]})"));
    CHECK(!bwaiThrows(bwaiRules(), m, "ai"));
    // the built-in profiles carry the BW asset values
    auto sim = bwaiSim(bwaiRules(), bwaiMap(), 1);
    const auto& cfg = *mod(*sim).bwAI();
    CHECK(cfg.profiles.at("easy").maxDelay == Fixed::fromInt(12) && cfg.profiles.at("expert").structuredAttacks);
    CHECK(cfg.profiles.at("hard").attackDistanceSafety == Fixed::fromRatio(3, 4) && cfg.profileFor(2).name == "normal");
}

TEST(bwai_send_exact_count) {
    auto sim = bwaiSim(bwaiRules(), bwaiMap(), 1);
    Entity from = findType(*sim, "village3", 1), to = findType(*sim, "village1", 1);
    CHECK(from.valid() && to.valid());
    const int before = sim->world().get<game::Garrison>(from).count;
    Command c; c.player = 1; c.type = game::kCmdSend; c.units = {from}; c.target = to; c.param = -3;
    sim->submit(c);
    for (int i = 0; i < 3; ++i) sim->step();
    const auto& g = sim->world().get<game::Garrison>(from);
    CHECK(before - g.count >= 3 && before - g.count <= 4);   // 3 sent (+ at most one bred meanwhile)
    CHECK(g.pendingOut + 0 <= 3);
}

// bot vs bot: every algorithm / difficulty finishes the match
TEST(bwai_matches_finish) {
    struct Case { const char* p1; const char* p2; };
    size_t upgrades = 0, skills = 0;
    for (Case cs : {Case{"normal", "normal"}, Case{"expert", "easy"}, Case{"hard", "bishop"}, Case{"ashley", "expert"}}) {
        const BwaiMatch r = bwaiMatch(bwaiRules(), bwaiMap(cs.p1, cs.p2), 3, 20 * 60 * 20);
        std::printf("  bw %s vs %s: result %d after %us: %zu sends, %zu upgrades, %zu skills\n", cs.p1, cs.p2, r.result, r.ticks / 20, r.sends, r.upgrades, r.skills);
        CHECK(r.sends > 10);
        CHECK(r.result != 0);
        upgrades += r.upgrades; skills += r.skills;
    }
    CHECK(upgrades > 0 && skills > 0);
}

TEST(bwai_deterministic) {
    const Json rules = bwaiRules(), map = bwaiMap("expert", "normal");
    const BwaiMatch a = bwaiMatch(rules, map, 5, 3000), b = bwaiMatch(rules, map, 5, 3000);
    CHECK(a.sums == b.sums && a.commands == b.commands && a.commands > 0);
    const BwaiMatch c = bwaiMatch(rules, map, 6, 3000);
    CHECK(c.sums != a.sums);   // the seed matters (bot timers)
}

// a bot restored from its save state (next to the simulation) plays on exactly like the saved one
TEST(bwai_save_restore) {
    const Json rules = bwaiRules(), map = bwaiMap("expert", "hard");
    auto sim = bwaiSim(rules, map, 9);
    std::vector<game::RtsAI> bots;
    for (auto& p : sim->players()) bots.emplace_back(p.slot, 9);
    auto play = [](Simulation& s, std::vector<game::RtsAI>& bs, int ticks) {
        std::vector<uint64_t> sums;
        for (int i = 0; i < ticks; ++i) { for (auto& b : bs) b.update(s); s.step(); if (i % 50 == 49) sums.push_back(s.checksum()); }
        return sums;
    };
    play(*sim, bots, 1200);
    const std::string bytes = sim->serialize();
    std::vector<std::string> states;
    for (auto& b : bots) states.push_back(b.saveState());
    const auto expect = play(*sim, bots, 1200);
    auto sim2 = bwaiSim(rules, map, 9);
    std::string err;
    CHECK(sim2->deserialize(bytes, &err));
    std::vector<game::RtsAI> bots2;
    for (size_t i = 0; i < states.size(); ++i) { bots2.emplace_back(sim2->players()[i].slot, 1234); CHECK(bots2.back().loadState(states[i])); }
    CHECK(play(*sim2, bots2, 1200) == expect);
}
