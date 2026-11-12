// Tests for battle networking (docs/battle-net.md): crypto + tickets, the secure UDP transport over
// the simulated mobile network (loss, jitter, NAT rebinding, replays), hero input, hosted battles
// (allocation, seating, payload, results consensus, desync, suspend/resume) and the control plane.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <thread>
#include <vector>
#include "apps/battle/BattleLoad.h"
#include "apps/battle/HeroDuel.h"
#include "engine/net/HeroInput.h"
#include "engine/net/Conn.h"
#include "engine/net/MatchTicket.h"
#include "engine/net/NetClient.h"
#include "engine/net/SecureUdp.h"
#include "engine/net/SimNet.h"
#include "engine/net/Smoothing.h"
#include "engine/net/server/BattleControl.h"

using namespace aiverse;

static int g_failed = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failed; std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
static std::vector<std::pair<const char*, std::function<void()>>>& registry() { static std::vector<std::pair<const char*, std::function<void()>>> r; return r; }
struct Reg { Reg(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); } };
#define TEST(name) static void name(); static Reg reg_##name(#name, name); static void name()

static const uint64_t kUnix = 1'800'000'000'000ull;

static net::TicketClaims claims(uint64_t match, const std::string& player, uint8_t slot, const std::string& compat = "1.0|protocol:3|map:x") {
    net::TicketClaims c;
    c.matchId = match; c.playerId = player; c.slot = slot; c.compatHash = net::compatibilityHash(compat); c.expiresAtMs = kUnix + 600000;
    return c;
}

// ---------------- crypto + tickets
TEST(crypto_aead_detects_tampering) {
    net::Key k = net::randomKey(), other = net::randomKey();
    net::Nonce n{}; n[0] = 7;
    const uint8_t ad[3] = {1, 2, 3};
    const std::string text = "attack at dawn";
    auto sealed = net::aeadSeal(k, n, ad, 3, reinterpret_cast<const uint8_t*>(text.data()), text.size());
    net::Bytes plain;
    CHECK(net::aeadOpen(k, n, ad, 3, sealed.data(), sealed.size(), plain) && std::string(plain.begin(), plain.end()) == text);
    auto bad = sealed; bad[2] ^= 1;
    CHECK(!net::aeadOpen(k, n, ad, 3, bad.data(), bad.size(), plain));
    CHECK(!net::aeadOpen(other, n, ad, 3, sealed.data(), sealed.size(), plain));
    const uint8_t ad2[3] = {1, 2, 4};
    CHECK(!net::aeadOpen(k, n, ad2, 3, sealed.data(), sealed.size(), plain));
    CHECK(net::toHexString(k.data(), 32).size() == 64 && net::keyFromHex(net::toHexString(k.data(), 32)) == k);
}

TEST(ticket_opens_only_with_the_server_key_and_in_time) {
    net::Key key = net::randomKey();
    auto t = net::issueTicket(key, claims(42, "alice", 2), "10.0.0.1:27017");
    auto c = net::openTicket(key, t.sealed, kUnix);
    CHECK(c && c->matchId == 42 && c->playerId == "alice" && c->slot == 2 && c->c2s == t.claims.c2s && c->s2c == t.claims.s2c);
    CHECK(!net::openTicket(key, t.sealed, kUnix + 600000));                // expired
    CHECK(!net::openTicket(net::randomKey(), t.sealed, kUnix));            // another server's key
    auto forged = t.sealed; forged[8] ^= 1;                                 // match id in the public header
    CHECK(!net::openTicket(key, forged, kUnix));
    auto back = net::ClientTicket::decode(t.encode());
    CHECK(back && back->server == "10.0.0.1:27017" && back->sealed == t.sealed && back->claims.c2s == t.claims.c2s && back->claims.playerId == "alice");
    CHECK(!net::ClientTicket::decode("aivt1.zz"));
    CHECK(net::compatibilityKey("1.2.3", 14, "square") == "1.2.3|protocol:14|map:square");
    CHECK(net::compatibilityHash("a") != net::compatibilityHash("b"));
}

