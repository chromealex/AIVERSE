// Commerce (rules "commerce", RtsCommerce.h): renting and buying premises, businesses, daily income, weekly rent
// and loan instalments, the credit limit, eviction, foreclosure, saves and the Lua table.
#include "modules/rts/RtsCommerce.h"

namespace {
const char* kCommerceRules = R"J({
  "victory": "noUnits",
  "resources": {"cash": {"start": 1000}},
  "commerce": {
    "currency": "cash", "dayMinutes": 0.05, "weekDays": 7, "sellShare": 0.8, "unmanagedShare": 1,
    "bank": {"rateWeek": 0.02, "unsecured": 2000, "collateralShare": 0.5, "termWeeks": 10, "graceWeeks": 1},
    "businesses": {
      "kiosk": {"name": "Kiosk", "kinds": ["shop"], "setup": 600, "incomeDay": 200, "costDay": 80},
      "agency": {"kinds": ["office"], "setup": 100, "incomeDay": 10, "costDay": 60}
    }
  },
  "types": {"dummy": {"health": {"max": 100}, "move": {"speed": 0, "radius": 0.4}, "sight": 2},
            "van": {"health": {"max": 300}, "move": {"speed": 0, "radius": 0.5}, "sight": 2}}
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

TEST(commerce_staff_manager_customers_and_purchases) {
    using Op = game::CommerceOp;
    Json rules = Json::parse(kCommerceRules);
    Json com2 = rules["commerce"];
    com2.set("unmanagedShare", Json::parse("0.5"));
    com2.set("managerWageWeek", Json::parse("100"));
    com2.set("businesses", Json::parse(R"({"store": {"kinds": ["shop"], "setup": 0, "incomeDay": 400, "costDay": 0, "staff": 2, "wageWeek": 70, "ticket": 25}})"));
    com2.set("offers", Json::parse(R"({"van": {"kinds": ["dealer"], "price": 700, "vehicle": "van"}, "pistol": {"kinds": ["gunshop"], "price": 50, "weapon": "pistol", "ammo": 12}})"));
    rules.set("commerce", com2);
    SimConfig cfg;
    auto sim = std::make_unique<Simulation>(cfg, std::make_unique<game::RtsModule>());
    sim->setup(rules, Json::parse(commerceMap(R"([{"id":"s","kind":"shop","at":[5,5],"price":4000,"rentWeek":0,"owner":1,"business":"store"},
        {"id":"cars","kind":"dealer","at":[9,5],"price":1,"public":true},{"id":"guns","kind":"gunshop","at":[12,5],"price":1,"public":true}])")));
    auto& c = com(*sim);
    auto* st = c.store(*sim);
    Fixed& cash = mod(*sim).state(*sim).players[1].stock[0];
    game::Premise& shop = st->premises[0];
    // nobody works there and nobody looks after it: nothing comes in
    CHECK(c.expectedToday(shop).raw == 0 && !c.visit(*sim, 0));
    deal(*sim, 1, Op::Hire, 0);
    CHECK(shop.staff == 1 && c.expectedToday(shop) == Fixed::fromInt(100));          // half the staff, half the supervision
    deal(*sim, 1, Op::Hire, 0);
    deal(*sim, 1, Op::Hire, 0);                                                       // fully staffed: ignored
    CHECK(shop.staff == 2 && std::string(c.refusal(*sim, 1, Op::Hire, 0, 0)) == "fully staffed");
    deal(*sim, 1, Op::Manager, 0);
    CHECK(shop.manager && c.expectedToday(shop) == Fixed::fromInt(400));
    // walk-in customers pay at once; the day's total stays the same
    cash = Fixed{};
    CHECK(c.visit(*sim, 0) && c.visit(*sim, 0) && cash == Fixed::fromInt(50) && shop.served == Fixed::fromInt(50));
    runToTick(*sim, 61);
    CHECK(cash == Fixed::fromInt(400) && shop.lastNet == Fixed::fromInt(400) && shop.served.raw == 0);
    // a very busy day brings up to half more, then the door is shut
    int n = 0;
    while (c.visit(*sim, 0)) ++n;
    CHECK(n == 24);
    runToTick(*sim, 121);
    CHECK(cash == Fixed::fromInt(400 + 600));
    // the week: two wages and the manager
    runToTick(*sim, 421);
    CHECK(st->accounts[1].lastWages == Fixed::fromInt(240) && cash == Fixed::fromInt(1000 + 5 * 400 - 240));
    deal(*sim, 1, Op::Fire, 0);
    deal(*sim, 1, Op::Manager, 0);
    CHECK(shop.staff == 1 && !shop.manager);
    // the city's dealer and gun shop: not for sale, but they sell
    CHECK(std::string(c.refusal(*sim, 1, Op::Buy, 1, 0)) == "not for sale" && std::string(c.refusal(*sim, 1, Op::Rent, 2, 0)) == "not for sale");
    CHECK(std::string(c.refusal(*sim, 1, Op::Purchase, 0, c.findOffer("van"))) == "not sold here");
    CHECK(std::string(c.refusal(*sim, 1, Op::Purchase, 2, c.findOffer("pistol"))) == "no weapons in this game");
    const Fixed before = cash;
    const int vans = countType(*sim, "van", 0);
    deal(*sim, 1, Op::Purchase, 1, c.findOffer("van"));
    CHECK(cash == before - Fixed::fromInt(700) && countType(*sim, "van", 0) == vans + 1 && st->accounts[1].cars.size() == 1);
    // saved with staff, a car and customers served
    c.visit(*sim, 0);
    std::string save = sim->serialize(), err;
    auto copy = std::make_unique<Simulation>(cfg, std::make_unique<game::RtsModule>());
    copy->setup(rules, Json::parse(commerceMap(R"([{"id":"s","kind":"shop","at":[5,5],"price":4000,"rentWeek":0,"owner":1,"business":"store"},
        {"id":"cars","kind":"dealer","at":[9,5],"price":1,"public":true},{"id":"guns","kind":"gunshop","at":[12,5],"price":1,"public":true}])")));
    CHECK(copy->deserialize(save, &err) && copy->checksum() == sim->checksum());
    run(*sim, 5.0); run(*copy, 5.0);
    CHECK(copy->checksum() == sim->checksum());
}

TEST(commerce_home_stash_is_private_and_survives) {
    using Op = game::CommerceOp;
    auto sim = makeSimJson(kCommerceRules, commerceMap(R"([{"id":"home","kind":"flat","at":[5,5],"price":800,"rentWeek":100},
        {"id":"shop","kind":"shop","at":[9,5],"price":500,"rentWeek":50}])"));
    auto& c = com(*sim);
    auto* st = c.store(*sim);
    CHECK(std::string(c.refusal(*sim, 1, Op::Stash, 0, 100)) == "not your home");
    deal(*sim, 1, Op::Rent, 0);
    deal(*sim, 1, Op::Rent, 1);
    CHECK(std::string(c.refusal(*sim, 1, Op::Stash, 1, 100)) == "no stash here");
    CHECK(std::string(c.refusal(*sim, 2, Op::Unstash, 0, 100)) == "not your home");
    CHECK(std::string(c.refusal(*sim, 1, Op::StashWeapons, 0, 0)) == "no weapons in this game");
    deal(*sim, 1, Op::Stash, 0, 600);
    CHECK(st->premises[0].stashCash == Fixed::fromInt(600) && c.cash(*sim, 1) == Fixed::fromInt(1000 - 150 - 600));
    CHECK(std::string(c.refusal(*sim, 1, Op::Leave, 0, 0)) == "empty the stash first");
    deal(*sim, 2, Op::Unstash, 0, 600);                                   // somebody else: ignored
    deal(*sim, 1, Op::Unstash, 0, 100);
    CHECK(st->premises[0].stashCash == Fixed::fromInt(500) && c.cash(*sim, 1) == Fixed::fromInt(350));
    // thrown out for the rent: the stashed money comes back to the pocket
    mod(*sim).state(*sim).players[1].stock[0] = Fixed{};
    runToTick(*sim, 421);
    CHECK(st->premises[0].tenant == 0 && st->premises[0].stashCash.raw == 0 && c.cash(*sim, 1) == Fixed::fromInt(450));   // and the shop rent is paid from it
}

TEST(commerce_accounts_outlive_the_seat) {
    using Op = game::CommerceOp;
    Json rules = Json::parse(kCommerceRules);
    Json com2 = rules["commerce"];
    com2.set("absentDays", Json::parse("3"));
    rules.set("commerce", com2);
    const std::string map = commerceMap(R"([{"id":"kiosk","kind":"shop","at":[5,5],"price":500,"rentWeek":70},
        {"id":"own","kind":"office","at":[9,5],"price":400,"rentWeek":50}])");
    SimConfig cfg;
    auto make = [&] { auto s = std::make_unique<Simulation>(cfg, std::make_unique<game::RtsModule>()); s->setup(rules, Json::parse(map)); return s; };
    auto sim = make();
    auto& c = com(*sim);
    auto* st = c.store(*sim);
    CHECK(c.account(*sim, 1) && c.account(*sim, 1)->id == 1 && c.account(*sim, 1)->slot == 1 && !c.account(*sim, 0));
    deal(*sim, 1, Op::Buy, 1);                                 // 1000 - 400
    deal(*sim, 1, Op::Rent, 0);                                // - 70
    deal(*sim, 1, Op::Open, 0, c.findBusiness("kiosk"));       // - 600 with what is left? no: 530 < 600, refused
    CHECK(st->premises[1].owner == 1 && st->premises[0].tenant == 1 && st->premises[0].business == -1 && c.cash(*sim, 1) == Fixed::fromInt(530));
    // the player leaves: his seat is empty, the account keeps what it owns and the money
    c.unbind(*sim, 1);
    CHECK(!c.account(*sim, 1) && c.cash(*sim, 1).raw == 0 && st->premises[1].owner == 1 && st->accounts[1].wallet == Fixed::fromInt(530));
    CHECK(std::string(c.refusal(*sim, 1, Op::Sell, 1, 0)) == "no such player" && c.slotOf(*sim, st->premises[1].owner) == 0);
    // somebody else takes the seat: a new account, he cannot touch the first one's things
    CHECK(c.bind(*sim, 1, 777) && c.account(*sim, 1)->id == 777 && c.cash(*sim, 1).raw == 0 && !c.bind(*sim, 2, 777) && !c.bind(*sim, 1, 0));
    CHECK(std::string(c.refusal(*sim, 1, Op::Sell, 1, 0)) == "not yours" && std::string(c.refusal(*sim, 1, Op::Rent, 0, 0)) == "taken");
    CHECK(c.accountById(*sim, 1) && c.accountById(*sim, 1)->slot == 0 && !c.accountById(*sim, 12345));
    // saved with one account away: the copy is the same
    std::string save = sim->serialize(), err;
    auto copy = make();
    CHECK(copy->deserialize(save, &err) && copy->checksum() == sim->checksum() && com(*copy).account(*copy, 1)->id == 777);
    // the first one comes back to another seat with his wallet
    CHECK(c.bind(*sim, 2, 1) && c.cash(*sim, 2) == Fixed::fromInt(530) && st->accounts[1].wallet.raw == 0);
    CHECK(c.refusal(*sim, 2, Op::Sell, 1, 0) == nullptr && c.slotOf(*sim, st->premises[1].owner) == 2);
    // away for three days: sold out to the city, the money waits for him
    c.unbind(*sim, 2);
    runToTick(*sim, 2 * 60 + 5);
    CHECK(st->premises[1].owner == 1 && st->premises[0].tenant == 1);
    runToTick(*sim, 3 * 60 + 5);
    CHECK(st->premises[1].owner == 0 && st->premises[0].tenant == 0 && about(st->accounts[1].wallet, 530 + 320));
    CHECK(c.bind(*sim, 2, 1) && about(c.cash(*sim, 2), 850));
    // sold out on request, with a debt: the bank is paid first
    deal(*sim, 2, Op::Buy, 1);
    deal(*sim, 2, Op::Borrow, 0, 300);
    CHECK(about(c.cash(*sim, 2), 750) && c.sellOut(*sim, 1) && !c.sellOut(*sim, 999));
    CHECK(st->premises[1].owner == 0 && c.account(*sim, 2)->debt.raw == 0 && about(c.cash(*sim, 2), 750 + 320 - 300));

    // a server's game: nobody owns a seat until an account is bound
    com2.set("accounts", Json::string("external"));
    rules.set("commerce", com2);
    auto srv = make();
    auto& sc = com(*srv);
    CHECK(!sc.account(*srv, 1) && std::string(sc.refusal(*srv, 1, Op::Rent, 0, 0)) == "no such player");
    CHECK(sc.bind(*srv, 1, 5001) && sc.account(*srv, 1)->id == 5001 && sc.cash(*srv, 1).raw == 0);   // a new account starts with an empty wallet
    CHECK(std::string(sc.refusal(*srv, 1, Op::Rent, 0, 0)) == "not enough money");
}
