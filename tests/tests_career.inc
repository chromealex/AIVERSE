// Careers (rules "careers", RtsCareer.h): ladders, ranks and perks, several careers at once, open careers by
// command and closed ones by script only, saves, the Lua table; fines of the commerce module.
#include "modules/rts/RtsCareer.h"

namespace {
const char* kCareerRules = R"J({
  "victory": "noUnits",
  "resources": {"cash": {"start": 100}},
  "commerce": {"currency": "cash", "dayMinutes": 1},
  "careers": {
    "police": {"name": "Police", "open": true, "ranks": [
      {"name": "Cadet", "points": 0}, {"name": "Officer", "points": 10, "perks": ["siren"]}, {"name": "Detective", "points": 50, "perks": ["records"]}]},
    "gang": {"name": "Gang", "ranks": [{"name": "Newcomer", "points": 0}, {"name": "Trusted", "points": 5, "perks": ["hotwire"]}]}
  },
  "types": {"dummy": {"health": {"max": 100}, "move": {"speed": 0, "radius": 0.4}, "sight": 2}}
})J";
const char* kCareerMap = R"(,"entities":[{"type":"dummy","owner":1,"pos":[3,3]},{"type":"dummy","owner":2,"pos":[28,12]}])";
} // namespace

TEST(career_ladders_perks_commands_and_saves) {
    CHECK(!game::Career::parse(Json()).enabled);
    auto fails = [&](const char* json, const char* what) {
        try { game::Career::parse(Json::parse(json)); } catch (const std::exception& e) { return std::string(e.what()).find(what) != std::string::npos; }
        return false;
    };
    CHECK(fails(R"({"a": {"ranks": []}})", "first rank starts at 0"));
    CHECK(fails(R"({"a": {"ranks": [{"name": "x", "points": 0}, {"name": "y", "points": 0}]}})", "must grow"));
    CHECK(fails(R"({"a": {"ranks": [{"points": 0}]}})", "needs a name"));

    auto sim = makeSimJson(kCareerRules, flatMap(32, 16, kCareerMap));
    auto& c = *mod(*sim).career();
    const int gang = c.find("gang"), police = c.find("police");
    CHECK(c.active() && gang == 0 && police == 1 && c.find("fire") == -1);
    CHECK(c.rank(*sim, 1, police) == -1 && c.add(*sim, 1, police, Fixed::fromInt(20)) == 0);   // not on it: nothing counts
    auto order = [&](game::CareerOp op, int career) {
        Command cmd; cmd.player = 1; cmd.type = game::kCmdCareer; cmd.param = game::careerPack(op, career); sim->submit(cmd);
        for (int i = 0; i < 4; ++i) sim->step();
    };
    order(game::CareerOp::Join, gang);                    // by invitation only: the command is ignored
    CHECK(c.rank(*sim, 1, gang) == -1);
    order(game::CareerOp::Join, police);
    CHECK(c.rank(*sim, 1, police) == 0 && !c.hasPerk(*sim, 1, "siren"));
    CHECK(c.add(*sim, 1, police, Fixed::fromInt(12)) == 1 && c.rank(*sim, 1, police) == 1 && c.hasPerk(*sim, 1, "siren"));
    CHECK(c.add(*sim, 1, police, Fixed::fromInt(60)) == 1 && c.hasPerk(*sim, 1, "records") && c.hasPerk(*sim, 1, "siren"));
    CHECK(c.add(*sim, 1, police, Fixed::fromInt(-70)) == -2 && c.rank(*sim, 1, police) == 0 && !c.hasPerk(*sim, 1, "siren") && c.progress(*sim, 1, police)->points == Fixed::fromInt(2));
    // both at once: a policeman in a gang
    CHECK(c.join(*sim, 1, gang) && !c.join(*sim, 1, gang) && c.add(*sim, 1, gang, Fixed::fromInt(5)) == 1);
    CHECK(c.hasPerk(*sim, 1, "hotwire") && !c.hasPerk(*sim, 2, "hotwire") && c.rank(*sim, 1, police) == 0);
    std::string save = sim->serialize(), err;
    auto copy = makeSimJson(kCareerRules, flatMap(32, 16, kCareerMap));
    CHECK(copy->checksum() != sim->checksum() && copy->deserialize(save, &err) && copy->checksum() == sim->checksum());
    CHECK(mod(*copy).career()->hasPerk(*copy, 1, "hotwire"));
    order(game::CareerOp::Leave, police);
    CHECK(c.rank(*sim, 1, police) == -1 && c.rank(*sim, 1, gang) == 1);
    // a fine: the pocket first, the rest is owed to the bank
    auto& com = *mod(*sim).commerce();
    com.fine(*sim, 1, Fixed::fromInt(60));
    CHECK(com.cash(*sim, 1) == Fixed::fromInt(40) && com.store(*sim)->accounts[1].debt.raw == 0);
    com.fine(*sim, 1, Fixed::fromInt(100));
    CHECK(com.cash(*sim, 1).raw == 0 && com.store(*sim)->accounts[1].debt == Fixed::fromInt(60));
}

