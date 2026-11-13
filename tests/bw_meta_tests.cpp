// Bunker Wars backend adapter (agent-bw-meta): MessagePack, wire formats, tickets, the meta API
// client against recorded-shape mock replies (tests/data/bw_meta), meta::Backend mapping, the
// matchmaking handoff session and the engine server as a BW battle node.
// Run: ./aiverse_bw_meta_tests (from the repo root or with AIVERSE_SOURCE_DIR).
#include <cstdio>
#include <deque>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "apps/server/BwStats.h"
#include "apps/server/GameData.h"
#include "engine/meta/Client.h"
#include "engine/meta/Encoding.h"
#include "engine/meta/MsgPack.h"
#include "engine/meta/bw/BwApi.h"
#include "engine/meta/bw/BwBackend.h"
#include "engine/meta/bw/BwHttp.h"
#include "engine/meta/bw/BwMatch.h"
#include "engine/meta/bw/BwNode.h"
#include "engine/meta/bw/BwPlatformHttp.h"
#include "engine/meta/bw/BwTcp.h"
#include "engine/meta/bw/BwTutorials.h"
#include <chrono>
#include <thread>
#include "engine/meta/bw/BwWire.h"
#include "engine/net/NetClient.h"
#include "engine/net/server/Server.h"
#include "engine/sim/Simulation.h"
#include "modules/rts/RtsAI.h"
#include "modules/rts/RtsModule.h"

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
static const std::string kData = std::string(AIVERSE_SOURCE_DIR) + "/tests/data/bw_meta/";
static const std::string kMushrooms = std::string(AIVERSE_SOURCE_DIR) + "/templates/mushrooms";
static std::string readFile(const std::string& p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }
static std::string hex(const mp::Bytes& b) { return enc::hexUpper(b.data(), b.size()); }

// ---------------------------------------------------------------- MessagePack

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

// ---------------------------------------------------------------- encodings, tickets