// ---------------- secure transport on the simulated network
struct SecurePair {
    net::SimNetwork simnet;
    net::Key key = net::randomKey();
    std::unique_ptr<net::SecureListener> server;
    std::unique_ptr<net::Conn> client, peer;
    net::Address serverAddr = net::SimNetwork::address("10.0.0.1", 27017);
    net::Address clientAddr = net::SimNetwork::address("10.1.0.1", 40000);
    uint64_t now = 1000;
    explicit SecurePair(net::LinkProfile link = {}, uint32_t redundancyMs = 30, std::function<bool(const net::TicketClaims&)> admit = {}, uint64_t seed = 5)
        : simnet(seed) {
        net::SecureServerConfig sc;
        sc.serverKey = key;
        sc.admit = admit ? admit : [](const net::TicketClaims& c) { return c.matchId == 42; };
        sc.clock = [this] { return kUnix + now; };
        sc.link.redundancyMs = redundancyMs;
        server = std::make_unique<net::SecureListener>(sc);
        server->attach(simnet.open("10.0.0.1", 27017));
        net::SecureLinkConfig lc; lc.redundancyMs = redundancyMs;
        client = net::connectSecure(simnet.open("10.1.0.1", 40000, link), serverAddr, net::issueTicket(key, claims(42, "alice", 1), "x:1"), lc);
    }
    void step(uint32_t ms = 10) {
        now += ms; simnet.setTime(now);
        client->update(now);
        for (auto& a : server->poll(now)) { peer = std::move(a.conn); CHECK(a.claims.playerId == "alice" && a.claims.slot == 1); }
        if (peer) peer->update(now);
    }
    bool connect(uint32_t maxMs = 5000) {
        for (uint32_t t = 0; t < maxMs && !(peer && client->state() == net::ConnState::Connected); t += 10) step();
        return peer && client->state() == net::ConnState::Connected;
    }
};

TEST(secure_handshake_and_both_directions) {
    SecurePair p;
    CHECK(p.connect());
    p.client->send({1, 2, 3});
    p.peer->send({9, 8});
    net::Bytes got;
    bool c2s = false, s2c = false;
    for (int i = 0; i < 50; ++i) {
        p.step();
        while (p.peer->recv(got)) c2s |= got == net::Bytes{1, 2, 3};
        while (p.client->recv(got)) s2c |= got == net::Bytes{9, 8};
    }
    CHECK(c2s && s2c);
    CHECK(p.server->stats().handshakes == 1 && p.server->sessions() == 1);
    p.client->close();
    for (int i = 0; i < 20; ++i) p.step();
    CHECK(p.peer->state() == net::ConnState::Closed);
}

TEST(secure_rejects_unadmitted_tickets_and_garbage) {
    SecurePair p({}, 30, [](const net::TicketClaims&) { return false; });
    CHECK(!p.connect(1500));
    CHECK(p.server->stats().rejectedTickets > 0 && !p.peer);
    // Garbage and a truncated request from a stranger: counted, never answered with a session.
    auto stranger = p.simnet.open("10.6.6.6", 5555);
    std::vector<uint8_t> junk(1000, 0xA1);
    stranger->sendTo(p.serverAddr, junk.data(), junk.size());
    std::vector<uint8_t> data(64, 0xA4);
    stranger->sendTo(p.serverAddr, data.data(), data.size());
    uint8_t x = 0x77;
    stranger->sendTo(p.serverAddr, &x, 1);
    for (int i = 0; i < 10; ++i) p.step();
    CHECK(p.server->stats().badPackets >= 2 && p.server->sessions() == 0);
}

// Records what the client sends so the test can replay it from another address.
struct TapPort : net::DatagramPort {
    std::unique_ptr<net::DatagramPort> inner;
    std::vector<std::vector<uint8_t>>* log;
    bool sendTo(const net::Address& to, const uint8_t* d, size_t n) override { log->emplace_back(d, d + n); return inner->sendTo(to, d, n); }
    int recvFrom(net::Address& from, uint8_t* b, size_t cap) override { return inner->recvFrom(from, b, cap); }
    uint16_t localPort() const override { return inner->localPort(); }
};

