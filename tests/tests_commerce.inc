// Commerce (rules "commerce", RtsCommerce.h): renting and buying premises, businesses, daily income, weekly rent
// and loan instalments, the credit limit, eviction, foreclosure, saves and the Lua table.
#include "modules/rts/RtsCommerce.h"

namespace {
const char* kCommerceRules = R"J({
  "victory": "noUnits",
  "resources": {"cash": {"start": 1000}},
  "commerce": {
    "currency": "cash", "dayMinutes": 0.05, "weekDays": 7, "sellShare": 0.8,
    "bank": {"rateWeek": 0.02, "unsecured": 2000, "collateralShare": 0.5, "termWeeks": 10, "graceWeeks": 1},
    "businesses": {
      "kiosk": {"name": "Kiosk", "kinds": ["shop"], "setup": 600, "incomeDay": 200, "costDay": 80},
      "agency": {"kinds": ["office"], "setup": 100, "incomeDay": 10, "costDay": 60}
    }
  },
  "types": {"dummy": {"health": {"max": 100}, "move": {"speed": 0, "radius": 0.4}, "sight": 2}}
})J";
std::string commerceMap(const char* premises) {
    return flatMap(32, 16, std::string(R"(,"entities":[{"type":"dummy","owner":1,"pos":[3,3]},{"type":"dummy","owner":2,"pos":[28,12]}],"premises":)") + premises);
}
const char* kPremises = R"([{"id":"kiosk_a","kind":"shop","at":[5,5],"price":5000,"rentWeek":350,"traffic":1.5},
                            {"id":"office_b","kind":"office","at":[9,5],"price":2900,"rentWeek":200}])";
game::Commerce& com(Simulation& sim) { return *mod(sim).commerce(); }
void deal(Simulation& sim, uint8_t p, game::CommerceOp op, int premise, int64_t value = 0) {
    Command c; c.player = p; c.type = game::kCmdCommerce; c.param = game::commercePack(op, premise, value); sim.submit(c);
    for (int i = 0; i < 4; ++i) sim.step();
}
bool about(Fixed v, int want) { return fxAbs(v - Fixed::fromInt(want)) < Fixed::fromInt(1); }
void runToTick(Simulation& sim, uint32_t tick) { while (sim.tick() < tick) sim.step(); }
} // namespace

TEST(commerce_rules_parse_and_errors) {
    std::vector<std::string> res = {"cash"};
    auto r = game::Commerce::parse(Json::parse(R"({"currency":"cash","dayMinutes":24,"businesses":{"b":{},"a":{"kinds":["shop"]}}})"), res, 20);
    CHECK(r.enabled && r.currency == 0 && r.dayTicks == 24 * 60 * 20 && r.businesses.size() == 2 && r.businesses[0].id == "a");
    CHECK(!game::Commerce::parse(Json(), res, 20).enabled);
    auto fails = [&](const char* json, const char* what) {
        try { game::Commerce::parse(Json::parse(json), res, 20); } catch (const std::exception& e) { return std::string(e.what()).find(what) != std::string::npos; }
        return false;
    };
    CHECK(fails(R"({"currency":"gold"})", "name a resource"));
    CHECK(fails(R"({"currency":"cash","dayMinutes":0})", "dayMinutes: must be positive"));
    CHECK(fails(R"({"currency":"cash","bank":{"termWeeks":0}})", "termWeeks"));
    CHECK(fails(R"({"currency":"cash","sellShare":2})", "sellShare"));
    auto badMap = [&](const char* premises, const char* what) {
        try { makeSimJson(kCommerceRules, commerceMap(premises)); } catch (const std::exception& e) { return std::string(e.what()).find(what) != std::string::npos; }
        return false;
    };
    CHECK(badMap(R"([{"id":"a","at":[1,1],"price":10},{"id":"a","at":[2,1],"price":10}])", "used twice"));
    CHECK(badMap(R"([{"id":"a","at":[1,1]}])", "price: must be positive"));
    CHECK(badMap(R"([{"id":"a","at":[1,1],"price":10,"business":"cinema"}])", "unknown 'cinema'"));
    CHECK(badMap(R"([{"id":"a","kind":"office","at":[1,1],"price":10,"business":"kiosk"}])", "does not fit"));
}

