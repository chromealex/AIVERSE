// Hooks a game's native code uses to reach its own backend (docs/native-modules.md "Remote backends"):
// MessagePack and encodings, HTTP (plain server/client and the platform stack), matches formed by an
// external matchmaker (Server::reserve, tickets, spectators, the end report), server extensions and
// plugin events. Everything here is synthetic: no game-specific protocol.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "apps/server/GameData.h"
#include "engine/meta/Encoding.h"
#include "engine/meta/MsgPack.h"
#include "engine/net/NetClient.h"
#include "engine/net/PlainHttp.h"
#include "engine/net/server/Server.h"
#include "engine/net/server/ServerExtension.h"
#include "engine/pack/http/HttpRequest.h"
#include "engine/sim/Simulation.h"
#include "engine/game/ComputerPlayer.h"
#include "engine/game/GameCore.h"

using namespace aiverse;
using namespace aiverse::meta;
namespace mp = aiverse::meta::msgpack;

static int g_failed = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failed; std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
static std::vector<std::pair<const char*, std::function<void()>>>& registry() { static std::vector<std::pair<const char*, std::function<void()>>> r; return r; }
struct Reg { Reg(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); } };
#define TEST(name) static void name(); static Reg reg_##name(#name, name); static void name()

#ifndef AIVERSE_SOURCE_DIR
#define AIVERSE_SOURCE_DIR "."
#endif
static const std::string kMushrooms = std::string(AIVERSE_TEMPLATES_DIR) + "/mushrooms";
static std::string hex(const mp::Bytes& b) { return enc::hexUpper(b.data(), b.size()); }
static uint64_t wallMs() { return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); }

// ---------------------------------------------------------------- MessagePack, encodings

TEST(msgpack_spec_vectors) {
    CHECK(hex(mp::encode(mp::Value::integer(0))) == "00");
    CHECK(hex(mp::encode(mp::Value::integer(127))) == "7F");
    CHECK(hex(mp::encode(mp::Value::integer(128))) == "CC80");
    CHECK(hex(mp::encode(mp::Value::integer(300))) == "CD012C");
    CHECK(hex(mp::encode(mp::Value::integer(70000))) == "CE00011170");
    CHECK(hex(mp::encode(mp::Value::integer(1ll << 33))) == "CF0000000200000000");
    CHECK(hex(mp::encode(mp::Value::integer(-1))) == "FF");
    CHECK(hex(mp::encode(mp::Value::integer(-32))) == "E0");
    CHECK(hex(mp::encode(mp::Value::integer(-33))) == "D0DF");
    CHECK(hex(mp::encode(mp::Value::integer(-129))) == "D1FF7F");
    CHECK(hex(mp::encode(mp::Value::integer(-40000))) == "D2FFFF63C0");
    CHECK(hex(mp::encode(mp::Value::nil())) == "C0");
    CHECK(hex(mp::encode(mp::Value::boolean(true))) == "C3");
    CHECK(hex(mp::encode(mp::Value::float64(1.5))) == "CB3FF8000000000000");
    CHECK(hex(mp::encode(mp::Value::float32(1.5f))) == "CA3FC00000");
    const std::string s40(40, 'x');
    CHECK(hex(mp::encode(mp::Value::str(s40))).substr(0, 4) == "D928");          // str8 (2013 spec)
    CHECK(hex(mp::encode(mp::Value::str(s40), {true})).substr(0, 6) == "DA0028");  // raw16 (msgpack-java 0.6)
    CHECK(hex(mp::encode(mp::Value::bin({1, 2}))) == "C4020102");
    CHECK(hex(mp::encode(mp::Value::bin({1, 2}), {true})) == "A20102");
    CHECK(hex(mp::encode(mp::Value::ext(5, {9}))) == "D40509");
    mp::Value m = mp::Value::map();
    m.set("a", mp::Value::integer(1));
    m.set("b", mp::Value::array({mp::Value::nil(), mp::Value::str("hi")}));
    CHECK(hex(mp::encode(m)) == "82A16101A16292C0A26869");
}