TEST(secure_drops_replays_and_survives_nat_rebinding) {
    SecurePair p;
    std::vector<std::vector<uint8_t>> sent;
    auto tap = std::make_unique<TapPort>();
    tap->inner = p.simnet.open("10.1.0.2", 40001);
    tap->log = &sent;
    p.client = net::connectSecure(std::move(tap), p.serverAddr, net::issueTicket(p.key, claims(42, "alice", 1), "x:1"));
    CHECK(p.connect());
    for (int i = 0; i < 20; ++i) { p.client->send({uint8_t(i)}); p.step(); }
    net::Bytes got;
    int received = 0;
    for (int i = 0; i < 30; ++i) { p.step(); while (p.peer->recv(got)) ++received; }
    CHECK(received == 20);
    // An attacker replays captured datagrams (from the same and from another address): ignored.
    auto attacker = p.simnet.open("10.6.6.7", 6666);
    const auto before = p.server->stats().replays;
    for (auto& d : sent) if (!d.empty() && d[0] == 0xA4) attacker->sendTo(p.serverAddr, d.data(), d.size());
    for (int i = 0; i < 10; ++i) { p.step(); while (p.peer->recv(got)) ++received; }
    CHECK(received == 20);
    CHECK(p.server->stats().replays > before);
    CHECK(p.server->stats().migrations == 0);
    // Wi-Fi -> LTE: the phone now appears from another address; the session follows it.
    p.simnet.rebind(net::SimNetwork::address("10.1.0.2", 40001), "10.2.0.9", 50000);
    for (int i = 0; i < 10; ++i) { p.client->send({uint8_t(100 + i)}); p.peer->send({uint8_t(200 + i)}); p.step(); }
    int up = 0, down = 0;
    for (int i = 0; i < 40; ++i) { p.step(); while (p.peer->recv(got)) up += got[0] >= 100; while (p.client->recv(got)) down += got[0] >= 200; }
    CHECK(up == 10 && down == 10);
    CHECK(p.server->stats().migrations == 1);
    CHECK(p.client->state() == net::ConnState::Connected && p.peer->state() == net::ConnState::Connected);
}

// Delivery latency of a 20 Hz input stream on a bad mobile link, with and without redundancy.
static std::pair<int, uint64_t> lossyStream(uint32_t redundancyMs, uint32_t lossPercent) {
    net::LinkProfile link;
    link.latencyMs = 75; link.jitterMs = 40; link.lossPercent = lossPercent; link.burstPercent = 1; link.burstLength = 4;
    SecurePair p(link, redundancyMs, {}, 11 + lossPercent);
    if (!p.connect(15000)) return {0, 0};
    std::vector<uint64_t> sentAt;
    std::vector<uint64_t> lat;
    net::Bytes got;
    for (int i = 0; i < 400; ++i) {   // 20 seconds
        net::Bytes m(12, uint8_t(i)); m[0] = uint8_t(i); m[1] = uint8_t(i >> 8);
        p.client->send(m);
        sentAt.push_back(p.now);
        for (int k = 0; k < 5; ++k) { p.step(); while (p.peer->recv(got)) lat.push_back(p.now - sentAt[size_t(got[0] | got[1] << 8)]); }
    }
    for (int k = 0; k < 300; ++k) { p.step(); while (p.peer->recv(got)) lat.push_back(p.now - sentAt[size_t(got[0] | got[1] << 8)]); }
    std::sort(lat.begin(), lat.end());
    return {int(lat.size()), lat.empty() ? 0 : lat[lat.size() * 95 / 100]};
}

TEST(secure_stream_tolerates_mobile_loss) {
    for (uint32_t loss : {10u, 20u}) {
        auto on = lossyStream(30, loss), off = lossyStream(0, loss);
        std::printf("  %u%% loss, 150 ms RTT, 40 ms jitter: p95 one-way %llu ms with redundancy, %llu ms without (%d/%d delivered)\n", loss,
                    (unsigned long long)on.second, (unsigned long long)off.second, on.first, off.first);
        CHECK(on.first == 400 && off.first == 400);   // reliable and ordered either way
        CHECK(on.second <= off.second);
        CHECK(on.second < 300);                       // one-way p95 stays within ~ two ticks of the 95 ms base
    }
}

TEST(secure_ping_for_server_selection) {
    SecurePair p;
    net::LinkProfile l; l.latencyMs = 30;
    auto port = p.simnet.open("10.3.0.1", 7000, l);
    p.server->setLoad(37);
    p.step();
    net::sendBattlePing(*port, p.serverAddr, p.now);
    uint32_t rtt = 0; uint8_t load = 0; bool got = false;
    for (int i = 0; i < 20 && !got; ++i) {
        p.step();
        uint8_t buf[64]; net::Address from; int n;
        while ((n = port->recvFrom(from, buf, sizeof(buf))) >= 0) got |= net::readBattlePong(buf, size_t(n), p.now, rtt, load);
    }
    std::printf("  pong: %d, rtt %u ms, load %u\n", int(got), rtt, load);
    CHECK(got && rtt >= 60 && rtt <= 70 && load == 37);
}

