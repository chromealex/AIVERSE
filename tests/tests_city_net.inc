// The city server (engine/net/city, docs/city-server.md): one simulation on the server, a client gets what is
// near its hero, its account lives in a store between sessions.
#include "engine/meta/Modules.h"
#include "engine/net/city/CityMeta.h"
#include "engine/net/city/CityMirror.h"
#include "engine/net/city/CityNet.h"

namespace {
namespace cn = aiverse::net::city;
Json cityNetRules() {
    Json rules = Json::parse(kTopDownRules);
    rules.set("resources", Json::parse(R"({"cash": {"start": 1000}})"));
    rules.set("commerce", Json::parse(R"({"currency": "cash", "dayMinutes": 5, "weekDays": 7, "sellShare": 0.8, "accounts": "external",
        "bank": {"rateWeek": 0.02, "unsecured": 2000, "collateralShare": 0.5, "termWeeks": 10, "graceWeeks": 1},
        "businesses": {"kiosk": {"kinds": ["shop"], "setup": 100, "incomeDay": 50, "costDay": 10}}})"));
    return rules;
}
Json cityNetMap() {
    Json map = Json::parse(topDownMap(R"({"type":"drone","owner":1,"pos":[4.5,4.5]},{"type":"drone","owner":2,"pos":[28.5,12.5]},
        {"type":"mover","owner":2,"pos":[6.5,4.5]},{"type":"dummy","owner":2,"pos":[5.5,6.5]},{"type":"dummy","owner":2,"pos":[27.5,10.5]})", false));
    map.set("premises", Json::parse(R"([{"id":"kiosk","kind":"shop","at":[5,5],"price":500,"rentWeek":70},{"id":"flat","kind":"flat","at":[26,12],"price":300,"rentWeek":40}])"));
    return map;
}
struct CityNetRig {
    cn::Server server;
    std::vector<std::unique_ptr<cn::Client>> clients;
    uint64_t now = 0;
    static cn::Options classic(cn::Options o) { o.emptySeats = false; return o; }   // every seat keeps its hero (the first tests)
    CityNetRig(cn::Options o, cn::AccountStore* store, bool emptySeats = false) : server(cityNetRules(), cityNetMap(), 1, emptySeats ? o : classic(o), store) {}
    cn::Client& join(uint8_t slot, const std::string& player, uint64_t content = 0) {
        auto pair = net::makeLoopbackPair();
        const bool ok = server.attach(slot, std::move(pair.first), player);
        CHECK(ok);
        clients.push_back(std::make_unique<cn::Client>(std::move(pair.second), content ? content : server.content()));
        return *clients.back();
    }
    cn::Client& knock(const std::string& name, const std::vector<uint8_t>& ticket, uint32_t latencyMs = 0) {   // a stranger: the server decides
        net::LoopbackOptions lo;
        lo.latencyMs = latencyMs;
        auto pair = net::makeLoopbackPair(lo);
        server.admit(std::move(pair.first));
        clients.push_back(std::make_unique<cn::Client>(std::move(pair.second), server.content(), name, ticket));
        return *clients.back();
    }
    void run(int ticks) {
        for (int i = 0; i < ticks; ++i) {
            now += 1000 / server.simulation().tickRate();
            for (auto& c : clients) c->poll(now);
            server.poll(now);
            server.step();
        }
        for (auto& c : clients) c->poll(now);
    }
};
Command cityDeal(game::CommerceOp op, int premise, int64_t value = 0) {
    Command c; c.type = game::kCmdCommerce; c.param = game::commercePack(op, premise, value); c.player = 9;   // the server stamps the real one
    return c;
}
}

TEST(city_server_tells_a_client_what_is_near) {
    cn::Options o;
    o.radius = 8;
    CityNetRig rig(o, nullptr);
    CHECK(rig.server.seats() == std::vector<uint8_t>({1, 2}) && rig.server.freeSeat() == 1);
    cn::Client& a = rig.join(1, "");
    cn::Client& b = rig.join(2, "");
    CHECK(!rig.server.attach(1, net::makeLoopbackPair().first) && !rig.server.attach(3, net::makeLoopbackPair().first) && rig.server.freeSeat() == 0);
    rig.run(10);
    CHECK(a.ready() && b.ready() && a.slot() == 1 && b.slot() == 2 && a.tick() > 0 && a.tickRate() == rig.server.simulation().tickRate());
    Simulation& sim = rig.server.simulation();
    const Entity heroA = rig.server.module().topDown()->heroOf(sim, 1), heroB = rig.server.module().topDown()->heroOf(sim, 2);
    CHECK(a.hero() == heroA.index && b.hero() == heroB.index);
    // a sees himself, the mover and the dummy next to him; not the far hero and its dummy
    CHECK(a.entities().size() == 3 && a.entities().count(heroA.index) && !a.entities().count(heroB.index));
    CHECK(b.entities().size() == 2 && b.entities().count(heroB.index) && !b.entities().count(heroA.index));
    CHECK(a.entities().at(heroA.index).state.flags == 1 && a.entities().at(heroA.index).state.owner == 1 && a.entities().at(heroA.index).state.hp == 255);
    CHECK(a.entities().at(heroA.index).state.x == 4 * 64 + 32 && a.entities().at(heroA.index).state.z == 4 * 64 + 32);
    size_t standing = 0;
    for (auto& [id, e] : a.entities()) standing += e.fixed;
    CHECK(standing >= 1);   // what stands was told once, reliably
    // he steers east: the server moves him, the client sees it; the command's player field is ignored
    game::topdown::Input in;
    in.moveAngle = game::TopDown::angleOf(kEast); in.moveMag = 1000;
    Command c = net::heroInputCommand(2, in);
    CHECK(a.submit(c));
    const uint64_t before = rig.server.stats().snapshotBytes;
    rig.run(40);
    CHECK(tdPos(sim, heroA).x > Fixed::fromInt(8) && tdPos(sim, heroB).x == Fixed::fromRatio(57, 2));
    CHECK(a.entities().at(heroA.index).state.x > 8 * 64 && !a.entities().at(heroA.index).fixed);
    CHECK(rig.server.stats().commands == 1 && rig.server.stats().rejected == 0 && rig.server.stats().snapshotBytes > before);
    // far from the start now: the things there are gone from his view, a standing one by the server's word
    in.moveMag = 1000;
    rig.run(60);
    CHECK(tdPos(sim, heroA).x > Fixed::fromInt(16));
    bool sawStartDummy = false;
    for (auto& [id, e] : a.entities()) if (e.state.x == 5 * 64 + 32 && e.state.z == 6 * 64 + 32) sawStartDummy = true;
    CHECK(!sawStartDummy);
    // b stood still all the time: after the first second almost nothing is sent to him
    CHECK(b.bytesIn() < a.bytesIn());
    // a client of other content is not let in; when a client goes its hero stops
    const size_t was = rig.server.clients();
    a.close();
    rig.clients.erase(rig.clients.begin());   // a leaves: the connection closes
    rig.run(5);
    CHECK(rig.server.clients() == was - 1 && rig.server.freeSeat() == 1);
    const Fixed stopped = tdPos(sim, heroA).x;
    rig.run(30);
    CHECK(fxAbs(tdPos(sim, heroA).x - stopped) < Fixed::fromInt(2));
    cn::Client& stranger = rig.join(1, "", 12345);
    rig.run(5);
    CHECK(!stranger.ready() && rig.server.stats().rejected == 1 && rig.server.clients() == was - 1);
}

TEST(city_server_keeps_accounts_between_sessions) {
    using Op = game::CommerceOp;
    int64_t clock = 1000;
    cn::FileAccounts store("", [&] { return clock; });
    cn::Options o;
    o.radius = 8;
    {
        CityNetRig rig(o, &store);
        cn::Client& ann = rig.join(1, "ann");
        CHECK(!rig.server.attach(2, net::makeLoopbackPair().first, "ann"));   // she is in already
        rig.run(25);
        CHECK(ann.ready() && ann.account().cash == Fixed::fromInt(0).raw);   // an outside account starts empty
        game::Commerce& c = *rig.server.module().commerce();
        Simulation& sim = rig.server.simulation();
        mod(sim).state(sim).players[1].stock[0] = Fixed::fromInt(1000);
        CHECK(ann.submit(cityDeal(Op::Buy, 0)));
        rig.run(25);
        CHECK(c.store(sim)->premises[0].owner != 0 && ann.account().cash == Fixed::fromInt(500).raw);
        CHECK(ann.submit(cityDeal(Op::Open, 0, c.findBusiness("kiosk"))));
        rig.run(25);
        CHECK(c.store(sim)->premises[0].business >= 0 && ann.account().cash == Fixed::fromInt(400).raw);
        clock = 2000;
        ann.close();
        rig.clients.clear();   // she leaves
        rig.run(3);
        CHECK(rig.server.clients() == 0 && c.cash(sim, 1).raw == 0 && c.store(sim)->premises[0].owner != 0);
        CHECK(store.data()["ann"]["account"]["commerce"].getInt("wallet") >= Fixed::fromInt(400).raw && !store.data()["ann"].getBool("online"));
        CHECK(store.absent(500).empty());
    }
    {   // the server starts again: her shop is hers before she comes, nobody else can buy it
        CityNetRig rig(o, &store);
        game::Commerce& c = *rig.server.module().commerce();
        Simulation& sim = rig.server.simulation();
        CHECK(c.store(sim)->premises[0].owner != 0 && c.store(sim)->premises[0].business == c.findBusiness("kiosk") && c.accountById(sim, cn::accountId("ann")));
        cn::Client& bob = rig.join(1, "bob");
        rig.run(25);
        mod(sim).state(sim).players[1].stock[0] = Fixed::fromInt(1000);
        CHECK(bob.submit(cityDeal(Op::Buy, 0)));
        rig.run(25);
        CHECK(bob.account().cash == Fixed::fromInt(1000).raw && c.slotOf(sim, c.store(sim)->premises[0].owner) == 0);
        cn::Client& ann = rig.join(2, "ann");   // another seat: the account is the player, not the seat
        rig.run(25);
        CHECK(ann.ready() && ann.account().cash >= Fixed::fromInt(400).raw && c.slotOf(sim, c.store(sim)->premises[0].owner) == 2);
        CHECK(!rig.server.sellOut("ann") && !rig.server.sellOut("nobody"));   // she is here
        ann.close();
        rig.clients.pop_back();
        rig.run(3);
        clock = 2000 + 700000;   // more than a week later
        CHECK(store.absent(604800) == std::vector<std::string>({"ann"}));
        const int64_t walletBefore = store.data()["ann"]["account"]["commerce"].getInt("wallet");
        CHECK(rig.server.sellOut("ann"));
        CHECK(c.store(sim)->premises[0].owner == 0 && c.store(sim)->premises[0].business == -1);
        CHECK(std::llabs(store.data()["ann"]["account"]["commerce"].getInt("wallet") - walletBefore - Fixed::fromInt(400).raw) < 700);   // 80% of 500 (0.8 is not exact)
        CHECK(store.data()["ann"]["account"]["commerce"]["premises"].size() == 0 && store.absent(604800).empty());
    }
}

TEST(city_accounts_in_the_meta_service) {
    auto modules = meta::modules::all();
    meta::modules::world(modules);
    meta::Service service(modules, meta::ServiceConfig::parse(Json::parseFile(std::string(AIVERSE_SOURCE_DIR) + "/examples/meta/service.json")));
    int64_t clock = 5000;
    cn::MetaAccounts store(service, [&] { return clock; });
    CHECK(store.enter("ann").isNull() && store.known().empty());
    Json doc = Json::parse(R"({"commerce": {"wallet": 42, "premises": []}, "career": {}})");
    store.save("ann", doc, false);
    clock = 5100;
    store.save("ann", doc, true);
    CHECK(store.failed() == 0 && store.known().size() == 1 && store.known()[0].first == "ann" && store.known()[0].second == doc);
    CHECK(store.absent(50).empty());
    clock = 5200;
    CHECK(store.absent(50) == std::vector<std::string>({"ann"}) && store.absent(500).empty());
    CHECK(store.enter("ann") == doc && store.absent(50).empty());
    // through a server: the same round trip as with a file
    cn::Options o;
    {
        CityNetRig rig(o, &store);
        cn::Client& ann = rig.join(1, "ann");
        rig.run(25);
        CHECK(ann.ready() && ann.account().cash == 42);
        ann.close();
        rig.clients.clear();
        rig.run(3);
    }
    CHECK(store.failed() == 0 && service.save()["extras"]["ann"]["world"]["account"]["commerce"].getInt("wallet") == 42 && !service.save()["extras"]["ann"]["world"].getBool("online"));
}

TEST(city_mirror_shows_the_server_world_in_a_local_simulation) {
    cn::Options o;
    o.radius = 8;
    CityNetRig rig(o, nullptr);
    cn::Client& a = rig.join(1, "");
    SimConfig cfg;
    Simulation local(cfg, std::make_unique<game::RtsModule>());
    local.setup(cityNetRules(), cityNetMap());
    Simulation& server = rig.server.simulation();
    const Entity heroS = rig.server.module().topDown()->heroOf(server, 1);
    {
        cn::Mirror mirror(local, a);
        auto frames = [&](int ticks) { for (int i = 0; i < ticks; ++i) { rig.run(1); mirror.update(0.05); } };
        frames(50);
        CHECK(mirror.hero().valid() && mirror.hero() == mod(local).topDown()->heroOf(local, 1) && mirror.shown() == 3);
        // the far hero and its dummy are not told: they are not in the local world any more
        size_t units = 0;
        local.world().each<game::Unit, Transform>([&](Entity, game::Unit&, Transform&) { ++units; });
        CHECK(units == 3);
        // the stick goes to the server through the local simulation; the local one never steps
        steer(local, 1, kEast);
        frames(40);
        CHECK(local.tick() == 0 && rig.server.stats().commands == 1 && tdPos(server, heroS).x > Fixed::fromInt(8));
        const Vec3 shown = tdPos(local, mirror.hero());
        CHECK(fxAbs(shown.x - tdPos(server, heroS).x) < Fixed::fromInt(1) && fxAbs(shown.z - tdPos(server, heroS).z) < Fixed::fromRatio(1, 10));
        CHECK(mirror.alpha() > 0 && mirror.alpha() <= 1);
        CHECK(local.world().get<game::Unit>(mirror.hero()).facing.x > Fixed::fromRatio(9, 10));   // he looks east
        // something new appears on the server next to him: it is created here under another local id
        steer(local, 1, kEast, 0);
        frames(10);
        const Vec3 at = tdPos(server, heroS) + Vec3{Fixed{}, Fixed{}, Fixed::fromInt(2)};
        const Entity born = mod(server).spawn(server, uint16_t(mod(server).rules().findType("mover")), 2, at);
        frames(10);
        const Entity here = mirror.localOf(born.index);
        CHECK(here.valid() && local.world().alive(here) && local.world().get<game::Unit>(here).type == uint16_t(mod(server).rules().findType("mover")));
        CHECK(fxAbs(tdPos(local, here).x - tdPos(server, born).x) < Fixed::fromInt(1));
        // it dies there: it goes here
        server.world().destroy(born);
        frames(70);
        CHECK(!mirror.localOf(born.index).valid() && !local.world().alive(here));
    }
    CHECK(!local.hasCommandSink());
}

TEST(city_server_tickets_empty_seats_events_and_premises) {
    using Op = game::CommerceOp;
    int64_t clock = 1000;
    cn::FileAccounts store("", [&] { return clock; });
    cn::Options o;
    o.radius = 8;
    o.key.assign(32, 7);
    o.clock = [&] { return clock; };
    CityNetRig rig(o, &store, true);
    Simulation& sim = rig.server.simulation();
    game::TopDown& td = *rig.server.module().topDown();
    game::Commerce& c = *rig.server.module().commerce();
    CHECK(rig.server.seats() == std::vector<uint8_t>({1, 2}) && !td.heroOf(sim, 1).valid() && !td.heroOf(sim, 2).valid());   // nobody is in the city
    rig.run(30);
    CHECK(!td.heroOf(sim, 1).valid());
    // no ticket, a forged one, an expired one: no seat
    cn::Client& none = rig.knock("ann", {});
    cn::Client& forged = rig.knock("ann", cn::ticket(std::vector<uint8_t>(32, 8), "ann", 5000));
    cn::Client& stolen = rig.knock("bob", cn::ticket(o.key, "ann", 5000));
    cn::Client& late = rig.knock("ann", cn::ticket(o.key, "ann", 999));
    rig.run(10);
    CHECK(!none.ready() && !forged.ready() && !stolen.ready() && !late.ready() && rig.server.stats().rejected == 4 && rig.server.clients() == 0);
    CHECK(!none.connected() && !td.heroOf(sim, 1).valid());
    rig.clients.clear();
    // a good one: a seat, and her hero walks in at the seat's place
    cn::Client& ann = rig.knock("ann", cn::ticket(o.key, "ann", 5000));
    rig.run(30);
    CHECK(ann.ready() && ann.slot() == 1 && rig.server.clients() == 1 && td.heroOf(sim, 1).valid() && !td.heroOf(sim, 2).valid());
    CHECK(tdPos(sim, td.heroOf(sim, 1)).x == Fixed::fromRatio(9, 2) && ann.entities().count(ann.hero()) && ann.hero() == td.heroOf(sim, 1).index);
    CHECK(c.account(sim, 1) && c.account(sim, 1)->id == cn::accountId("ann"));
    cn::Client& bob = rig.knock("bob", cn::ticket(o.key, "bob", 5000));
    rig.run(30);
    CHECK(bob.ready() && bob.slot() == 2 && td.heroOf(sim, 2).valid());
    // premises: she buys the kiosk and opens it; she sees it as hers with its books, he sees somebody else's
    mod(sim).state(sim).players[1].stock[0] = Fixed::fromInt(1000);
    CHECK(ann.submit(cityDeal(Op::Buy, 0)) && ann.submit(cityDeal(Op::Open, 0, c.findBusiness("kiosk"))));
    rig.run(45);
    CHECK(ann.premises().size() == 2 && ann.premises()[0].owner == 1 && ann.premises()[0].tenant == 0 && ann.premises()[0].business == c.findBusiness("kiosk"));
    CHECK(bob.premises().size() == 2 && bob.premises()[0].owner == 2 && bob.premises()[0].business == c.findBusiness("kiosk") && bob.premises()[1].owner == 0);
    c.store(sim)->premises[0].total = Fixed::fromInt(77);
    rig.run(25);
    CHECK(ann.premises()[0].total == Fixed::fromInt(77).raw && bob.premises()[0].total == 0);
    // events of the simulation: the one next to her reaches her, the one about him reaches him
    const Entity heroA = td.heroOf(sim, 1);
    bool emit = true;
    sim.addSystem("test.emit", [&](Simulation& s) {
        if (!emit) return;
        emit = false;
        SimEvent near; near.type = 901; near.a = heroA; near.value = -5; near.pos = Vec3{Fixed::fromInt(5), Fixed{}, Fixed::fromInt(5)};
        SimEvent his; his.type = 902; his.player = 2;
        s.emit(near); s.emit(his);
    });
    rig.run(6);
    const auto toAnn = ann.takeSimEvents(), toBob = bob.takeSimEvents();
    size_t annNear = 0, annHis = 0, bobNear = 0, bobHis = 0;
    for (auto& e : toAnn) { annNear += e.type == 901 && e.a == heroA.index && e.value == -5 && e.x == 5 * 64; annHis += e.type == 902; }
    for (auto& e : toBob) { bobNear += e.type == 901; bobHis += e.type == 902 && e.player == 2 && e.a == UINT32_MAX; }
    CHECK(annNear == 1 && annHis == 0 && bobNear == 0 && bobHis == 1);
    // she walks east and leaves: her hero is gone from the city; she comes back where she left
    game::topdown::Input in;
    in.moveAngle = game::TopDown::angleOf(kEast); in.moveMag = 1000;
    CHECK(ann.submit(net::heroInputCommand(1, in)));
    rig.run(30);
    const Fixed left = tdPos(sim, td.heroOf(sim, 1)).x;
    CHECK(left > Fixed::fromInt(8));
    ann.close();
    rig.run(30);
    CHECK(rig.server.clients() == 1 && !td.heroOf(sim, 1).valid() && !sim.world().alive(heroA) && c.store(sim)->premises[0].owner != 0);
    cn::Client& again = rig.knock("ann", cn::ticket(o.key, "ann", 5000));
    rig.run(30);
    CHECK(again.ready() && again.slot() == 1 && td.heroOf(sim, 1).valid() && fxAbs(tdPos(sim, td.heroOf(sim, 1)).x - left) < Fixed::fromInt(1));
    CHECK(again.premises().size() == 2 && again.premises()[0].owner == 1);
}

TEST(city_mirror_predicts_the_hero_and_shows_events_and_premises) {
    using Op = game::CommerceOp;
    cn::Options o;
    o.radius = 8;
    auto shownAfterPush = [&](bool predict, Fixed& serverX, Fixed& serverLater, Fixed& restGap) {
        CityNetRig rig(o, nullptr, true);
        cn::Client& a = rig.knock("ann", {}, 100);   // a tenth of a second each way
        Simulation local(SimConfig{}, std::make_unique<game::RtsModule>());
        local.setup(cityNetRules(), cityNetMap());
        cn::Mirror mirror(local, a);
        mirror.setPrediction(predict);
        auto frames = [&](int ticks) { for (int i = 0; i < ticks; ++i) { rig.run(1); mirror.update(0.05); } };
        frames(60);
        Simulation& server = rig.server.simulation();
        CHECK(a.ready() && mirror.hero().valid() && mirror.latency() > 0.15);
        steer(local, a.slot(), kSouth);
        frames(8);   // 0.4 s: the command is barely there and back
        const Fixed shown = tdPos(local, mirror.hero()).z;
        serverX = tdPos(server, rig.server.module().topDown()->heroOf(server, a.slot())).z;
        frames(6);   // he keeps pushing: where the server has him once the stick's last word arrived
        serverLater = tdPos(server, rig.server.module().topDown()->heroOf(server, a.slot())).z;
        steer(local, a.slot(), kSouth, 0);
        frames(40);
        restGap = fxAbs(tdPos(local, mirror.hero()).z - tdPos(server, rig.server.module().topDown()->heroOf(server, a.slot())).z);
        return shown;
    };
    Fixed serverPlain, serverLead, laterPlain, laterLead, gapPlain, gapLead;
    const Fixed plain = shownAfterPush(false, serverPlain, laterPlain, gapPlain), lead = shownAfterPush(true, serverLead, laterLead, gapLead);
    CHECK(serverPlain == serverLead);                                  // the server does the same either way
    CHECK(lead > plain + Fixed::fromRatio(3, 4) && lead > serverLead && lead <= laterLead + Fixed::fromRatio(3, 4));   // drawn ahead: about where the server will have him when it has heard the stick
    CHECK(plain < serverPlain);                                        // without it the picture is behind
    CHECK(gapPlain < Fixed::fromRatio(1, 10) && gapLead < Fixed::fromRatio(1, 10));   // at rest both stand where the server says

    // events and premises reach the local simulation
    CityNetRig rig(o, nullptr, true);
    cn::Client& a = rig.knock("ann", {});
    Simulation local(SimConfig{}, std::make_unique<game::RtsModule>());
    local.setup(cityNetRules(), cityNetMap());
    std::vector<SimEvent> heard;
    int stepped = 0;
    local.addSystem("test.collect", [&](Simulation& s) { heard.insert(heard.end(), s.events().begin(), s.events().end()); });
    local.addSystem("test.logic", [&](Simulation&) { ++stepped; });
    cn::Mirror mirror(local, a);
    auto frames = [&](int ticks) { for (int i = 0; i < ticks; ++i) { rig.run(1); mirror.update(0.05); } };
    frames(60);
    Simulation& server = rig.server.simulation();
    const Entity heroS = rig.server.module().topDown()->heroOf(server, 1);
    bool emit = true;
    server.addSystem("test.emit", [&](Simulation& s) { if (emit) { emit = false; SimEvent e; e.type = 903; e.a = heroS; e.pos = Vec3{Fixed::fromInt(5), Fixed{}, Fixed::fromInt(5)}; s.emit(e); } });
    frames(10);
    size_t got = 0;
    for (auto& e : heard) got += e.type == 903 && e.a == mirror.hero() && e.pos.x == Fixed::fromInt(5);
    CHECK(got == 1 && stepped == 0);   // told once to the collectors; nothing else ran
    {   // a projectile in flight on the server is one here, with its shooter
        const Entity pe = server.world().create();
        const Vec3 from = tdPos(server, heroS) + Vec3{Fixed{}, Fixed::fromInt(1), Fixed::fromInt(-2)};
        server.world().add<Transform>(pe, Transform{from, from, Fixed{}});
        game::Projectile pr;
        pr.source = heroS; pr.owner = 1; pr.speed = Fixed::fromRatio(1, 2); pr.aim = from + Vec3{Fixed{}, Fixed{}, Fixed::fromInt(-2)}; pr.start = from; pr.total = Fixed::fromInt(2);
        server.world().add<game::Projectile>(pe, pr);
        size_t seenFlying = 0, withShooter = 0;
        for (int i = 0; i < 6; ++i) {
            frames(1);
            local.world().each<game::Projectile, Transform>([&](Entity, game::Projectile& lp, Transform& lt) {
                ++seenFlying; withShooter += lp.source == mirror.hero() && fxAbs(lt.pos.y - Fixed::fromInt(1)) < Fixed::fromRatio(1, 4);
            });
        }
        CHECK(seenFlying >= 2 && withShooter == seenFlying);
        if (server.world().alive(pe)) server.world().destroy(pe);
        frames(6);
        size_t left = 0;
        local.world().each<game::Projectile, Transform>([&](Entity, game::Projectile&, Transform&) { ++left; });
        CHECK(left == 0 && a.shots().empty());
    }
    game::Commerce& cs = *rig.server.module().commerce();
    game::Commerce& cl = *mod(local).commerce();
    mod(server).state(server).players[1].stock[0] = Fixed::fromInt(1000);
    local.submit(cityDeal(Op::Buy, 0));   // through the local simulation's sink
    frames(45);
    CHECK(cs.store(server)->premises[0].owner != 0 && cl.account(local, 1) && cl.store(local)->premises[0].owner == cl.store(local)->slotRec[1]);
    CHECK(cl.cash(local, 1) == Fixed::fromInt(500) && std::string(cl.refusal(local, 1, Op::Buy, 0, 0)) != "");   // hers already: the menu knows
    cs.store(server)->premises[1].owner = 65000 < cs.store(server)->accounts.size() ? 0 : uint16_t(cs.store(server)->accounts.size());   // somebody else (a record she is not)
    cs.store(server)->accounts.emplace_back();
    frames(45);
    CHECK(cl.store(local)->premises[1].owner != 0 && cl.store(local)->premises[1].owner != cl.store(local)->slotRec[1] && cl.slotOf(local, cl.store(local)->premises[1].owner) == 0);
}