TEST(msgpack_round_trip_and_limits) {
    mp::Value v = mp::Value::map();
    v.set("i", mp::Value::integer(-123456789012ll));
    v.set("u", mp::Value::uinteger(18446744073709551615ull));
    v.set("f", mp::Value::float64(-0.25));
    v.set("s", mp::Value::str(std::string(70000, 'z')));
    v.set("b", mp::Value::bin(mp::Bytes(300, 7)));
    v.set("e", mp::Value::ext(-1, {0, 0, 0, 1}));
    mp::Value arr = mp::Value::array();
    for (int i = 0; i < 20; ++i) arr.push(mp::Value::integer(i * 1000));
    v.set("a", arr);
    v.set(mp::Value::integer(7), mp::Value::boolean(false));   // non-string key
    for (bool legacy : {false, true}) {
        mp::Bytes b = mp::encode(v, {legacy});
        mp::Value back;
        std::string err;
        size_t used = 0;
        CHECK(mp::decode(b.data(), b.size(), back, &err, &used));
        CHECK(used == b.size());
        if (!legacy) CHECK(back == v);
        CHECK(back.find("i") && back.find("i")->asInt() == -123456789012ll);
        CHECK(back.find("u") && back.find("u")->asUInt() == 18446744073709551615ull);
        CHECK(back.find("s") && back.find("s")->asStr().size() == 70000);
    }
    // Malformed input fails cleanly.
    mp::Value out;
    std::string err;
    const uint8_t truncated[] = {0xcd, 0x01};
    CHECK(!mp::decode(truncated, sizeof truncated, out, &err) && !err.empty());
    const uint8_t reserved[] = {0xc1};
    CHECK(!mp::decode(reserved, 1, out, &err));
    const uint8_t hugeArray[] = {0xdd, 0xff, 0xff, 0xff, 0xff, 0x00};
    CHECK(!mp::decode(hugeArray, sizeof hugeArray, out, &err));
    mp::Bytes deep(100, 0x91);
    deep.push_back(0xc0);
    CHECK(!mp::decode(deep, out, &err));                 // depth 100 > 64
    // Stream of values.
    mp::Bytes two = mp::encode(mp::Value::integer(1));
    mp::Bytes second = mp::encode(mp::Value::str("x"));
    two.insert(two.end(), second.begin(), second.end());
    size_t used = 0;
    CHECK(mp::decode(two.data(), two.size(), out, nullptr, &used) && used == 1 && out.asInt() == 1);
    CHECK(mp::decode(two.data() + used, two.size() - used, out) && out.asStr() == "x");
}

TEST(msgpack_json_bridge) {
    Json j = Json::parse(R"({"id": 9007199254740993, "neg": -5, "f": 0.5, "s": "т", "a": [true, null], "o": {"k": 1}})");
    mp::Value v = mp::fromJson(j);
    CHECK(v.find("id")->asUInt() == 9007199254740993ull);   // exact beyond double precision
    CHECK(v.find("neg")->asInt() == -5);
    CHECK(v.find("f")->type() == mp::Type::Float64);
    mp::Value decoded;
    CHECK(mp::decode(mp::encode(v), decoded));
    Json back = mp::toJson(decoded);
    CHECK(back["id"].numberLiteral() == "9007199254740993");
    CHECK(back["s"].str() == "т");
    CHECK(back["a"][0].boolean() && back["a"][1].isNull());
    CHECK(back["o"].getInt("k") == 1);
    CHECK(mp::toJson(mp::Value::bin({0xff})).str() == "/w==");
}