TEST(career_lua_table) {
    Json rules = Json::parse(kCareerRules);
    Json list = Json::array(), src = Json::object();
    list.push(Json::string("scripts/a.lua"));
    src.set("scripts/a.lua", Json::string(R"(
        function on_start()
          assert(career.active() and #career.list() == 2 and career.list()[2].ranks[3].name == "Detective")
          assert(career.status(1, "police") == nil and not career.has_perk(1, "siren"))
          assert(career.join(1, "police") and career.add(1, "police", 10) == 1)
          local s = career.status(1, "police")
          assert(s.rank == 2 and s.rank_name == "Officer" and s.points == fx(10) and s.next_points == fx(50))
          assert(career.has_perk(1, "siren") and career.join(1, "gang") and career.leave(1, "gang"))
          commerce.fine(1, 150)
          assert(commerce.account(1).debt == fx(50))
        end
    )"));
    rules.set("scripts", list);
    rules.set("$scriptSources", src);
    SimConfig cfg;
    auto sim = std::make_unique<Simulation>(cfg, std::make_unique<game::RtsModule>());
    sim->setup(rules, Json::parse(flatMap(32, 16, kCareerMap)));
    for (int i = 0; i < 5; ++i) sim->step();
    for (auto& e : mod(*sim).scripts()->log()) if (e.error) std::printf("  log: %s\n", e.text.c_str());
    CHECK(mod(*sim).scripts()->errorCount() == 0 && mod(*sim).career()->rank(*sim, 1, mod(*sim).career()->find("police")) == 1);
}

TEST(prompts_show_pick_and_interact) {
    Json rules = Json::parse(kCareerRules);
    rules.set("prompts", Json::parse("true"));
    Json list = Json::array(), src = Json::object();
    list.push(Json::string("scripts/a.lua"));
    src.set("scripts/a.lua", Json::string(R"(
        function on_interact(player)
          prompt.show(player, "hello", "Stranger", {"Nice day."}, {"Ask about work", "Bye"})
        end
        function on_prompt(player, what)
          state.picked = what
          if what == "hello:1" then prompt.show(player, "work", "Stranger", {"They hire at the docks."}, {"Thanks"}) end
        end
        function on_start()
          assert(prompt.get(1) == nil)
          assert(not prompt.show(1, "bad:id", "t", {}, {"x"}) and not prompt.show(1, "none", "t", {}, {}))
        end
    )"));
    rules.set("scripts", list);
    rules.set("$scriptSources", src);
    SimConfig cfg;
    auto sim = std::make_unique<Simulation>(cfg, std::make_unique<game::RtsModule>());
    sim->setup(rules, Json::parse(flatMap(32, 16, kCareerMap)));
    auto& pr = *mod(*sim).prompts();
    auto send = [&](uint8_t player, int64_t param) {
        Command c; c.player = player; c.type = game::kCmdPrompt; c.param = param; sim->submit(c);
        for (int i = 0; i < 4; ++i) sim->step();
    };
    for (int i = 0; i < 3; ++i) sim->step();
    CHECK(pr.active() && !pr.of(*sim, 1));
    send(1, 0);                                   // nothing open: ignored
    CHECK(!pr.of(*sim, 1));
    send(1, game::kPromptInteract);
    CHECK(pr.of(*sim, 1) && pr.of(*sim, 1)->id == "hello" && pr.of(*sim, 1)->options.size() == 2 && !pr.of(*sim, 2));
    send(1, 5);                                   // no such option: stays open
    send(2, 0);                                   // somebody else's prompt: ignored
    CHECK(pr.of(*sim, 1) && pr.of(*sim, 1)->id == "hello");
    std::string save = sim->serialize(), err;
    auto copy = std::make_unique<Simulation>(cfg, std::make_unique<game::RtsModule>());
    copy->setup(rules, Json::parse(flatMap(32, 16, kCareerMap)));
    CHECK(copy->deserialize(save, &err) && copy->checksum() == sim->checksum() && mod(*copy).prompts()->of(*copy, 1));
    send(1, 0);                                   // "Ask about work": the script opens the next one
    CHECK(pr.of(*sim, 1) && pr.of(*sim, 1)->id == "work" && pr.of(*sim, 1)->lines[0] == "They hire at the docks.");
    send(1, 0);
    CHECK(!pr.of(*sim, 1));
    for (auto& e : mod(*sim).scripts()->log()) if (e.error) std::printf("  log: %s\n", e.text.c_str());
    CHECK(mod(*sim).scripts()->errorCount() == 0);
    auto plain = makeSimJson(kCareerRules, flatMap(32, 16, kCareerMap));
    CHECK(!mod(*plain).prompts()->active() && !mod(*plain).prompts()->store(*plain));
}