TEST(secure_real_udp_on_loopback) {
    net::Key key = net::randomKey();
    net::SecureServerConfig sc;
    sc.serverKey = key;
    sc.admit = [](const net::TicketClaims&) { return true; };
    net::SecureListener server(sc);
    if (!server.open(0, true)) { std::printf("  (no UDP sockets here, skipped)\n"); return; }
    auto t = net::issueTicket(key, [] { auto c = claims(1, "bob", 1); c.expiresAtMs = net::unixMs() + 60000; return c; }(),
                              "127.0.0.1:" + std::to_string(server.port()));
    auto client = net::connectSecure(t);
    CHECK(client != nullptr);
    if (!client) return;
    std::unique_ptr<net::Conn> peer;
    net::Bytes got;
    bool echoed = false;
    const uint64_t start = net::nowMs();
    while (net::nowMs() - start < 3000 && !echoed) {
        const uint64_t now = net::nowMs();
        client->update(now);
        for (auto& a : server.poll(now)) peer = std::move(a.conn);
        if (peer) { peer->update(now); while (peer->recv(got)) peer->send(got); }
        if (client->state() == net::ConnState::Connected) client->send({4, 2});
        while (client->recv(got)) echoed |= got == net::Bytes{4, 2};
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(echoed);
}

// ---------------- hero input + smoothing
TEST(hero_input_is_sticky_quantized_state) {
    net::HeroInputState s;
    s.moveSector = 17; s.moveMagnitude = 200; s.aimMilli = 123000; s.held = 5;
    CHECK(net::unpackHeroInput(net::packHeroInput(s)) == s);
    net::HeroInputState released;
    CHECK(net::unpackHeroInput(net::packHeroInput(released)) == released && !net::heroAiming(released));
    CHECK(net::unpackHeroInput(int64_t(-1)).aimMilli <= net::kAimReleased);   // hostile bits are clamped
    auto v = net::heroMoveVector(s, 32);   // sector 17 of 32 = half a turn: pointing to -x
    CHECK(v.x < Fixed{} && fxAbs(v.y) < Fixed::fromRatio(1, 100));
    net::HeroInputQuantizer q;
    CHECK(q.update(1, 0, 0, 0, 0).has_value());
    CHECK(!q.update(0.99f, 0.01f, 0, 0, 0).has_value());     // same sector + magnitude: nothing to send
    auto c = q.update(0, 1, 0, 1, 1);
    CHECK(c && c->moveSector == 9 && c->held == 1 && c->aimMilli == 90000);
    CHECK(q.update(0.05f, 0, 0, 0, 0)->moveSector == 0);     // dead zone: stop
    auto a = net::heroActionCommand(2, 3, 1, true, {Fixed::fromInt(4), {}, {}});
    CHECK(a.type == net::kCmdHeroAction && net::heroActionIndex(a) == 3 && net::heroActionPhase(a) == 1 && net::heroActionHasPoint(a));
}

TEST(correction_smoother_blends_and_snaps) {
    net::CorrectionSmoother s({50.0f, 4.0f});
    s.correct(1, 1.0f, 0.0f);
    float dx, dy;
    s.offset(1, dx, dy);
    CHECK(dx == 1.0f && dy == 0.0f);
    s.advance(50.0f);
    s.offset(1, dx, dy);
    CHECK(dx > 0.49f && dx < 0.51f);
    s.correct(2, 10.0f, 0.0f);                 // teleport: not smoothed
    s.offset(2, dx, dy);
    CHECK(dx == 0.0f);
    for (int i = 0; i < 40; ++i) s.advance(16.0f);
    CHECK(s.active() == 0);
}

// ---------------- hosted battles
static net::MatchAllocation allocation(uint64_t id, int humans = 2) {
    net::MatchAllocation a;
    a.matchId = id; a.gameId = "g"; a.compatKey = "1.0|protocol:3|map:x"; a.mapId = "x";
    a.map.file = "heroduel"; a.map.dataHash = 1; a.map.slotTeams = {1, 2};
    a.settings = net::battleSettings();
    for (int i = 0; i < 2; ++i) {
        net::MatchAllocation::Player p;
        p.info.slot = uint8_t(i + 1); p.info.team = uint8_t(i + 1);
        if (i < humans) p.playerId = "u" + std::to_string(i + 1); else p.info.kind = net::SlotKind::Bot;
        a.players.push_back(p);
    }
    return a;
}

TEST(hosted_allocation_validation_and_admission) {
    net::ServerConfig cfg; cfg.udpPort = cfg.wsPort = 0; cfg.log = false; cfg.hostedOnly = true;
    net::Server srv(cfg);
    std::string err;
    auto bad = allocation(5); bad.players[1].info.slot = 1;
    CHECK(!srv.allocateMatch(bad, &err) && err.find("twice") != std::string::npos);
    auto bots = allocation(6, 0);
    CHECK(!srv.allocateMatch(bots, &err) && err.find("no human") != std::string::npos);
    CHECK(srv.allocateMatch(allocation(7), &err));
    CHECK(!srv.allocateMatch(allocation(7), &err));
    CHECK(srv.admits(claims(7, "u1", 1)));
    CHECK(!srv.admits(claims(7, "u1", 2)));                         // wrong slot
    CHECK(!srv.admits(claims(7, "mallory", 1)));                    // not in the match
    CHECK(!srv.admits(claims(7, "u1", 1, "0.9|protocol:3|map:x"))); // other build
    CHECK(!srv.admits(claims(8, "u1", 1)));
    // No one shows up: the join timeout ends the allocation with an aborted report.
    auto quick = allocation(9); quick.joinTimeoutMs = 1000;
    CHECK(srv.allocateMatch(quick, &err));
    srv.update(10); srv.update(2000);
    auto reps = srv.takeReports();
    CHECK(reps.size() == 1 && reps[0].matchId == 9 && reps[0].aborted && reps[0].reason.key == "net.end.no_show");
    CHECK(srv.releaseMatch(7) && !srv.releaseMatch(7));
    reps = srv.takeReports();
    CHECK(reps.size() == 1 && reps[0].aborted && reps[0].reason.key == "net.end.released");
    // A plain (ticketless) connection may not even say Hello on a hosted-only server.
    auto [a, b] = net::makeLoopbackPair();
    srv.addConnection(std::move(b));
    net::HelloMsg h; h.name = "x"; h.gameId = "g";
    a->send(net::encode(h));
    bool refused = false;
    for (uint64_t t = 3000; t < 3500 && !refused; t += 10) {
        a->update(t); srv.update(t);
        net::Bytes m;
        while (a->recv(m)) {
            ByteReader r(m.data(), m.size());
            net::ErrorMsg e;
            if (net::peekType(m) == net::Msg::Error && r.u8() && net::decode(r, e)) refused |= e.text.key == "net.error.ticket_required";
        }
    }
    CHECK(refused);
}

TEST(hosted_battles_on_a_bad_mobile_link) {
    battle::BattleLoadConfig cfg;
    cfg.matches = 3; cfg.playersPerMatch = 2; cfg.seconds = 15;
    cfg.link = net::linkPreset("bad");   // 20% loss + bursts, 150 ms RTT, 40 ms jitter, duplicates
    auto r = battle::runBattleLoad(cfg);
    std::printf("  %s\n", r.summary().c_str());
    CHECK(r.matches == 3 && r.finished == 3 && r.consensus == 3);
    CHECK(r.desyncs == 0 && r.brokenSessions == 0);
    CHECK(r.rollbacks > 0);                       // remote input changes are predicted, then corrected
    CHECK(r.maxRollback <= net::battleSettings().rollbackWindow);
    for (auto& rep : r.reports) {
        CHECK(rep.players.size() == 2 && !rep.report.empty() && rep.result != 0);
        for (auto& p : rep.players) CHECK(p.joined && p.connectedAtEnd && p.report == rep.report && p.droppedCommands == 0);
    }
}

TEST(hosted_battle_survives_suspend_and_network_change) {
    battle::BattleLoadConfig cfg;
    cfg.matches = 1; cfg.playersPerMatch = 2; cfg.seconds = 20;
    cfg.link = net::linkPreset("4g");
    cfg.suspendOne = true; cfg.suspendMs = 6000;   // app in the background for 6 s mid-match
    cfg.rebindOne = true;                          // the other phone changes its address
    auto r = battle::runBattleLoad(cfg);
    std::printf("  %s\n", r.summary().c_str());
    CHECK(r.finished == 1 && r.consensus == 1 && r.desyncs == 0 && r.brokenSessions == 0);
    CHECK(r.reconnects >= 1);                       // resume after a long suspend reconnects at once
    CHECK(r.migrations >= 1);                       // the rebound phone kept its session
    CHECK(r.handshakes >= 3);
}

TEST(hosted_battle_reports_desync) {
    battle::BattleLoadConfig cfg;
    cfg.matches = 1; cfg.playersPerMatch = 2; cfg.seconds = 8;
    cfg.link = net::linkPreset("wifi");
    cfg.desyncOne = true;
    auto r = battle::runBattleLoad(cfg);
    CHECK(r.desyncs >= 1);
    CHECK(r.reports.size() == 1 && r.reports[0].desync && !r.reports[0].consensus);
}

// ---------------- control plane
TEST(battle_control_requests_are_signed_fresh_and_unique) {
    net::ServerConfig cfg; cfg.udpPort = cfg.wsPort = 0; cfg.log = false; cfg.hostedOnly = true;
    net::Server srv(cfg);
    net::BattleControlConfig bc;
    bc.controlKey = net::randomKey(); bc.maxMatches = 1; bc.publicAddress = "203.0.113.5:27017";
    uint64_t clock = kUnix;
    bc.clock = [&] { return clock; };
    net::BattleControl ctl(srv, bc);
    auto call = [&](const std::string& json, const net::Key& key) {
        std::string out;
        CHECK(net::verifyControl(bc.controlKey, ctl.handleLine(net::signControl(key, json)), out));
        return Json::parse(out);
    };
    const std::string ts = std::to_string(kUnix);
    const std::string alloc = R"({"op":"allocate","ts":")" + ts + R"(","nonce":"n1","match":{"matchId":"77","gameId":"g","compatKey":"1.0|protocol:3|map:x",
        "map":{"file":"heroduel","dataHash":"ab","slotTeams":[1,2]},"seed":"5","players":[{"playerId":"u1","slot":1,"loadout":"0a0b"},{"slot":2,"bot":true}]}})";
    Json r = call(alloc, bc.controlKey);
    CHECK(r.getBool("ok") && r.getStr("matchId") == "77" && r.getStr("server") == "203.0.113.5:27017");
    CHECK(srv.admits(claims(77, "u1", 1)));
    CHECK(!call(alloc, bc.controlKey).getBool("ok"));                                   // replayed nonce
    CHECK(call(alloc, net::randomKey()).getStr("error") == "bad signature");           // wrong key
    CHECK(!call(R"({"op":"status","ts":")" + std::to_string(kUnix - 600000) + R"(","nonce":"n2"})", bc.controlKey).getBool("ok"));  // stale
    std::string full = alloc; full.replace(full.find("\"n1\""), 4, "\"n3\""); full.replace(full.find("\"77\""), 4, "\"78\"");
    CHECK(call(full, bc.controlKey).getStr("error").find("full") == 0);                // capacity 1
    Json st = call(R"({"op":"status","ts":")" + ts + R"(","nonce":"n4"})", bc.controlKey);
    CHECK(st.getInt("running") == 1 && st["matches"].size() == 1 && st["matches"][0].getStr("phase") == "waiting");
    CHECK(call(R"({"op":"release","ts":")" + ts + R"(","nonce":"n5","matchId":"77"})", bc.controlKey).getBool("ok"));
    Json reps = call(R"({"op":"reports","ts":")" + ts + R"(","nonce":"n6"})", bc.controlKey);
    CHECK(reps["reports"].size() == 1 && reps["reports"][0].getStr("matchId") == "77" && reps["reports"][0].getBool("aborted"));
}

int main(int argc, char** argv) {
    const std::string only = argc > 1 ? argv[1] : "";
    for (auto& [name, fn] : registry()) {
        if (!only.empty() && std::string(name).find(only) == std::string::npos) continue;
        std::printf("%s\n", name);
        fn();
    }
    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