TEST(hashes_and_base64) {
    auto d = enc::sha256("abc", 3);
    CHECK(enc::hexUpper(d.data(), 32) == "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD");
    auto h = enc::hmacSha256("Jefe", "what do ya want for nothing?");   // RFC 4231 case 2
    CHECK(enc::hexUpper(h.data(), 32) == "5BDCC146BF60754E6A042426089575C75A003F089D2739839DEC58B964EC3843");
    CHECK(enc::base64("hello?>") == "aGVsbG8/Pg==");
    CHECK(enc::base64("hello?>", true, false) == "aGVsbG8_Pg");
    auto u = enc::unbase64("aGVsbG8_Pg");
    CHECK(u && std::string(u->begin(), u->end()) == "hello?>");
    CHECK(!enc::unbase64("a$b"));
    auto x = enc::unhex("00ffAb");
    CHECK(x && x->size() == 3 && (*x)[1] == 0xff && (*x)[2] == 0xab);
}


// ---------------------------------------------------------------- HTTP

TEST(plain_http_server_and_client) {
    pack::HttpCall seen;
    net::HttpServer srv([&](const pack::HttpCall& r) {
        seen = r;
        pack::HttpReply res;
        res.status = r.url == "/echo?x=1" ? 200 : 404;
        res.body = r.body;
        res.headers.push_back({"E-Tag", "x1"});
        return res;
    });
    if (!srv.listen(0, true)) { std::printf("  (no loopback sockets: skipped)\n"); return; }
    net::HttpClient cli;
    pack::HttpCall req;
    req.method = "POST";
    req.url = "http://127.0.0.1:" + std::to_string(srv.port()) + "/echo?x=1";
    req.headers.push_back({"X-Internal-Token", "secret"});
    req.body = std::string(50000, 'q');
    pack::HttpReply got;
    bool done = false;
    cli.request(req, [&](pack::HttpReply r) { got = r; done = true; });
    for (uint64_t now = 1; now < 5000 && !done; ++now) { srv.update(now); cli.update(now); }
    CHECK(done && got.status == 200 && got.body.size() == 50000 && pack::headerValue(got.headers, "e-tag") == "x1");
    CHECK(seen.method == "POST" && pack::headerValue(seen.headers, "x-internal-token") == "secret");
    done = false;
    req.url = "http://127.0.0.1:" + std::to_string(srv.port()) + "/nothing";
    cli.request(req, [&](pack::HttpReply r) { got = r; done = true; });
    for (uint64_t now = 5000; now < 10000 && !done; ++now) { srv.update(now); cli.update(now); }
    CHECK(done && got.status == 404 && cli.pending() == 0);
    pack::HttpReply parsed;
    CHECK(net::parseHttpResponse("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n2\r\nde\r\n0\r\n\r\n", parsed, false) && parsed.body == "abcde");
    net::UrlParts u;
    CHECK(net::parseUrl("http://[::1]:8080/a/b?c", u) && u.host == "::1" && u.port == 8080 && u.path == "/a/b?c");
    CHECK(net::parseUrl("https://host/x", u) && u.scheme == "https" && u.port == 443 && !net::parseUrl("host/x", u));
}