TEST(bw_hashes_and_base64) {
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

TEST(bw_battle_tickets) {
    const std::string secret = "0123456789abcdef0123456789abcdef-test";
    bw::BattleTicket t;
    t.gameId = 900001; t.userId = 4242; t.expiresAt = 2000000000;
    const std::string s = bw::issueTicket(t, secret);
    CHECK(s.rfind("v1.", 0) == 0 && s.find('=') == std::string::npos);
    auto body = enc::unbase64(s.substr(3, s.rfind('.') - 3));
    CHECK(body && std::string(body->begin(), body->end()) == R"({"G":900001,"U":4242,"Exp":2000000000})");
    auto ok = bw::validateTicket(s, secret, 1900000000);
    CHECK(ok && ok->gameId == 900001 && ok->userId == 4242 && ok->role == 0);
    CHECK(!bw::validateTicket(s, secret, 2000000001));                 // expired
    CHECK(!bw::validateTicket(s, secret + "x", 1900000000));           // other secret
    std::string tampered = s;
    tampered[5] = tampered[5] == 'A' ? 'B' : 'A';
    CHECK(!bw::validateTicket(tampered, secret, 1900000000));
    CHECK(!bw::validateTicket(s, "short", 1900000000));                // secrets < 32 bytes are refused
    t.role = 1; t.name = "Watcher";
    auto spec = bw::validateTicket(bw::issueTicket(t, secret), secret, 1900000000);
    CHECK(spec && spec->role == 1 && spec->name == "Watcher");
}

// ---------------------------------------------------------------- frames, chat, battle packets

TEST(bw_frames_and_packets) {
    bw::Bytes f = bw::encodeFrame({"Match", 0, "{}"});
    CHECK(hex(f) == "00054D6174636800027B7D");
    bw::Bytes g = bw::encodeFrame({"NotifyGame", 3, "AB"});
    bw::FrameReader r;
    bw::Bytes all = f;
    all.insert(all.end(), g.begin(), g.end());
    bw::Bytes dropped = bw::encodeFrame({"Bad,", 0, "x"});   // Java dropped "func," frames
    all.insert(all.end(), dropped.begin(), dropped.end());
    all.insert(all.end(), f.begin(), f.end());
    std::vector<bw::Frame> got;
    for (size_t i = 0; i < all.size(); ++i) {               // byte by byte: partial frames wait
        r.feed(&all[i], 1);
        bw::Frame fr;
        while (r.next(fr)) got.push_back(fr);
    }
    CHECK(got.size() == 3);
    if (got.size() == 3) {
        CHECK(got[0].func == "Match" && got[0].msg == "{}" && got[0].idx == 0);
        CHECK(got[1].func == "NotifyGame" && got[1].idx == 3 && got[1].msg == "AB");
        CHECK(got[2].func == "Match");
    }
    // Server RPC packet, expected bytes from BunkerWars-Server-CS docs/architecture.md (msgpack-java 0.6).
    bw::Bytes pkg = bw::packageForPlayer(bw::RpcMethod::OnPlayerConnected_RPC, 1, 300);
    CHECK(hex(pkg) == "9593CD012C01000102B54F6E506C61796572436F6E6E65637465645F525043C0");
    bw::RpcHeader h;
    std::string method;
    CHECK(bw::readPackageHeader(pkg, h, &method) && h.tick == 300 && h.playerIndex == 1 && method == "OnPlayerConnected_RPC");
    CHECK(hex(bw::packageForPlayer(bw::RpcMethod::OnPlayerDefeatTimeout_RPC, 0, -5)).substr(0, 10) == "9593000000");
    CHECK(!bw::readPackageHeader({0x91, 0x01}, h));
    // Chat: client auth packet and chunked server messages.
    CHECK(hex(bw::encodeChatAuth("t")) == "0001000D7B22746F6B656E223A2274227D");
    const std::string big = "{\"msg\":\"" + std::string(20000, 'm') + "\"}";
    bw::Bytes srv = bw::encodeChatServerPacket(uint16_t(bw::ChatType::OnMsg), 5, big, 8192);
    bw::Bytes small = bw::encodeChatServerPacket(uint16_t(bw::ChatType::OnNotifyFriendRequest), 6, "{\"id\":1}");
    srv.insert(srv.end(), small.begin(), small.end());
    bw::ChatReader cr;
    std::vector<bw::ChatPacket> packets;
    for (size_t i = 0; i < srv.size(); i += 1000) {
        cr.feed(srv.data() + i, std::min<size_t>(1000, srv.size() - i));
        bw::ChatPacket p;
        while (cr.next(p)) packets.push_back(p);
    }
    CHECK(packets.size() == 2 && packets[0].json == big && packets[0].type == 0);
    CHECK(packets.size() == 2 && packets[1].type == 0x11 && packets[1].json == "{\"id\":1}");
}

// ---------------------------------------------------------------- HTTP API client (mock server)

struct MockServer {
    struct Rule { std::string urlSuffix; int status; std::string body; std::vector<std::pair<std::string, std::string>> headers; bool timeout = false; };
    std::deque<Rule> rules;      // matched in order of registration; each used once
    std::vector<bw::HttpRequest> seen;
    void add(std::string suffix, int status, std::string body, std::vector<std::pair<std::string, std::string>> headers = {}) {
        rules.push_back({std::move(suffix), status, std::move(body), std::move(headers)});
    }
    bw::HttpTransport transport() {
        return [this](const bw::HttpRequest& req, std::function<void(bw::HttpResponse)> done) {
            seen.push_back(req);
            for (auto it = rules.begin(); it != rules.end(); ++it) {
                if (req.url.size() < it->urlSuffix.size() || req.url.compare(req.url.size() - it->urlSuffix.size(), it->urlSuffix.size(), it->urlSuffix))
                    continue;
                bw::HttpResponse r;
                r.status = it->status; r.body = it->body; r.headers = it->headers; r.timeout = it->timeout;
                if (it->timeout) { r.status = 0; r.error = "timeout"; }
                rules.erase(it);
                done(r);
                return;
            }
            bw::HttpResponse r;
            r.status = 404;
            done(r);
        };
    }
};
static void drain(bw::ApiClient& api) { for (int i = 0; i < 20; ++i) api.pump(); }
static std::string wrapData(const std::string& dataJson) { return "{\"i\":-1,\"data\":" + dataJson + "}"; }

TEST(bw_api_entry_auth_and_hosts) {
    MockServer m;
    m.add("http://boot.mock/v/entryPoint", 200, readFile(kData + "entry_point.json"));
    m.add("http://auth.mock/v/profile/auth", 200, readFile(kData + "auth.json"));
    bw::ApiClient api(m.transport(), {"http://boot.mock"});
    bw::ClientInfo info;
    bool entered = false, authed = false;
    api.entryPoint(info, [&](bw::ApiResult r) { entered = r.ok(); });
    drain(api);
    CHECK(entered && api.chunks());
    CHECK(api.hosts().size() == 1 && api.hosts()[0] == "http://auth.mock");
    CHECK(api.endpoints().stat.size() == 1 && api.endpoints().stat[0] == "stat.mock:9000");
    api.auth(bw::ApiClient::guestAuth(info, "guid-1", "duid-1"), [&](bw::ApiResult r) { authed = r.ok(); });
    drain(api);
    CHECK(authed && api.token() == "tok-1" && api.userId() == 4242);
    CHECK(api.hosts().size() == 2 && api.hosts()[0] == "http://data1.mock");   // data hosts from the entry point
    CHECK(api.endpoints().game.size() == 1 && api.endpoints().game[0] == "mm.mock:7777");
    CHECK(api.endpoints().chat.size() == 1 && api.endpoints().chat[0] == "chat.mock:7778");
    // The auth request is a RequestData envelope with the AuthTO.
    Json sent = Json::parse(m.seen.back().body);
    CHECK(sent.getInt("i") == -1 && sent["data"].getInt("authType") == 0 && sent["data"].getStr("guid") == "guid-1");
    // Every later call carries the token header and fails over to the second data host.
    m.add("http://data1.mock/v/news/get", 0, "");
    m.add("http://data2.mock/v/news/get", 200, wrapData("{\"news\":[]}"));
    bool news = false;
    api.call("v/news/get", Json::object(), [&](bw::ApiResult r) { news = r.ok() && r.data["news"].isArray(); });
    drain(api);
    CHECK(news);
    CHECK(m.seen.back().url == "http://data2.mock/v/news/get");
    bool hasToken = false;
    for (auto& [k, v] : m.seen.back().headers) hasToken |= k == "token" && v == "tok-1";
    CHECK(hasToken);
}

TEST(bw_api_errors_cache_chunks_reauth) {
    MockServer m;
    bw::ApiClient api(m.transport(), {"http://data.mock"});
    api.setToken("old");
    // Handled error (HTTP 500 with an error body) is definitive, not a transport failure.
    m.add("/v/hero/buyHero", 500, readFile(kData + "error_not_enough_money.json"));
    bw::ApiResult res;
    api.call("v/hero/buyHero", Json::parse(R"({"heroClientId":9})"), [&](bw::ApiResult r) { res = r; });
    drain(api);
    CHECK(res.error.code == 1 && res.error.key == "shop.not_enough_money" && !res.transport);
    // E-Tag: the second call sends the tag and a 304 returns the cached data.
    m.add("/v/news/get", 200, wrapData("{\"items\":[1,2]}"), {{"E-Tag", "abc"}});
    m.add("/v/news/get", 304, "");
    bw::CallOptions cached;
    cached.cacheable = true;
    api.call("v/news/get", Json::object(), [&](bw::ApiResult r) { res = r; }, cached);
    drain(api);
    api.call("v/news/get", Json::object(), [&](bw::ApiResult r) { res = r; }, cached);
    drain(api);
    bool sentTag = false;
    for (auto& [k, v] : m.seen.back().headers) sentTag |= k == "E-Tag" && v == "abc";
    CHECK(sentTag && res.ok() && res.notModified && res.data["items"].size() == 2);
    // Chunked reply: {path}/chunk then GET v/chunk/{id}/{n}; the parts are one JSON text.
    api.setChunks(true);
    const std::string profile = readFile(kData + "profile_get.json");
    const size_t third = profile.size() / 3;
    auto part = [&](size_t i) {
        Json d = Json::object();
        d.set("length", Json::number(int64_t(profile.size())));
        d.set("chunks", Json::number(int64_t(3)));
        d.set("data", Json::string(profile.substr(i * third, i == 2 ? std::string::npos : third)));
        Json w = Json::object();
        w.set("data", d);
        return w.dump(-1);
    };
    m.add("/v/profile/get/chunk", 200, part(0));
    m.add("/v/chunk/777/1", 200, part(1));
    m.add("/v/chunk/777/2", 200, part(2));
    bw::CallOptions chunk;
    chunk.chunked = true;
    chunk.requestId = 777;
    api.call("v/profile/get", Json::parse(R"({"updateMask":0})"), [&](bw::ApiResult r) { res = r; }, chunk);
    drain(api);
    CHECK(res.ok() && res.data.getInt("userId") == 4242 && res.data["heroes"].size() == 2);
    CHECK(Json::parse(m.seen[m.seen.size() - 3].body).getInt("id") == 777);
    // Static data: continuation is v/chunk/db/<name>/<n>.
    m.add("/v/db/heroes/chunk", 200, R"({"data":{"length":10,"chunks":2,"data":"[1,"}})");
    m.add("/v/chunk/db/heroes/1", 200, R"({"data":{"length":10,"chunks":2,"data":"2]"}})");
    bw::CallOptions st;
    st.chunked = true; st.staticData = true;
    api.call("v/db/heroes", Json(), [&](bw::ApiResult r) { res = r; }, st);
    drain(api);
    CHECK(res.ok() && res.data.size() == 2 && res.data[1].integer() == 2);
    // NOT_AUTHORIZED: silent re-auth with the stored AuthTO, then one repeat.
    api.setChunks(false);
    m.add("/v/profile/auth", 200, readFile(kData + "auth.json"));
    api.auth(bw::ApiClient::guestAuth({}, "g", "d"), nullptr);
    drain(api);
    api.setToken("expired");
    api.setHosts({"http://data.mock"});
    m.add("/v/friend/getAll", 200, readFile(kData + "error_not_authorized.json"));
    m.add("/v/profile/auth", 200, readFile(kData + "auth.json"));
    m.add("/v/friend/getAll", 200, wrapData("{\"friends\":[{\"id\":1}]}"));
    int calls = 0;
    api.call("v/friend/getAll", Json::object(), [&](bw::ApiResult r) { res = r; ++calls; });
    drain(api);
    CHECK(calls == 1 && res.ok() && res.data["friends"].size() == 1 && api.token() == "tok-1");
    // Transport timeout on the only host: transport error with TIMEOUT code.
    api.setHosts({"http://data.mock"});
    m.rules.push_back({"/v/clans/list", 0, "", {}, true});
    api.call("v/clans/list", Json::object(), [&](bw::ApiResult r) { res = r; });
    drain(api);
    CHECK(res.transport && res.error.code == int(bw::ApiCode::Timeout));
}

// ---------------------------------------------------------------- meta::Backend over the API

TEST(bw_backend_view_and_routes) {
    MockServer m;
    bw::ApiClient api(m.transport(), {"http://data.mock"});
    api.setToken("tok");
    bw::BwBackend backend(api);
    meta::Client client(backend);
    // meta/view -> v/profile/get -> engine view projection
    m.add("/v/profile/get", 200, wrapData(readFile(kData + "profile_get.json")));
    CHECK(client.begin(meta::requests::view()));
    client.pump();
    auto reply = client.reply();
    CHECK(reply.result.code == Code::Ok);
    const Json& v = reply.data;
    CHECK(v.getStr("source") == "bunkerwars" && v.getStr("player") == "4242" && v.getInt("revision") == 1);
    CHECK(v["profile"]["balances"].getInt("gold") == 1250 && v["profile"]["balances"].getInt("gems") == 40);
    CHECK(v["profile"]["balances"].getInt("currency.11") == 5 && v["profile"]["balances"].getInt("currency.12.3") == 2);
    CHECK(v["profile"]["attributes"].getStr("name") == "Commander" && v["profile"]["attributes"].getInt("leagueIndex") == 3);
    CHECK(v["heroes"].size() == 2 && v["clan"].getInt("clanId") == 77 && v["offers"].size() == 1 && v["inventory"].isObject());
    CHECK(Json::parse(m.seen.back().body)["data"].getInt("updateMask", -1) == 0);
    // A purchase that fails for money maps to Insufficient with the BW error kept for localization.
    m.add("/v/hero/buyHero", 500, readFile(kData + "error_not_enough_money.json"));
    auto args = Json::parse(R"({"heroClientId":9})");
    CHECK(client.begin({"heroes", "buy", "op-buy-1", args}));
    client.pump();
    reply = client.reply();
    CHECK(reply.result.code == Code::Insufficient && !client.pending());
    CHECK(reply.data["error"].getStr("loc") == "Errors|shop|not_enough_money");
    CHECK(Json::parse(m.seen.back().body)["data"].getInt("heroClientId") == 9);
    // Success: data object + ResponseData.offers.
    m.add("/v/hero/buyHero", 200, readFile(kData + "buy_hero.json"));
    CHECK(client.begin({"heroes", "buy", "op-buy-2", args}));
    client.pump();
    reply = client.reply();
    CHECK(reply.result.code == Code::Ok && reply.data.getInt("gold") == 950 && reply.data["offers"].isObject());
    // Unknown route: Unsupported, and no HTTP request is made.
    const size_t before = m.seen.size();
    CHECK(client.begin({"economy", "buy", "op-x", Json::object()}));
    client.pump();
    CHECK(client.reply().result.code == Code::Unsupported && m.seen.size() == before);
    // statics.<name> -> v/db/<name>; path injection is refused.
    CHECK(bw::findRoute("statics", "heroes") && std::string(bw::findRoute("statics", "heroes")->path) == "v/db/heroes");
    CHECK(!bw::findRoute("statics", "../secret"));
    CHECK(bw::findRoute("battlepass", "claim") && std::string(bw::findRoute("battlepass", "claim")->path) == "v/hero/battlePass/rewards/claim");
    // Every catalog entry is unique.
    std::map<std::string, int> seen;
    for (auto& r : bw::routes()) ++seen[std::string(r.service) + "." + r.action];
    bool unique = true;
    for (auto& [k, n] : seen) unique &= n == 1;
    CHECK(unique && bw::routes().size() > 150);
}

TEST(bw_backend_retry_rules) {
    MockServer m;
    bw::ApiClient api(m.transport(), {"http://data.mock"});
    api.setToken("tok");
    bw::BwBackend backend(api);
    meta::Client client(backend);
    // A plain mutation lost in transport: terminal (the BW API has no operation ids), UI refreshes.
    m.rules.push_back({"/v/inventory/buy-cell", 0, "", {}, true});
    CHECK(client.begin({"inventory", "buyCell", "op-cell", Json::object()}));
    client.pump();
    CHECK(!client.pending() && client.reply().result.code == Code::Unavailable && client.reply().data.getBool("refresh"));
    // A chunk-capable mutation keeps its request id: the retry hits the server's chunk cache.
    api.setChunks(true);
    m.rules.push_back({"/v/clans/takeGift/chunk", 0, "", {}, true});
    m.add("/v/clans/takeGift/chunk", 200, R"({"data":{"length":2,"chunks":1,"data":"{}"}})");
    CHECK(client.begin({"clan", "takeGift", "op-gift", Json::object()}));
    client.pump();
    CHECK(client.pending() && !client.inFlight() && client.reply().result.code == Code::Unavailable);
    const int64_t firstId = bw::jsonInt(Json::parse(m.seen.back().body)["id"]);
    CHECK(client.retry());
    client.pump();
    CHECK(!client.pending() && client.reply().result.code == Code::Ok);
    CHECK(bw::jsonInt(Json::parse(m.seen.back().body)["id"]) == firstId && firstId > 0);
}

// ---------------------------------------------------------------- matchmaking handoff

TEST(bw_match_session_handoff) {
    bw::MatchSession s;
    s.connect("tok-1");
    bw::MatchParams p;
    p.mapId = 101; p.matcher = 0; p.mode = 1; p.winMode = 1;
    s.match(p);
    bw::FrameReader out;
    out.feed(s.takeOutgoing());
    bw::Frame f;
    CHECK(out.next(f) && f.func == "Connect" && f.msg == "tok-1");
    CHECK(out.next(f) && f.func == "Match" && Json::parse(f.msg).getInt("mapId") == 101);
    // Matchmaker replies, split across reads.
    bw::Bytes in = bw::encodeFrame({"Connected", 0, ""});
    auto add = [&](const bw::Frame& fr) { auto b = bw::encodeFrame(fr); in.insert(in.end(), b.begin(), b.end()); };
    add({"Match.onUpdate", 0, R"({"roomId":900001,"mapId":101,"players":2,"connectedPlayers":[]})"});
    add({"Match.onReadyToCountdown", 0, ""});
    add({"Match.onReady", 0, R"({"roomId":900001,"mapId":101,"matcher":0,"mode":1,"winMode":1,"players":2,)"
                              R"("battleHost":"node1.mock","battlePort":27015,"battlePortSsl":27016,"battleTicket":"v1.abc.def"})"});
    s.feed(in.data(), 7);
    s.feed(in.data() + 7, in.size() - 7);
    auto ev = s.takeEvents();
    CHECK(ev.size() == 4 && ev[0].kind == bw::MatchSession::Event::Kind::Connected);
    CHECK(ev.size() == 4 && ev[3].kind == bw::MatchSession::Event::Kind::Ready);
    CHECK(s.phase() == bw::MatchSession::Phase::Ready && s.handoff() && s.handoff()->hasNode());
    if (s.handoff()) {
        CHECK(s.handoff()->address() == "node1.mock:27015" && s.handoff()->queueId() == "ticket:v1.abc.def");
        CHECK(s.handoff()->roomId == 900001 && s.handoff()->mapId == 101);
    }
    // The Java server (no battle* fields) cannot hand off to an engine node.
    bw::MatchSession legacy;
    legacy.feed(bw::encodeFrame({"Match.onReady", 0, R"({"roomId":1,"mapId":2})"}));
    CHECK(legacy.handoff() && !legacy.handoff()->hasNode());
}

// ---------------------------------------------------------------- engine server as a BW battle node

struct Player {
    explicit Player(std::string n) : name(std::move(n)) {}
    std::string name;
    std::unique_ptr<Simulation> sim;
    std::unique_ptr<net::NetClient> net;
    std::vector<game::RtsAI> bots;
    bool ended = false;
    int result = 0;
};
static void pumpPlayer(Player& p, uint64_t now, double dt) {
    if (!p.net) return;
    p.net->update(now);
    auto events = p.net->takeEvents();
    if (std::getenv("BW_DEBUG"))
        for (auto& e : events) std::printf("    [%s] event %d %s\n", p.name.c_str(), int(e.kind), e.text.key.c_str());
    for (auto& e : events)
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

TEST(bw_node_hosts_a_backend_match) {
    const std::string gameId = Json::parseFile(kMushrooms + "/game.json").getStr("id");
    auto maps = net::describeGameMaps(kMushrooms);
    std::string mapFile;
    for (auto& mi : maps) if (mi.players() == 2) { mapFile = mi.file; break; }
    CHECK(!mapFile.empty());
    bw::NodeConfig nc;
    nc.ticketSecret = "0123456789abcdef0123456789abcdef-node";
    nc.internalToken = "internal-secret";
    nc.metaBaseUrl = "http://meta.mock/internal/battle/";
    nc.matchmakerUrl = "http://mm.mock/internal/";
    nc.gameId = gameId;
    nc.maps[101] = mapFile;
    nc.joinTimeoutSec = 20;
    bw::BattleNode node(nc);
    node.clockSec = [] { return int64_t(1900000000); };
    std::vector<bw::HttpRequest> posted;
    node.transportOverride = [&](const bw::HttpRequest& r, std::function<void(bw::HttpResponse)> done) {
        posted.push_back(r);
        bw::HttpResponse ok;
        ok.status = 200;
        done(ok);
    };
    net::ServerConfig sc;
    sc.udpPort = 0; sc.wsPort = 0; sc.log = std::getenv("BW_DEBUG") != nullptr;
    sc.surrenderCommand = game::kCmdSurrender;
    sc.gameDirs[gameId] = kMushrooms;
    sc.arbiter = netgame::makeArbiter(sc.gameDirs);
    node.battleStats = netgame::bwBattleStats(sc.gameDirs);   // BW PlayerStatistic by re-simulation
    node.install(sc);
    net::Server server(sc);
    node.attach(server);

    // Matchmaker -> node: the internal API needs the shared token.
    bw::HttpRequest req;
    req.method = "POST"; req.url = "/internal/games"; req.body = readFile(kData + "game_spec.json");
    CHECK(node.handle(req).status == 401);
    req.headers.push_back({"X-Internal-Token", "internal-secret"});
    bw::HttpResponse created = node.handle(req);
    CHECK(created.status == 201 && Json::parse(created.body).getInt("roomId") == 900001);
    CHECK(node.handle(req).status == 409 && server.reservations() == 1);
    bw::HttpRequest status;
    status.method = "GET"; status.url = "/internal/status"; status.headers = req.headers;
    CHECK(Json::parse(node.handle(status).body).getInt("games") == 1);

    // Players arrive with their tickets (as Match.onReady gave them) through the engine protocol.
    auto ticket = [&](int64_t user) { bw::BattleTicket t; t.gameId = 900001; t.userId = user; t.expiresAt = 1900000300; return bw::issueTicket(t, nc.ticketSecret); };
    Player a{"Commander"}, b{"Rival"}, intruder{"Intruder"}, early{"Watcher"}, late{"LateWatcher"};
    auto connect = [&](Player& p, uint64_t key) {
        auto [c, s] = net::makeLoopbackPair({20, 5, key});
        server.addConnection(std::move(s));
        p.net = std::make_unique<net::NetClient>(std::move(c), net::ClientIdentity{p.name, gameId, key});
    };
    connect(a, 1); connect(b, 2); connect(intruder, 3); connect(early, 4); connect(late, 5);
    auto spectatorTicket = [&](const char* name) {
        bw::BattleTicket t; t.gameId = 900001; t.userId = 0; t.expiresAt = 1900000300; t.role = 1; t.name = name;
        return bw::issueTicket(t, nc.ticketSecret);
    };
    uint64_t now = 1000;
    for (int i = 0; i < 40; ++i, now += 5) {
        server.update(now); node.update(now);
        for (Player* p : {&a, &b, &intruder, &early, &late}) pumpPlayer(*p, now, .005);
    }
    bw::BattleTicket forged; forged.gameId = 900001; forged.userId = 4242; forged.expiresAt = 1900000300;
    intruder.net->joinQueue("ticket:" + bw::issueTicket(forged, "another-secret-another-secret-xx"), maps);
    a.net->joinQueue("ticket:" + ticket(4242), maps);
    early.net->joinQueue("ticket:" + spectatorTicket("Watcher"), maps);   // spectator before the start
    for (int i = 0; i < 100; ++i, now += 5) {
        server.update(now); node.update(now); pumpPlayer(a, now, .005); pumpPlayer(b, now, .005); pumpPlayer(intruder, now, .005); pumpPlayer(early, now, .005);
    }
    CHECK(!a.net->inMatch() && server.reservations() == 1);   // waits for the second seat
    b.net->joinQueue("ticket:" + ticket(5151), maps);
    bool lateJoined = false, earlyWatched = false, lateWatched = false;
    for (uint64_t t0 = now; now - t0 < 600000 && !(a.ended && b.ended); now += 5) {
        server.update(now); node.update(now); pumpPlayer(a, now, .005); pumpPlayer(b, now, .005); pumpPlayer(early, now, .005); pumpPlayer(late, now, .005);
        if (!lateJoined && now - t0 > 20000) { late.net->joinQueue("ticket:" + spectatorTicket("LateWatcher"), maps); lateJoined = true; }
        earlyWatched |= early.net->inMatch() && early.net->match().localSlot == 0;
        lateWatched |= late.net->inMatch() && late.net->match().localSlot == 0;
    }
    CHECK(earlyWatched);
    CHECK(lateWatched);   // spectator tickets: before the start and late (history + delay)
    auto hasResult = [&] { for (auto& p : posted) if (p.url.find("/result") != std::string::npos) return true; return false; };
    for (int i = 0; i < 20000 && !hasResult(); ++i, now += 5) {   // the statistics re-simulation runs on a worker thread
        server.update(now); node.update(now);
        if (i > 10) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(intruder.net && !intruder.net->inMatch());
    CHECK(a.ended && b.ended && a.result == b.result && a.result != 0);
    CHECK(server.reservations() == 0);
    // node -> meta: started + result (GameReport); node -> matchmaker: heartbeat + events.
    const bw::HttpRequest* result = nullptr;
    bool started = false, heartbeat = false, closed = false;
    for (auto& p : posted) {
        if (p.url == "http://meta.mock/internal/battle/games/900001/result") result = &p;
        started |= p.url == "http://meta.mock/internal/battle/games/900001/started";
        heartbeat |= p.url == "http://mm.mock/internal/nodes/heartbeat";
        closed |= p.url == "http://mm.mock/internal/games/900001/events" && Json::parse(p.body).getStr("type") == "closed";
        bool auth = false;
        for (auto& [k, v] : p.headers) auth |= k == "X-Internal-Token" && v == "internal-secret";
        CHECK(auth);
    }
    CHECK(started && heartbeat && closed && result);
    if (result) {
        Json rep = Json::parse(result->body);
        CHECK(rep.getInt("gameId") == 900001 && rep.getStr("closedFrom") == "RESULT");
        CHECK(rep["spec"]["matchContext"]["mapResult"].getInt("leagueRating") == 1520);   // opaque context comes back
        CHECK(rep["players"].size() == 2);
        int winners = 0;
        for (auto& p : rep["players"].items()) winners += p.getBool("win");
        if (a.result > 0) CHECK(winners == 1);
        CHECK(rep["players"][0].getInt("userId") == 4242 && rep["players"][1].getInt("userId") == 5151);
        CHECK(rep["battleResult"]["matchStatistic"].size() == 2);
        // BW PlayerStatistic values: 71 per player, MultiplayerWins (45) for the winner, points (40) and units on the field (39).
        const Json& ms = rep["battleResult"]["matchStatistic"];
        CHECK(ms[0]["values"].size() == 71 && ms[1]["values"].size() == 71);
        CHECK(rep.getStr("strErrors").find("stats") == std::string::npos);
        int wins = 0;
        for (size_t i = 0; i < 2; ++i) {
            const Json& v = ms[i]["values"];
            wins += int(v[45].integer());
            CHECK(v[39].integer() > 0);
            if (ms[i].getBool("winner")) CHECK(v[45].integer() == 1 && v[40].integer() > 0 && v[70].integer() > 0);
        }
        if (a.result > 0) CHECK(wins == 1);
        std::printf("    stats p1:");
        for (size_t k = 0; k < 71; ++k) if (ms[0]["values"][k].integer()) std::printf(" %zu=%lld", k, (long long)ms[0]["values"][k].integer());
        std::printf("\n");
    }
}

TEST(bw_node_map_table_for_bunkerwars) {
    // tools/meta/bw_map_ids.py output: BW MapHeader ids -> templates/bunkerwars maps.
    Json cfg = Json::object();
    cfg.set("maps", Json::string(std::string(AIVERSE_SOURCE_DIR) + "/templates/bunkerwars/bw/map_ids.json"));
    cfg.set("gameId", Json::string("bw"));
    bw::NodeConfig nc = bw::NodeConfig::parse(cfg);
    CHECK(nc.maps.size() > 100 && nc.maps[2001] == "maps/2players_2001.json");
    for (auto& [id, file] : nc.maps) {
        std::ifstream f(std::string(AIVERSE_SOURCE_DIR) + "/templates/bunkerwars/" + file);
        if (!f) { CHECK(!"map file of the table exists"); std::printf("    missing %s\n", file.c_str()); break; }
    }
    Json spec = Json::parse(readFile(kData + "game_spec.json"));
    spec.set("mapId", Json::number(int64_t(2001)));
    auto r = bw::reservationFromSpec(spec, nc);
    CHECK(r && r->mapFile == "maps/2players_2001.json");
    spec.set("mapId", Json::number(int64_t(424242)));
    std::string err;
    CHECK(!bw::reservationFromSpec(spec, nc, &err) && err.find("424242") != std::string::npos);
}

TEST(bw_node_cancels_when_players_do_not_come) {
    bw::NodeConfig nc;
    nc.ticketSecret = "0123456789abcdef0123456789abcdef-node";
    nc.internalToken = "t";
    nc.metaBaseUrl = "http://meta.mock/internal/battle/";
    nc.gameId = "any";
    nc.defaultMap = "maps/x.json";
    nc.joinTimeoutSec = 2;
    bw::BattleNode node(nc);
    std::vector<bw::HttpRequest> posted;
    node.transportOverride = [&](const bw::HttpRequest& r, std::function<void(bw::HttpResponse)> done) {
        posted.push_back(r);
        bw::HttpResponse fail;   // the meta is down: the node retries 3 times, then logs the report
        fail.status = 503;
        done(fail);
    };
    net::ServerConfig sc;
    sc.udpPort = 0; sc.wsPort = 0; sc.log = false;
    node.install(sc);
    net::Server server(sc);
    node.attach(server);
    bw::HttpRequest req;
    req.method = "POST"; req.url = "/internal/games"; req.body = readFile(kData + "game_spec.json");
    req.headers.push_back({"X-Internal-Token", "t"});
    CHECK(node.handle(req).status == 201);
    for (uint64_t now = 1000; now < 5000; now += 50) { server.update(now); node.update(now); }
    CHECK(server.reservations() == 0);
    int results = 0;
    for (auto& p : posted)
        if (p.url.find("/games/900001/result") != std::string::npos) {
            ++results;
            Json rep = Json::parse(p.body);
            CHECK(rep.getStr("closedFrom") == "PROGRESS" && rep["history"].size() == 0 && rep.getStr("strStates") == "nobody joined");
        }
    CHECK(results == 3);
    // Bad specs are refused.
    req.body = R"({"gameId":5,"players":[{"user":{"id":1}}]})";
    CHECK(node.handle(req).status == 400);
    req.body = "{not json";
    CHECK(node.handle(req).status == 400);
}

TEST(bw_http_loopback) {
    // The plain HTTP server and client against each other on a real socket.
    bw::HttpServer srv([](const bw::HttpRequest& r) {
        bw::HttpResponse res;
        res.status = r.url == "/echo" ? 200 : 404;
        res.body = r.body;
        res.headers.push_back({"E-Tag", "x1"});
        return res;
    });
    if (!srv.listen(0, true)) { std::printf("  (no loopback sockets: skipped)\n"); return; }
    bw::HttpClient cli;
    bw::HttpRequest req;
    req.url = "http://127.0.0.1:" + std::to_string(srv.port()) + "/echo";
    req.body = std::string(50000, 'q');
    bw::HttpResponse got;
    bool done = false;
    cli.request(req, [&](bw::HttpResponse r) { got = r; done = true; });
    for (uint64_t now = 1; now < 5000 && !done; ++now) { srv.update(now); cli.update(now); }
    CHECK(done && got.status == 200 && got.body.size() == 50000 && got.header("e-tag") == "x1");
    bw::HttpResponse parsed;
    CHECK(bw::parseHttpResponse("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n2\r\nde\r\n0\r\n\r\n", parsed, false) && parsed.body == "abcde");
    bw::UrlParts u;
    CHECK(bw::parseUrl("http://[::1]:8080/a/b?c", u) && u.host == "::1" && u.port == 8080 && u.path == "/a/b?c");
}

// ---------------------------------------------------------------- client-exact TCP (TcpConnector)

TEST(bw_frames_match_the_unity_client) {
    auto hexFrame = [](const std::string& func, const std::string& msg) { return hex(bw::encodeFrame({func, 0, msg})); };
    bw::MatchSession s;
    s.connect("");                                     // MsgConnect sends nothing without a token
    CHECK(s.takeOutgoing().empty());
    s.connect("tok");
    s.ping(1234, 5);
    s.pingRaw(99);
    s.reconnect(9, 120);
    const std::string all = hex(s.takeOutgoing());
    CHECK(all == hexFrame("Connect", "tok") + hexFrame("GamePing", R"({"ms":1234,"tick":5})") + hexFrame("GamePing", "99") +
                     hexFrame("MatchReconnect", R"({"roomId":9,"lastTick":120})"));
    CHECK(hexFrame("Connect", "tok") == "0007436F6E6E6563740003746F6B");
    // Message.WriteToStream refuses strings of 32768+ bytes; so do we (and report it).
    CHECK(bw::encodeFrame({"NotifyGame", 0, std::string(32768, 'a')}).empty());
    CHECK(bw::encodeFrame({"NotifyGame", 0, std::string(32767, 'a')}).size() == 4 + 10 + 32767);
    s.send("NotifyGame", std::string(40000, 'a'));
    auto ev = s.takeEvents();
    CHECK(s.takeOutgoing().empty() && ev.size() == 1 && ev[0].kind == bw::MatchSession::Event::Kind::Error);
    // Client side reads the whole channel index (int.Parse).
    bw::FrameReader r;
    r.feed(bw::encodeFrame({"NotifyGame", 12, "x"}));
    bw::Frame f;
    CHECK(r.next(f) && f.func == "NotifyGame" && f.idx == 12);
}

namespace {
// A fake BW TCP server (matchmaker / chat) on 127.0.0.1.
struct FakeTcp {
    net::TcpListener l;
    std::vector<std::unique_ptr<net::TcpSocket>> socks;
    std::vector<std::string> got;   // received bytes per accepted socket
    bool open() { return l.listen(0, true); }
    int port() const { return l.localPort(); }
    void update() {
        while (auto s = l.accept()) { socks.push_back(std::move(s)); got.emplace_back(); }
        uint8_t buf[4096];
        for (size_t i = 0; i < socks.size(); ++i) {
            if (!socks[i]) continue;
            for (;;) {
                const int n = socks[i]->recv(buf, sizeof buf);
                if (n <= 0) { if (n < 0) socks[i].reset(); break; }
                got[i].append(reinterpret_cast<const char*>(buf), size_t(n));
            }
        }
    }
    void send(size_t i, const bw::Bytes& b) { if (i < socks.size() && socks[i]) socks[i]->send(b.data(), b.size()); }
    void drop(size_t i) { if (i < socks.size()) socks[i].reset(); }
};
std::string bytesOf(const bw::Bytes& b) { return std::string(b.begin(), b.end()); }
int closedPort() {   // a port nothing listens on
    net::TcpListener l;
    if (!l.listen(0, true)) return 1;
    const int p = l.localPort();
    l.close();
    return p;
}
void tick(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
uint64_t wallMs() { return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); }
} // namespace

TEST(bw_matchmaker_connection_host_group_ping_reconnect) {
    FakeTcp mm;
    if (!mm.open()) { std::printf("  (no loopback sockets: skipped)\n"); return; }
    bw::MatchConnectionConfig cfg;
    cfg.hosts = {{"127.0.0.1", closedPort()}, {"127.0.0.1", mm.port()}};   // first host is down: the group moves on
    cfg.token = "tok-1";
    cfg.connectTimeoutMs = 1000;
    cfg.pingIntervalMs = 200;
    bw::MatchConnection c(cfg);
    uint64_t t0 = wallMs();
    c.start(t0);
    auto run = [&](int ms, const std::function<bool()>& until) {
        const uint64_t end = wallMs() + uint64_t(ms);
        while (wallMs() < end && !until()) { c.update(wallMs()); mm.update(); tick(2); }
    };
    run(3000, [&] { return !mm.got.empty() && mm.got[0].size() >= 16; });
    CHECK(c.hostIndex() == 1 && mm.got.size() == 1);
    const std::string connect = bytesOf(bw::encodeFrame({"Connect", 0, "tok-1"}));
    CHECK(!mm.got.empty() && mm.got[0].rfind(connect, 0) == 0);
    bw::MatchParams p;
    p.mapId = 101;
    c.session().match(p);
    mm.send(0, bw::encodeFrame({"Connected", 0, ""}));
    mm.send(0, bw::encodeFrame({"Match.onUpdate", 0, R"({"roomId":555,"mapId":101})"}));
    run(1000, [&] { return mm.got[0].find("GamePing") != std::string::npos && mm.got[0].find("\"mapId\":101") != std::string::npos; });
    CHECK(mm.got[0].find(bytesOf(bw::encodeFrame({"Match", 0, p.toJson().dump(-1)}))) != std::string::npos);
    CHECK(mm.got[0].find("{\"ms\":") != std::string::npos);
    auto ev = c.session().takeEvents();
    bool connected = false, update = false;
    for (auto& e : ev) { connected |= e.kind == bw::MatchSession::Event::Kind::Connected; update |= e.kind == bw::MatchSession::Event::Kind::Update; }
    CHECK(connected && update);
    // The matchmaker drops us mid-search: Int.onDisconnect, 0.5 s later Connect + MatchReconnect.
    c.reconnect(555, 77);
    mm.drop(0);
    const uint64_t dropAt = wallMs();
    run(4000, [&] { return mm.got.size() == 2 && mm.got[1].find("MatchReconnect") != std::string::npos; });
    CHECK(mm.got.size() == 2 && wallMs() - dropAt >= 450 && c.reconnects() == 1);
    if (mm.got.size() == 2)
        CHECK(mm.got[1].rfind(connect + bytesOf(bw::encodeFrame({"MatchReconnect", 0, R"({"roomId":555,"lastTick":77})"})), 0) == 0);
    bool disc = false;
    for (auto& e : c.session().takeEvents()) disc |= e.kind == bw::MatchSession::Event::Kind::Disconnected && e.frame.func == "Int.onDisconnect";
    CHECK(disc);
    c.stop();
}

TEST(bw_host_probes_pick_the_answering_host) {
    FakeTcp silent, game, chat;
    if (!silent.open() || !game.open() || !chat.open()) { std::printf("  (no loopback sockets: skipped)\n"); return; }
    std::vector<bw::HostPort> hosts = {{"127.0.0.1", closedPort()}, {"127.0.0.1", silent.port()}, {"127.0.0.1", game.port()}};
    bw::HostProbe probe(bw::HostProbe::Kind::Game, hosts, "tok-9", wallMs());
    const std::string ping = bytesOf(bw::encodeFrame({"Ping", 0, "tok-9"}));
    bool answered = false;
    for (uint64_t end = wallMs() + 4000; wallMs() < end && !probe.done(); tick(2)) {
        probe.update(wallMs());
        silent.update();
        game.update();
        if (!answered && !game.got.empty() && game.got[0] == ping) { game.send(0, bw::encodeFrame({"onPing", 0, ""})); answered = true; }
    }
    CHECK(answered && probe.done() && probe.winner() == 2);
    CHECK(!silent.got.empty() && silent.got[0] == ping);   // everyone was asked at once
    // Chat: the ping packet is [type 2][len 1][0]; any reply wins.
    bw::HostProbe chatProbe(bw::HostProbe::Kind::Chat, {{"127.0.0.1", closedPort()}, {"127.0.0.1", chat.port()}}, "", wallMs());
    bool replied = false;
    for (uint64_t end = wallMs() + 4000; wallMs() < end && !chatProbe.done(); tick(2)) {
        chatProbe.update(wallMs());
        chat.update();
        if (!replied && !chat.got.empty() && chat.got[0] == std::string("\x00\x02\x00\x01\x00", 5)) {
            chat.send(0, bw::encodeChatServerPacket(0x30, 1, "{}"));
            replied = true;
        }
    }
    CHECK(replied && chatProbe.winner() == 1);
    // Nobody answers: done without a winner (the silent host times out after the answer window).
    bw::HostProbe none(bw::HostProbe::Kind::Game, {{"127.0.0.1", silent.port()}}, "tok", wallMs(), 1000, 300);
    for (uint64_t end = wallMs() + 3000; wallMs() < end && !none.done(); tick(2)) { none.update(wallMs()); silent.update(); }
    CHECK(none.done() && none.winner() == -1);
    CHECK(bw::parseHostPorts({"mm.mock:7777", "https://x:1", "noport"}).size() == 2);
}

TEST(bw_chat_connection_auth_and_packets) {
    FakeTcp chat;
    if (!chat.open()) { std::printf("  (no loopback sockets: skipped)\n"); return; }
    bw::ChatConnection c({{"127.0.0.1", chat.port()}}, "tok-c");
    c.start(wallMs());
    bool sent = false;
    std::vector<bw::ChatPacket> got;
    for (uint64_t end = wallMs() + 3000; wallMs() < end && got.empty(); tick(2)) {
        c.update(wallMs());
        chat.update();
        if (!sent && !chat.got.empty() && chat.got[0] == bytesOf(bw::encodeChatAuth("tok-c"))) {
            chat.send(0, bw::encodeChatServerPacket(uint16_t(bw::ChatType::OnMsg), 3, R"({"room":"global","msg":"hi"})", 8));
            sent = true;
        }
        for (auto& p : c.takePackets()) got.push_back(p);
    }
    CHECK(sent && got.size() == 1 && got[0].json == R"({"room":"global","msg":"hi"})");
    c.send(bw::ChatType::OnMsg, R"({"room":"global","msg":"yo"})");
    for (int i = 0; i < 50; ++i) { c.update(wallMs()); chat.update(); tick(2); }
    CHECK(chat.got[0].find(bytesOf(bw::encodeChatPacket(0, R"({"room":"global","msg":"yo"})"))) != std::string::npos);
    c.stop();
}

TEST(bw_platform_https_transport_out_of_the_box) {
    // The default transport is the engine's per-platform HTTP stack (libcurl here).
    if (!bw::platformHttpAvailable()) { std::printf("  (no platform HTTP stack in this build: skipped)\n"); return; }
    bw::HttpRequest seen;
    bw::HttpServer srv([&](const bw::HttpRequest& r) {
        seen = r;
        bw::HttpResponse res;
        res.status = 200;
        res.body = R"({"i":-1,"data":{"news":[{"id":1}]}})";
        res.headers.push_back({"E-Tag", "n1"});
        return res;
    });
    if (!srv.listen(0, true)) { std::printf("  (no loopback sockets: skipped)\n"); return; }
    bw::ApiClient api(bw::platformTransport(), {"http://127.0.0.1:" + std::to_string(srv.port())});
    api.setToken("tok-h");
    bw::ApiResult res;
    bool done = false;
    bw::CallOptions o;
    o.cacheable = true;
    api.call("v/news/get", Json::parse(R"({"lang":"en"})"), [&](bw::ApiResult r) { res = r; done = true; }, o);
    for (uint64_t end = wallMs() + 5000; wallMs() < end && !done; tick(2)) { srv.update(wallMs()); api.pump(); }
    CHECK(done && res.ok() && res.data["news"].size() == 1);
    CHECK(seen.method == "POST" && seen.url == "/v/news/get" && Json::parse(seen.body)["data"].getStr("lang") == "en");
    bw::HttpResponse hdrs;
    hdrs.headers = seen.headers;
    CHECK(hdrs.header("token") == "tok-h" && hdrs.header("content-type").find("application/json") == 0);
    // The second call sends the E-Tag the platform stack returned.
    done = false;
    api.call("v/news/get", Json::parse(R"({"lang":"en"})"), [&](bw::ApiResult r) { res = r; done = true; }, o);
    for (uint64_t end = wallMs() + 5000; wallMs() < end && !done; tick(2)) { srv.update(wallMs()); api.pump(); }
    hdrs.headers = seen.headers;
    CHECK(done && hdrs.header("E-Tag") == "n1");
    // Nothing listening: a transport error, not a crash or a hang.
    bw::ApiClient down(bw::platformTransport(), {"http://127.0.0.1:" + std::to_string(closedPort())});
    done = false;
    down.call("v/news/get", Json::object(), [&](bw::ApiResult r) { res = r; done = true; });
    for (uint64_t end = wallMs() + 5000; wallMs() < end && !done; tick(5)) down.pump();
    CHECK(done && res.transport && res.error.code == int(bw::ApiCode::TransportError));
}

TEST(bw_chat_through_the_backend) {
    // chat.* requests over the chat TCP connection, incoming packets into the backend's chat state.
    MockServer m;
    bw::ApiClient api(m.transport(), {"http://data.mock"});
    bw::BwBackend backend(api);
    meta::Client client(backend);
    backend.setChatIdentity(4242, "Commander");
    CHECK(client.begin({"chat", "send", "c1", Json::parse(R"({"room":"global","msg":"hi"})")}));
    client.pump();
    CHECK(client.reply().result.code == Code::Unavailable);   // not connected yet
    FakeTcp chat;
    if (!chat.open()) { std::printf("  (no loopback sockets: skipped)\n"); return; }
    bw::ChatConnection link({{"127.0.0.1", chat.port()}}, "tok-c");
    bw::bindChat(backend, link);
    link.start(wallMs());
    for (uint64_t end = wallMs() + 2000; wallMs() < end && !link.connected(); tick(2)) { link.update(wallMs()); chat.update(); }
    CHECK(link.connected());
    CHECK(client.begin({"chat", "send", "c2", Json::parse(R"({"room":"global","msg":"hi"})")}));
    client.pump();
    CHECK(client.reply().result.code == Code::Ok);
    CHECK(client.begin({"chat", "history", "c3", Json::parse(R"({"room":"global","count":2})")}));
    for (int i = 0; i < 100; ++i) { link.update(wallMs()); chat.update(); client.pump(); tick(2); }
    CHECK(client.pending());   // waits for OnHistory
    const std::string sent = chat.got.empty() ? "" : chat.got[0];
    CHECK(sent.find(bytesOf(bw::encodeChatAuth("tok-c"))) == 0);
    CHECK(sent.find(R"({"messageType":0,"ts":0,"room":"global","senderId":4242,"sender":"Commander","msg":"hi"})") != std::string::npos);
    CHECK(sent.find(bytesOf(bw::encodeChatPacket(3, R"({"room":"global","historyCount":2})"))) != std::string::npos);
    chat.send(0, bw::encodeChatServerPacket(2, 7, R"({"list":[{"room":"global","sender":"Rival","msg":"gg"}]})"));
    chat.send(0, bw::encodeChatServerPacket(0x11, 8, R"({"id":5,"senderId":5151,"senderName":"Rival"})"));
    for (uint64_t end = wallMs() + 2000; wallMs() < end && client.pending(); tick(2)) { link.update(wallMs()); chat.update(); client.pump(); }
    CHECK(!client.pending() && client.reply().result.code == Code::Ok && client.reply().data["list"].size() == 1);
    for (int i = 0; i < 20; ++i) { link.update(wallMs()); chat.update(); tick(2); }
    CHECK(backend.chat()["rooms"]["global"].size() == 1 && backend.chat()["rooms"]["global"][0].getStr("msg") == "gg");
    CHECK(backend.chat()["notifications"].size() == 1 && backend.chat()["notifications"][0].getInt("type") == 0x11);
    CHECK(client.begin({"chat", "notifyAccept", "c4", Json::parse(R"({"id":5})")}));
    client.pump();
    for (int i = 0; i < 20; ++i) { link.update(wallMs()); chat.update(); tick(2); }
    CHECK(chat.got[0].find(bytesOf(bw::encodeChatPacket(0x21, R"({"reqId":5,"id":5})"))) != std::string::npos);
    link.stop();
}

TEST(bw_match_session_spectate) {
    bw::MatchSession s;
    s.spectate(900001, false);
    bw::FrameReader r;
    r.feed(s.takeOutgoing());
    bw::Frame f;
    CHECK(r.next(f) && f.func == "Spectate" && f.msg == R"({"roomId":900001,"custom":false})");
    s.feed(bw::encodeFrame({"onSpectateJoin", 0, R"({"z":0,"battleHost":"node1","battlePort":27015,"battleTicket":"v1.s.t"})"}));
    auto ev = s.takeEvents();
    CHECK(ev.size() == 1 && ev[0].kind == bw::MatchSession::Event::Kind::SpectateJoin);
    CHECK(s.handoff() && s.handoff()->spectator && s.handoff()->queueId() == "ticket:v1.s.t");
    s.feed(bw::encodeFrame({"onSpectateJoin", 0, R"({"z":0,"error":"game.not_found"})"}));
    ev = s.takeEvents();
    CHECK(ev.size() == 1 && ev[0].kind == bw::MatchSession::Event::Kind::Error);
}

TEST(bw_meta_tutorials_runner) {
    bw::MetaTutorials t;
    CHECK(t.load(Json::parse(R"({"tutorials":[
      {"name":"A","guid":"g1","forObject":"MW2.UI.Mobile.MainScreen.MainScreen","startEvent":3,
       "conditions":[{"type":"HasKey","key":"Done","conditionValue":0,"value":0},
                     {"type":"HasProfileContent","checkType":0,"features":[{"contentType":0,"haveNot":0,"id":7,"level":2,"conditionValue":5}],"valuesCheckType":1,
                      "values":[{"contentType":4,"conditionValue":5,"value":100}]},
                     {"type":"SplitTest","splitTest":"st169.a","isNot":0}],
       "actions":[{"type":"ShowHideComponents","state":0,"tutorialTag":"Hint"},{"type":"Wait","seconds":0.5},
                  {"type":"LockElement","tutorialTag":"Play"},{"type":"WaitButtonClick","tutorialTag":"Play"},
                  {"type":"SetKey","key":"Done","operation":0,"value":1,"save":1},
                  {"type":"RunTutorial","nextTutorialGuid":"g2","breakExecution":0},{"type":"PushNotification","data":{"tag":"x"}}]},
      {"name":"B","guid":"g2","forObject":"","startEvent":5,"conditions":[{"type":"HasKey","key":"Done","conditionValue":0,"value":1},
                     {"type":"HasKeyTimer","key":"Done","seconds":60,"result":1,"conditionValue":0,"value":1}],
       "actions":[{"type":"WaitForClick"},{"type":"CounterAdd","id":1,"value":2}]},
      {"name":"C","guid":"g3","forObject":"MW2.UI.Mobile.Clan.ClanScreen","startEvent":3,"conditions":[{"type":"CurrentClanSeasonHasRewards"}],"actions":[]}
    ]})")));
    t.setProfile(Json::parse(R"({"userId":4242,"gold":1250,"heroes":[{"clientId":7,"level":4}],"ab":{"tests":["st169.a"]}})"));
    t.setNow(1000);
    std::vector<std::string> log;
    t.host.showHide = [&](const std::string& tag, bool show, const Json&) { log.push_back((show ? "show " : "hide ") + tag); };
    t.host.lockElement = [&](const std::string& tag, const Json&) { log.push_back("lock " + tag); };
    t.host.action = [&](const Json& a, const std::string& w) { log.push_back(a.getStr("type") + "@" + w); };
    t.host.keyChanged = [&](const std::string& k, int64_t v, bool save) { log.push_back("key " + k + "=" + std::to_string(v) + (save ? " save" : "")); };
    CHECK(t.windowEvent("main", bw::TutorialEvent::OnShowEnd) == 0);         // wrong event
    CHECK(t.windowEvent("main", bw::TutorialEvent::OnWindowActive) == 1);    // engine screen id of MainScreen
    CHECK(log.size() == 1 && log[0] == "show Hint");
    t.update(0.3);
    CHECK(log.size() == 1);
    t.update(0.3);
    CHECK(log.size() == 2 && log[1] == "lock Play");
    t.buttonClick("Other");
    CHECK(log.size() == 2);
    t.buttonClick("Play");
    CHECK(log.size() == 3 && log[2] == "key Done=1 save" && t.keys().at("Done") == 1 && t.keys().at("Done.Timer") == 1000);
    t.keySaved();   // RunTutorial B (its conditions hold now), then A goes on
    CHECK(log.size() == 4 && log[3] == "PushNotification@main");
    auto running = t.runningNames();
    CHECK(running.size() == 1 && running[0] == "B");
    t.click();
    CHECK(t.runningNames().empty());
    CHECK(t.windowEvent("MainScreen", bw::TutorialEvent::OnWindowActive) == 0);   // Done = 1 now
    t.setNow(1100);
    CHECK(!t.run("B"));   // HasKeyTimer: the 60 s window after SetKey is over
    // Unknown client-side conditions go to the host, else they fail and are listed.
    CHECK(t.windowEvent("clan", bw::TutorialEvent::OnWindowActive) == 0);
    CHECK(t.unsupported().count("CurrentClanSeasonHasRewards") == 1);
    t.host.condition = [](const Json& c, bool& r) { r = c.getStr("type") == "CurrentClanSeasonHasRewards"; return true; };
    CHECK(t.windowEvent("MW2.UI.Mobile.Clan.ClanScreen", bw::TutorialEvent::OnWindowActive) == 1);
    CHECK(bw::bwCompare(2, 5, 3) && !bw::bwCompare(2, 5, 7) && bw::bwCompare(5, 2, 2));
    CHECK(bw::MetaTutorials::windowMatches("findmatch", "MW2.UI.Mobile.FindMatch.FindMatchScreen"));
}

TEST(bw_meta_tutorials_real_data) {
    // ui/bw_tutorials.json of templates/bunkerwars (tools/meta/bw_tutorials.py from the BW client).
    Json doc = Json::parseFile(std::string(AIVERSE_SOURCE_DIR) + "/templates/bunkerwars/ui/bw_tutorials.json");
    bw::MetaTutorials t;
    CHECK(t.load(doc) && t.size() == 217);
    t.setProfile(Json::parse(readFile(kData + "profile_get.json")));
    int actions = 0;
    t.host.showHide = [&](const std::string&, bool, const Json&) { ++actions; };
    t.host.lockElement = [&](const std::string&, const Json&) { ++actions; };
    t.host.action = [&](const Json&, const std::string&) { ++actions; };
    std::set<std::string> windows;
    for (auto& d : doc["tutorials"].items()) windows.insert(d.getStr("forObject"));
    int started = 0;
    for (auto& w : windows)
        for (int ev = 0; ev < 5; ++ev) started += t.windowEvent(w, bw::TutorialEvent(ev));
    for (int i = 0; i < 50; ++i) { t.update(1.0); t.click(); }
    CHECK(started > 0 && actions > 0);   // a fresh profile starts the first-session tutorials
    std::printf("    %d tutorials started, %d UI actions; host conditions:", started, actions);
    for (auto& u : t.unsupported()) std::printf(" %s", u.c_str());
    std::printf("\n");
}

int main() {
    for (auto& [name, fn] : registry()) {
        const int before = g_failed;
        std::printf("%s\n", name);
        fn();
        if (g_failed != before) std::printf("  ^ failed\n");
    }
    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