TEST(commerce_rent_open_earn_borrow_buy) {
    auto sim = makeSimJson(kCommerceRules, commerceMap(kPremises));
    auto& c = com(*sim);
    auto* st = c.store(*sim);
    using Op = game::CommerceOp;
    const int kiosk = c.findBusiness("kiosk");
    CHECK(c.active() && st && st->premises.size() == 2 && c.findPremise(*sim, "office_b") == 1 && kiosk >= 0);
    CHECK(c.cash(*sim, 1) == Fixed::fromInt(1000));
    CHECK(std::string(c.refusal(*sim, 1, Op::Open, 0, kiosk)) == "not yours");
    deal(*sim, 1, Op::Rent, 0);
    CHECK(st->premises[0].tenant == 1 && c.cash(*sim, 1) == Fixed::fromInt(650));
    CHECK(std::string(c.refusal(*sim, 2, Op::Rent, 0, 0)) == "taken");
    deal(*sim, 1, Op::Open, 1, c.findBusiness("agency"));                        // not his office: ignored
    CHECK(st->premises[1].business == -1 && c.cash(*sim, 1) == Fixed::fromInt(650));
    CHECK(std::string(c.refusal(*sim, 1, Op::Open, 0, c.findBusiness("agency"))) == "does not fit here");
    deal(*sim, 1, Op::Open, 0, kiosk);
    CHECK(st->premises[0].business == kiosk && c.cash(*sim, 1) == Fixed::fromInt(50));
    // the first day: 200 * 1.5 - 80
    runToTick(*sim, 61);
    CHECK(c.cash(*sim, 1) == Fixed::fromInt(270) && st->premises[0].lastNet == Fixed::fromInt(220) && st->accounts[1].dayNet == Fixed::fromInt(220));
    // the bank: up to the unsecured limit
    CHECK(std::string(c.refusal(*sim, 1, Op::Buy, 1, 0)) == "not enough money");
    CHECK(c.creditLimit(*sim, 1) == Fixed::fromInt(2000));
    deal(*sim, 1, Op::Borrow, 0, 2000);
    CHECK(st->accounts[1].debt == Fixed::fromInt(2000) && c.cash(*sim, 1) == Fixed::fromInt(2270));
    CHECK(std::string(c.refusal(*sim, 1, Op::Borrow, 0, 1)) == "over the credit limit");
    // the end of the week: seven days of income, then rent, then interest and a tenth of the loan
    runToTick(*sim, 421);
    CHECK(about(c.cash(*sim, 1), 50 + 7 * 220 + 2000 - 350 - 240) && about(st->accounts[1].debt, 1800));
    CHECK(st->accounts[1].lastRent == Fixed::fromInt(350) && about(st->accounts[1].lastLoanPayment, 240) && st->accounts[1].missed == 0);
    CHECK(st->accounts[1].lastWeekNet == Fixed::fromInt(7 * 220) && st->premises[0].total == Fixed::fromInt(7 * 220));
    // buying the office raises the limit; with more debt on it the bank does not let it go
    deal(*sim, 1, Op::Buy, 1);
    CHECK(st->premises[1].owner == 1 && about(c.cash(*sim, 1), 100) && c.creditLimit(*sim, 1) == Fixed::fromInt(3450));
    CHECK(c.refusal(*sim, 1, Op::Sell, 1, 0) == nullptr);
    deal(*sim, 1, Op::Borrow, 0, 1500);
    CHECK(about(st->accounts[1].debt, 3300) && std::string(c.refusal(*sim, 1, Op::Sell, 1, 0)) == "pledged to the bank");
    deal(*sim, 1, Op::Repay, 0, 1500);
    CHECK(about(st->accounts[1].debt, 1800));
    // a tenant who buys keeps his business; leaving a lease closes it
    CHECK(std::string(c.refusal(*sim, 1, Op::Leave, 1, 0)) == "not your lease");
    // saves: a copy continues identically
    std::string save = sim->serialize(), err;
    auto copy = makeSimJson(kCommerceRules, commerceMap(kPremises));
    CHECK(copy->deserialize(save, &err) && copy->checksum() == sim->checksum());
    run(*sim, 25.0); run(*copy, 25.0);
    CHECK(copy->checksum() == sim->checksum() && com(*copy).cash(*copy, 1) == c.cash(*sim, 1));
    deal(*sim, 1, Op::Leave, 0);
    CHECK(st->premises[0].tenant == 0 && st->premises[0].business == -1 && copy->checksum() != sim->checksum());
    // selling pays 80 %
    const Fixed before = c.cash(*sim, 1);
    deal(*sim, 1, Op::Sell, 1);
    CHECK(st->premises[1].owner == 0 && about(c.cash(*sim, 1) - before, 2320));
}