TEST(platform_http_request_reaches_a_server) {
    // pack::httpRequest is the platform's HTTP stack (libcurl / NSURLSession / HttpURLConnection /
    // WinHTTP / fetch): request, headers and body out; status, headers and body back.
    pack::HttpCall seen;
    net::HttpServer srv([&](const pack::HttpCall& r) {
        seen = r;
        pack::HttpReply res;
        res.status = r.url == "/v/items" ? 200 : 500;
        res.body = R"({"items":[1,2,3]})";
        res.headers.push_back({"E-Tag", "n1"});
        return res;
    });
    if (!srv.listen(0, true)) { std::printf("  (no loopback sockets: skipped)\n"); return; }
    struct Shared { std::mutex m; pack::HttpReply reply; bool done = false; };
    auto run = [&](pack::HttpCall call, pack::HttpReply& out) {
        auto sh = std::make_shared<Shared>();
        if (!pack::httpRequest(call, [sh](pack::HttpReply r) { std::lock_guard<std::mutex> l(sh->m); sh->reply = std::move(r); sh->done = true; })) return false;
        for (uint64_t end = wallMs() + 8000; wallMs() < end;) {
            srv.update(wallMs());
            { std::lock_guard<std::mutex> l(sh->m); if (sh->done) { out = sh->reply; return true; } }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        out = {};
        return true;
    };
    pack::HttpCall call;
    call.method = "POST";
    call.url = "http://127.0.0.1:" + std::to_string(srv.port()) + "/v/items";
    call.headers = {{"token", "tok-h"}, {"Content-Type", "application/json"}};
    call.body = R"({"lang":"en"})";
    call.timeoutMs = 5000;
    pack::HttpReply got;
    if (!run(call, got)) { std::printf("  (no platform HTTP stack in this build: skipped)\n"); return; }
    CHECK(got.status == 200 && got.body == R"({"items":[1,2,3]})" && pack::headerValue(got.headers, "e-tag") == "n1");
    CHECK(seen.method == "POST" && seen.url == "/v/items" && seen.body == call.body);
    CHECK(pack::headerValue(seen.headers, "token") == "tok-h" && pack::headerValue(seen.headers, "content-type").find("application/json") == 0);
    call.url = "http://127.0.0.1:" + std::to_string(srv.port()) + "/broken";
    CHECK(run(call, got) && got.status == 500 && !got.body.empty());   // an error status still carries its body
    // Nothing listening: a transport error, not a crash or a hang.
    uint16_t closed = 0;
    { net::HttpServer tmp([](const pack::HttpCall&) { return pack::HttpReply{}; }); if (tmp.listen(0, true)) closed = tmp.port(); }
    call.url = "http://127.0.0.1:" + std::to_string(closed) + "/x";
    call.timeoutMs = 2000;
    CHECK(run(call, got) && got.status == 0);
}

// ---------------------------------------------------------------- matches from an external matchmaker

struct Player {
    explicit Player(std::string n) : name(std::move(n)) {}
    std::string name;
    std::unique_ptr<Simulation> sim;
    std::unique_ptr<net::NetClient> net;
    std::vector<game::ComputerPlayer> bots;
    bool ended = false;
    int result = 0;
};
static void pumpPlayer(Player& p, uint64_t now, double dt) {
    if (!p.net) return;
    p.net->update(now);
    for (auto& e : p.net->takeEvents())
        if (e.kind == net::NetEvent::Kind::MatchEnded && !p.ended) { p.ended = true; p.result = e.value; }
    if (p.net->matchReady()) {
        auto& m = p.net->match();
        auto data = netgame::loadMatchData(kMushrooms, m.map.file);
        p.sim = netgame::makeSimulation(data.module, p.net->simConfig());
        p.sim->setup(data.rules, data.map);
        p.net->attach(*p.sim);
        p.bots.clear();
        for (uint8_t s : m.controlled) p.bots.emplace_back(s, 77 + s);
    }
    if (p.sim && p.net->session()) {
        if (!p.net->session()->confirmedResult()) for (auto& b : p.bots) b.update(*p.sim);
        p.net->advance(dt);
    }
}

// A backend extension as a game would write it: tickets are "<match>/<user>/<secret>" (a real one signs
// them), started / ended matches are collected instead of being posted to a backend.
struct TestBackend final : net::ServerExtension {
    std::string secret;
    std::vector<std::string> started;
    std::vector<net::ExternalMatchEnd> ended;
    int updates = 0;
    bool startedUp = false;
    void configure(const Json& config, net::ServerConfig& sc) override {
        if (!config.has("secret")) throw std::runtime_error("\"secret\" is required");
        secret = config.getStr("secret");
        sc.ticketCheck = [this](const std::string& ticket, net::TicketInfo& info) {
            const size_t a = ticket.find('/'), b = ticket.rfind('/');
            if (a == std::string::npos || b == a || ticket.substr(b + 1) != secret) return false;
            info.externalId = ticket.substr(0, a);
            const std::string who = ticket.substr(a + 1, b - a - 1);
            if (who.rfind("watch:", 0) == 0) { info.spectator = true; info.name = who.substr(6); }
            else info.userId = std::stoull(who);
            return true;
        };
        sc.onExternalMatchStarted = [this](const std::string& id) { started.push_back(id); };
        sc.onExternalMatchEnd = [this](const net::ExternalMatchEnd& e) { ended.push_back(e); };
    }
    bool start(net::Server&, std::string*) override { startedUp = true; return true; }
    void update(uint64_t) override { ++updates; }
};
AIVERSE_SERVER_EXTENSION("test-backend", TestBackend);

TEST(server_extension_registry) {
    auto names = net::serverExtensions();
    CHECK(names.size() == 1 && names[0] == "test-backend");
    CHECK(!net::makeServerExtension("no-such-backend"));
    auto ext = net::makeServerExtension("test-backend");
    CHECK(ext != nullptr);
    net::ServerConfig sc;
    bool refused = false;
    try { ext->configure(Json(), sc); } catch (const std::exception&) { refused = true; }
    CHECK(refused && !sc.ticketCheck);
    ext->configure(Json::parse(R"({"secret":"s"})"), sc);
    CHECK(sc.ticketCheck && sc.onExternalMatchStarted && sc.onExternalMatchEnd);
}

TEST(external_match_reservation_tickets_spectators_report) {
    const std::string gameId = Json::parseFile(kMushrooms + "/game.json").getStr("id");
    auto maps = net::describeGameMaps(kMushrooms);
    std::string mapFile;
    for (auto& mi : maps) if (mi.players() == 2) { mapFile = mi.file; break; }
    CHECK(!mapFile.empty());
    auto extBase = net::makeServerExtension("test-backend");
    auto* ext = static_cast<TestBackend*>(extBase.get());
    net::ServerConfig sc;
    sc.udpPort = 0; sc.wsPort = 0; sc.log = std::getenv("REMOTE_DEBUG") != nullptr;
    sc.surrenderCommand = game::kCmdSurrender;
    sc.gameDirs[gameId] = kMushrooms;
    sc.arbiter = netgame::makeArbiter(sc.gameDirs);
    ext->configure(Json::parse(R"({"secret":"s3cret"})"), sc);
    net::Server server(sc);
    std::string err;
    CHECK(ext->start(server, &err) && ext->startedUp);

    net::Reservation r;
    r.externalId = "m-900001";
    r.gameId = gameId;
    r.mapFile = mapFile;
    r.joinTimeoutSec = 20;
    r.seats.resize(2);
    r.seats[0].userId = 4242; r.seats[0].name = "Commander"; r.seats[0].team = 1;
    r.seats[1].userId = 5151; r.seats[1].name = "Rival"; r.seats[1].team = 2;
    CHECK(server.reserve(r, &err));
    CHECK(!server.reserve(r, &err) && !err.empty() && server.reservations() == 1);   // the id is taken
    net::Reservation bad = r;
    bad.externalId = "m-2"; bad.mapFile.clear();
    CHECK(!server.reserve(bad, &err));

    Player a{"Commander"}, b{"Rival"}, intruder{"Intruder"}, early{"Watcher"}, late{"LateWatcher"};
    auto connect = [&](Player& p, uint64_t key) {
        auto [c, s] = net::makeLoopbackPair({20, 5, key});
        server.addConnection(std::move(s));
        p.net = std::make_unique<net::NetClient>(std::move(c), net::ClientIdentity{p.name, gameId, key});
    };
    connect(a, 1); connect(b, 2); connect(intruder, 3); connect(early, 4); connect(late, 5);
    uint64_t now = 1000;
    auto pumpAll = [&] {
        server.update(now); ext->update(now);
        for (Player* p : {&a, &b, &intruder, &early, &late}) pumpPlayer(*p, now, .005);
    };
    for (int i = 0; i < 40; ++i, now += 5) pumpAll();
    intruder.net->joinQueue("ticket:m-900001/4242/wrong", maps);      // bad signature
    a.net->joinQueue("ticket:m-900001/4242/s3cret", maps);
    early.net->joinQueue("ticket:m-900001/watch:Watcher/s3cret", maps);   // spectator before the start
    for (int i = 0; i < 100; ++i, now += 5) pumpAll();
    CHECK(!a.net->inMatch() && server.reservations() == 1 && ext->started.empty());   // waits for the second seat
    b.net->joinQueue("ticket:m-900001/5151/s3cret", maps);
    bool lateJoined = false, earlyWatched = false, lateWatched = false;
    for (uint64_t t0 = now; now - t0 < 600000 && !(a.ended && b.ended); now += 5) {
        pumpAll();
        if (!lateJoined && now - t0 > 20000) { late.net->joinQueue("ticket:m-900001/watch:LateWatcher/s3cret", maps); lateJoined = true; }
        earlyWatched |= early.net->inMatch() && early.net->match().localSlot == 0;
        lateWatched |= late.net->inMatch() && late.net->match().localSlot == 0;
    }
    for (int i = 0; i < 200 && ext->ended.empty(); ++i, now += 5) pumpAll();
    CHECK(earlyWatched && lateWatched);                 // spectator tickets: before the start and late
    CHECK(!intruder.net->inMatch());
    CHECK(a.ended && b.ended && a.result == b.result && a.result != 0);
    CHECK(server.reservations() == 0 && ext->updates > 0);
    CHECK(ext->started.size() == 1 && ext->started[0] == "m-900001");
    CHECK(ext->ended.size() == 1);
    if (!ext->ended.empty()) {
        const net::ExternalMatchEnd& e = ext->ended[0];
        CHECK(e.externalId == "m-900001" && e.started && e.result == a.result && e.endTick > 0 && e.endHash != 0);
        CHECK(e.seats.size() == 2 && e.seats[0].userId == 4242 && e.seats[1].userId == 5151 && e.seats[0].joined && e.seats[1].joined);
        // The report carries what a re-simulation needs (statistics on the host): same end state.
        CHECK(e.gameId == gameId && e.mapFile == mapFile && e.seed != 0 && !e.commands.empty());
        auto data = netgame::loadMatchData(kMushrooms, e.mapFile);
        SimConfig cfg;
        cfg.seed = e.seed; cfg.tickRate = e.tickRate; cfg.inputDelayTicks = e.inputDelay;
        auto sim = netgame::makeSimulation(data.module, cfg);
        sim->setup(data.rules, data.map);
        for (auto& c : e.commands) sim->inject(c);
        while (sim->tick() < e.endTick && !sim->result()) sim->step();
        std::printf("  re-simulated %zu commands to tick %u: hash %s\n", e.commands.size(), e.endTick, sim->checksum() == e.endHash ? "matches" : "DIFFERS");
        CHECK(sim->checksum() == e.endHash);
    }

    // Seats nobody takes: the reservation is cancelled at the timeout and reported as not started.
    net::Reservation r2 = r;
    r2.externalId = "m-900002"; r2.joinTimeoutSec = 2;
    CHECK(server.reserve(r2, &err));
    for (int i = 0; i < 800; ++i, now += 5) { server.update(now); ext->update(now); }
    CHECK(server.reservations() == 0 && ext->ended.size() == 2);
    if (ext->ended.size() == 2) CHECK(ext->ended[1].externalId == "m-900002" && !ext->ended[1].started && !ext->ended[1].reason.empty());
}

int main(int argc, char** argv) {
    for (auto& [name, fn] : registry()) {
        if (argc > 1 && std::string(name).find(argv[1]) == std::string::npos) continue;   // ./aiverse_remote_tests <name filter>
        const int before = g_failed;
        fn();
        std::printf("%s %s\n", g_failed == before ? "[ OK ]" : "[FAIL]", name);
    }
    std::printf("\n%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
