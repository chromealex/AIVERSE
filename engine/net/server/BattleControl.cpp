#include "BattleControl.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include "../NetClient.h"
#ifndef __EMSCRIPTEN__
#include "../Socket.h"
#endif

namespace aiverse::net {

namespace {
uint64_t u64Of(const Json& j, uint64_t def = 0) {
    if (j.isString()) return std::strtoull(j.str().c_str(), nullptr, 10);
    if (j.isNumber()) return std::strtoull(j.numberLiteral().c_str(), nullptr, 10);
    return def;
}
std::string u64Str(uint64_t v) { return std::to_string(v); }
std::string u64Hex(uint64_t v) { char b[24]; std::snprintf(b, sizeof(b), "%016llx", (unsigned long long)v); return b; }
bool hexBytes(const Json& j, Bytes& out) { out.clear(); return j.isNull() || fromHexString(j.str(), out); }
Json err(const std::string& e) { Json r = Json::object(); r.set("ok", Json::fromBool(false)); r.set("error", Json::string(e)); return r; }
} // namespace

bool allocationFromJson(const Json& j, const std::map<std::string, std::string>& gameDirs, MatchAllocation& a, std::string& error) {
    a = MatchAllocation{};
    a.matchId = u64Of(j["matchId"]);
    a.gameId = j.getStr("gameId");
    a.compatKey = j.getStr("compatKey");
    a.mapId = j.getStr("mapId");
    a.seed = u64Of(j["seed"]);
    a.settings = battleSettings(uint32_t(j.getInt("tickRate", 20)));
    if (j.has("inputDelay")) a.settings.inputDelay = uint32_t(j.getInt("inputDelay"));
    if (j.has("rollbackWindow")) a.settings.rollbackWindow = uint32_t(j.getInt("rollbackWindow"));
    if (j.has("lagToleranceMs")) a.settings.lagToleranceMs = uint32_t(j.getInt("lagToleranceMs"));
    a.joinTimeoutMs = uint32_t(j.getInt("joinTimeoutMs", a.joinTimeoutMs));
    a.maxDurationSec = uint32_t(j.getInt("maxDurationSec", a.maxDurationSec));
    a.maxCommandsPerTick = uint32_t(j.getInt("maxCommandsPerTick", a.maxCommandsPerTick));
    for (auto& c : j["allowedCommands"].items()) a.allowedCommands.push_back(uint16_t(c.integer()));
    if (!hexBytes(j["gameData"], a.gameData)) { error = "gameData is not hex"; return false; }
    const Json& m = j["map"];
    if (m.isString()) {
        auto it = gameDirs.find(a.gameId);
        if (it == gameDirs.end()) { error = "map given by name but game '" + a.gameId + "' has no folder on this server"; return false; }
        try { a.map = describeMap(it->second, m.str()); } catch (const std::exception& e) { error = std::string("map: ") + e.what(); return false; }
    } else if (m.isObject()) {
        a.map.file = m.getStr("file"); a.map.title = m.getStr("title", a.map.file);
        a.map.dataHash = std::strtoull(m.getStr("dataHash").c_str(), nullptr, 16);
        for (auto& t : m["slotTeams"].items()) a.map.slotTeams.push_back(uint8_t(t.integer()));
    } else { error = "map is missing"; return false; }
    if (a.mapId.empty()) a.mapId = a.map.file;
    for (auto& p : j["players"].items()) {
        MatchAllocation::Player ap;
        ap.playerId = p.getStr("playerId");
        ap.info.slot = uint8_t(p.getInt("slot"));
        ap.info.team = uint8_t(p.getInt("team", ap.info.slot <= a.map.players() && ap.info.slot ? a.map.slotTeams[ap.info.slot - 1] : 0));
        ap.info.kind = p.getBool("bot") ? SlotKind::Bot : SlotKind::Human;
        ap.info.name = p.getStr("name");
        if (!hexBytes(p["loadout"], ap.info.loadout)) { error = "loadout of slot " + std::to_string(ap.info.slot) + " is not hex"; return false; }
        a.players.push_back(std::move(ap));
    }
    return true;
}

Json reportToJson(const MatchReport& r) {
    Json j = Json::object();
    j.set("matchId", Json::string(u64Str(r.matchId)));
    j.set("result", Json::number(int64_t(r.result)));
    j.set("reason", Json::string(r.reason.key));
    j.set("endTick", Json::number(int64_t(r.endTick)));
    j.set("endHash", Json::string(u64Hex(r.endHash)));
    j.set("durationMs", Json::number(int64_t(r.durationMs)));
    j.set("verified", Json::fromBool(r.verified));
    j.set("desync", Json::fromBool(r.desync));
    j.set("aborted", Json::fromBool(r.aborted));
    j.set("consensus", Json::fromBool(r.consensus));
    j.set("report", Json::string(toHexString(r.report.data(), r.report.size())));
    if (!r.replayPath.empty()) j.set("replay", Json::string(r.replayPath));
    Json ps = Json::array();
    for (auto& p : r.players) {
        Json q = Json::object();
        q.set("playerId", Json::string(p.playerId));
        q.set("slot", Json::number(int64_t(p.slot)));
        q.set("team", Json::number(int64_t(p.team)));
        q.set("joined", Json::fromBool(p.joined));
        q.set("left", Json::fromBool(p.left));
        q.set("connectedAtEnd", Json::fromBool(p.connectedAtEnd));
        q.set("droppedCommands", Json::number(int64_t(p.droppedCommands)));
        q.set("report", Json::string(toHexString(p.report.data(), p.report.size())));
        ps.push(std::move(q));
    }
    j.set("players", std::move(ps));
    return j;
}

Json hostedToJson(const HostedMatchInfo& h) {
    static const char* phases[] = {"waiting", "running", "finished"};
    Json j = Json::object();
    j.set("matchId", Json::string(u64Str(h.matchId)));
    j.set("phase", Json::string(phases[h.phase < 3 ? h.phase : 2]));
    j.set("joined", Json::number(int64_t(h.joined)));
    j.set("expected", Json::number(int64_t(h.expected)));
    j.set("tick", Json::number(int64_t(h.tick)));
    j.set("ageMs", Json::number(int64_t(h.ageMs)));
    return j;
}

std::string signControl(const Key& key, const std::string& json) {
    auto mac = keyedHash(key, reinterpret_cast<const uint8_t*>(json.data()), json.size(), 32);
    return toHexString(mac.data(), mac.size()) + " " + json;
}

bool verifyControl(const Key& key, const std::string& line, std::string& json) {
    const auto sp = line.find(' ');
    if (sp != 64) return false;
    Bytes mac;
    if (!fromHexString(line.substr(0, 64), mac)) return false;
    json = line.substr(65);
    auto want = keyedHash(key, reinterpret_cast<const uint8_t*>(json.data()), json.size(), 32);
    return equalBytes(mac.data(), want.data(), 32);
}

#ifndef __EMSCRIPTEN__
struct BattleControl::Net {
    TcpListener listener;
    struct Peer { std::unique_ptr<TcpSocket> sock; std::string in, out; };
    std::vector<Peer> peers;
};
#else
struct BattleControl::Net {};
#endif

BattleControl::BattleControl(Server& server, BattleControlConfig cfg, std::map<std::string, std::string> gameDirs)
    : server_(server), cfg_(std::move(cfg)), gameDirs_(std::move(gameDirs)), net_(std::make_unique<Net>()) {}
BattleControl::~BattleControl() = default;

uint32_t BattleControl::running() const {
    uint32_t n = 0;
    for (auto& h : server_.hostedMatches()) if (h.phase < 2) ++n;
    return n;
}

Json BattleControl::handle(const Json& req) {
    const std::string op = req.getStr("op");
    Json r = Json::object();
    r.set("ok", Json::fromBool(true));
    r.set("op", Json::string(op));
    r.set("nonce", Json::string(req.getStr("nonce")));
    r.set("ts", Json::string(u64Str(cfg_.clock())));
    if (op == "allocate") {
        if (running() >= cfg_.maxMatches) return err("full: " + std::to_string(cfg_.maxMatches) + " matches running");
        MatchAllocation a;
        std::string e;
        if (!allocationFromJson(req["match"], gameDirs_, a, e) || !server_.allocateMatch(a, &e)) return err(e);
        r.set("matchId", Json::string(u64Str(a.matchId)));
        r.set("server", Json::string(cfg_.publicAddress));
        return r;
    }
    if (op == "release") {
        const uint64_t id = u64Of(req["matchId"]);
        if (!server_.releaseMatch(id)) return err("no running match " + u64Str(id));
        return r;
    }
    if (op == "status") {
        Json ms = Json::array();
        for (auto& h : server_.hostedMatches()) ms.push(hostedToJson(h));
        auto st = server_.stats();
        r.set("capacity", Json::number(int64_t(cfg_.maxMatches)));
        r.set("running", Json::number(int64_t(running())));
        r.set("clients", Json::number(int64_t(st.clients)));
        r.set("desyncs", Json::number(int64_t(st.desyncs)));
        r.set("matches", std::move(ms));
        return r;
    }
    if (op == "reports") {
        Json rs = Json::array();
        for (auto& rep : server_.takeReports()) rs.push(reportToJson(rep));
        r.set("reports", std::move(rs));
        return r;
    }
    return err("unknown op '" + op + "'");
}

std::string BattleControl::handleLine(const std::string& line) {
    std::string text;
    Json resp;
    if (!verifyControl(cfg_.controlKey, line, text)) resp = err("bad signature");
    else {
        try {
            Json req = Json::parse(text);
            const uint64_t ts = u64Of(req["ts"]), now = cfg_.clock();
            const std::string nonce = req.getStr("nonce");
            if (nonce.empty() || nonce.size() > 64) resp = err("nonce required");
            else if (ts + cfg_.clockSkewMs < now || ts > now + cfg_.clockSkewMs) resp = err("stale request (check clocks)");
            else if (nonces_.count(nonce)) resp = err("replayed request");
            else {
                nonces_.insert(nonce); nonceOrder_.push_back(nonce);
                if (nonceOrder_.size() > 8192) { nonces_.erase(nonceOrder_.front()); nonceOrder_.pop_front(); }
                resp = handle(req);
            }
        } catch (const std::exception& e) { resp = err(std::string("bad request: ") + e.what()); }
    }
    return signControl(cfg_.controlKey, resp.dump(-1));
}

#ifndef __EMSCRIPTEN__
bool BattleControl::listen(uint16_t port, bool loopbackOnly, std::string* error) {
    if (!net_->listener.listen(port, loopbackOnly)) { if (error) *error = "cannot bind control port " + std::to_string(port); return false; }
    return true;
}
uint16_t BattleControl::port() const { return net_->listener.localPort(); }

void BattleControl::update(uint64_t) {
    auto& N = *net_;
    while (auto s = N.listener.accept()) { if (N.peers.size() < 16) N.peers.push_back({std::move(s), {}, {}}); }
    for (auto& p : N.peers) {
        uint8_t buf[4096];
        int n;
        while (p.sock && (n = p.sock->recv(buf, sizeof(buf))) > 0) p.in.append(reinterpret_cast<char*>(buf), size_t(n));
        if (p.sock && n < 0) p.sock.reset();
        if (p.in.size() > (1u << 20)) p.sock.reset();     // a line that big is not ours
        size_t nl;
        while (p.sock && (nl = p.in.find('\n')) != std::string::npos) {
            std::string line = p.in.substr(0, nl);
            p.in.erase(0, nl + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            p.out += handleLine(line) + "\n";
        }
        while (p.sock && !p.out.empty()) {
            int w = p.sock->send(reinterpret_cast<const uint8_t*>(p.out.data()), p.out.size());
            if (w < 0) { p.sock.reset(); break; }
            if (w == 0) break;
            p.out.erase(0, size_t(w));
        }
    }
    N.peers.erase(std::remove_if(N.peers.begin(), N.peers.end(), [](auto& p) { return !p.sock; }), N.peers.end());
}
#else
void BattleControl::update(uint64_t) {}
#endif

} // namespace aiverse::net