TEST(commerce_eviction_and_foreclosure) {
    using Op = game::CommerceOp;
    auto sim = makeSimJson(kCommerceRules, commerceMap(R"([{"id":"kiosk_a","kind":"shop","at":[5,5],"price":5000,"rentWeek":350,"business":"kiosk"},
        {"id":"own","kind":"office","at":[9,5],"price":1000,"owner":1,"business":"agency"}])"));
    auto& c = com(*sim);
    auto* st = c.store(*sim);
    Fixed& cash = mod(*sim).state(*sim).players[1].stock[0];
    CHECK(st->premises[1].owner == 1 && st->premises[0].business >= 0 && c.creditLimit(*sim, 1) == Fixed::fromInt(2500));
    deal(*sim, 1, Op::Rent, 0);
    deal(*sim, 1, Op::Borrow, 0, 2500);
    // day 1: the kiosk brings 120, the agency loses 50
    cash = Fixed{};
    runToTick(*sim, 61);
    CHECK(cash == Fixed::fromInt(70) && st->premises[1].lastNet == Fixed::fromInt(-50) && st->accounts[1].debt == Fixed::fromInt(2500));
    // week 1 ends with no money for the rent: evicted, the kiosk is gone; the loan payment is missed
    for (int d = 2; d <= 7; ++d) { cash = Fixed{}; runToTick(*sim, uint32_t(d * 60 + 1)); }
    CHECK(st->premises[0].tenant == 0 && st->premises[0].business == -1 && st->accounts[1].missed == 1);
    CHECK(st->premises[1].owner == 1 && st->accounts[1].lastRent.raw == 0 && st->accounts[1].lastLoanPayment == Fixed::fromInt(70));
    // week 2: only the losing agency is left; what the player cannot cover becomes debt
    const Fixed owed = st->accounts[1].debt;
    cash = Fixed{};
    runToTick(*sim, 8 * 60 + 1);
    CHECK(st->accounts[1].debt == owed + Fixed::fromInt(50) && cash.raw == 0);
    // missed again, past the grace week: the bank takes the office for half its price
    runToTick(*sim, 14 * 60 + 1);
    CHECK(st->premises[1].owner == 0 && st->premises[1].business == -1 && st->accounts[1].missed == 2);
    CHECK(st->accounts[1].debt < owed && st->accounts[1].debt > owed - Fixed::fromInt(200));
}

TEST(commerce_lua_table_and_games_without_it) {
    Json rules = Json::parse(kCommerceRules);
    Json list = Json::array(), src = Json::object();
    list.push(Json::string("scripts/a.lua"));
    src.set("scripts/a.lua", Json::string(R"(
        function on_start()
          assert(commerce.active() and commerce.count() == 2)
          local p = commerce.premise("kiosk_a")
          assert(p.index == 1 and p.kind == "shop" and p.owner == 0 and p.business == nil)
          local ok, why = commerce.can(1, "open", "kiosk_a", "kiosk")
          assert(not ok and why == "not yours")
          assert(commerce.act(1, "rent", "kiosk_a") and commerce.act(1, "open", 1, "kiosk"))
          assert(commerce.premise(1).business == "kiosk" and commerce.premise(1).tenant == 1)
          assert(commerce.act(1, "borrow", 500) and not commerce.act(1, "borrow", 5000))
          local a = commerce.account(1)
          assert(a.debt == fx(500) and a.limit == fx(2000) and a.cash == fx(550))
          assert(commerce.clock().day == 1 and #commerce.businesses() == 2)
          assert(commerce.param("rent", "office_b") == 1 + 1 * 256)
        end
    )"));
    rules.set("scripts", list);
    rules.set("$scriptSources", src);
    SimConfig cfg;
    auto sim = std::make_unique<Simulation>(cfg, std::make_unique<game::RtsModule>());
    sim->setup(rules, Json::parse(commerceMap(kPremises)));
    for (int i = 0; i < 5; ++i) sim->step();
    for (auto& e : mod(*sim).scripts()->log()) if (e.error) std::printf("  log: %s\n", e.text.c_str());
    CHECK(mod(*sim).scripts()->errorCount() == 0);
    CHECK(com(*sim).store(*sim)->accounts[1].debt == Fixed::fromInt(500) && com(*sim).store(*sim)->premises[0].business >= 0);
    auto plain = makeSimJson(kDrivingRules, topDownMap(kDriveStart));
    CHECK(mod(*plain).commerce() && !mod(*plain).commerce()->active() && !mod(*plain).commerce()->store(*plain));
}
