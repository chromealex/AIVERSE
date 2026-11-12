#ifndef __EMSCRIPTEN__
#include "BwNode.h"
#include "BwWire.h"
#include "engine/meta/Encoding.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace aiverse::meta::bw {

NodeConfig NodeConfig::parse(const Json& j) {
    NodeConfig c;
    auto s = [&](const char* k, std::string& v) { if (j[k].isString()) v = j.getStr(k); };
    s("nodeId", c.nodeId); s("region", c.region); s("publicHost", c.publicHost); s("internalUrl", c.internalUrl);
    s("ticketSecret", c.ticketSecret); s("internalToken", c.internalToken); s("metaBaseUrl", c.metaBaseUrl);
    s("matchmakerUrl", c.matchmakerUrl); s("gameId", c.gameId); s("defaultMap", c.defaultMap);
    c.clientPort = int(jsonInt(j["clientPort"], c.clientPort));
    c.clientPortSsl = int(jsonInt(j["clientPortSsl"], c.clientPortSsl));
    c.internalPort = uint16_t(jsonInt(j["internalPort"], c.internalPort));
    c.capacity = int(jsonInt(j["capacity"], c.capacity));
    c.heartbeatMs = uint32_t(jsonInt(j["heartbeatMs"], c.heartbeatMs));
    c.joinTimeoutSec = uint32_t(jsonInt(j["joinTimeoutSec"], c.joinTimeoutSec));
    c.settings.tickRate = uint32_t(jsonInt(j["tickRate"], c.settings.tickRate));
    c.settings.inputDelay = uint32_t(jsonInt(j["inputDelay"], c.settings.inputDelay));
    c.settings.rollbackWindow = uint32_t(jsonInt(j["rollbackWindow"], c.settings.rollbackWindow));
    for (auto& [k, v] : j["maps"].members()) c.maps[std::atoi(k.c_str())] = v.str();
    return c;
}

std::optional<net::Reservation> reservationFromSpec(const Json& spec, const NodeConfig& cfg, std::string* error) {
    auto fail = [&](const std::string& e) -> std::optional<net::Reservation> { if (error) *error = e; return std::nullopt; };
    const int64_t gameId = jsonInt(spec["gameId"]);
    if (gameId <= 0) return fail("gameId");
    const Json& players = spec["players"];
    if (!players.isArray() || players.size() < 2) return fail("players");
    const int mapId = int(jsonInt(spec["mapId"]));
    auto m = cfg.maps.find(mapId);
    if (m == cfg.maps.end() && cfg.defaultMap.empty()) return fail("no engine map for BW map " + std::to_string(mapId));
    net::Reservation r;
    r.externalId = std::to_string(gameId);
    r.gameId = cfg.gameId;
    r.mapFile = m != cfg.maps.end() ? m->second : cfg.defaultMap;
    r.settings = cfg.settings;
    r.joinTimeoutSec = cfg.joinTimeoutSec;
    r.fillMissingWithBots = false;   // BW: a player that never loads closes the game from PROGRESS
    for (auto& p : players.items()) {
        net::ReservedSeat s;
        s.bot = p.getBool("isBot") || p["user"].isNull();
        s.team = uint8_t(jsonInt(p["info"]["team"]));
        s.name = p["user"].getStr("name");
        const int64_t uid = jsonInt(p["user"]["id"]);
        s.userId = uid > 0 ? uint64_t(uid) : 0;
        if (!s.bot && !s.userId) return fail("human seat without user id");
        if (s.bot && s.name.empty()) s.name = "@net.bot";
        r.seats.push_back(std::move(s));
    }
    return r;
}

Json gameReport(const Json& spec, const net::ExternalMatchEnd& e, const std::string& replayBase64) {
    Json rep = Json::object();
    rep.set("gameId", Json::number(jsonInt(spec["gameId"])));
    rep.set("closedFrom", Json::string(e.started ? "RESULT" : "PROGRESS"));
    rep.set("spec", spec);
    if (e.started) rep.set("gameStartTime", Json::number(int64_t(e.startMs)));
    const bool decided = e.started && e.result != 0;
    Json players = Json::array();
    Json stats = Json::array();
    for (size_t i = 0; i < e.seats.size(); ++i) {
        const auto& s = e.seats[i];
        const Json& sp = spec["players"][i];
        const bool win = decided && e.result > 0 && s.team == uint8_t(e.result);
        const char* status = !decided ? "None" : e.result < 0 ? "Draw" : win ? "Victory" : "Defeat";
        Json p = Json::object();
        p.set("userId", Json::number(jsonInt(sp["user"]["id"])));
        p.set("isBot", Json::fromBool(s.bot));
        p.set("status", Json::string(status));
        p.set("ts", Json::number(int64_t(e.endMs)));
        p.set("win", Json::fromBool(win));
        p.set("softDefeat", Json::fromBool(false));
        players.push(std::move(p));
        Json st = Json::object();   // PlayerStatistic; BW's 70 per-player values are not produced by the engine sim
        st.set("userId", Json::number(jsonInt(sp["user"]["id"])));
        Json info = Json::object();
        info.set("color", Json::number(jsonInt(sp["info"]["color"])));
        info.set("team", Json::number(int64_t(s.team)));
        st.set("playerInfo", info);
        st.set("winner", Json::fromBool(win));
        st.set("experience", Json::number(int64_t(0)));
        st.set("values", Json::array());
        stats.push(std::move(st));
    }
    rep.set("players", players);
    if (e.started) {
        Json br = Json::object();
        br.set("localStatus", Json::string("None"));
        br.set("matchStatistic", stats);
        br.set("startBattleTime", Json::number(int64_t(e.startMs)));
        br.set("endBattleTime", Json::number(int64_t(e.endMs)));
        rep.set("battleResult", br);
    }
    rep.set("strErrors", Json::string(e.desync ? "nsync engine " + std::to_string(e.endTick) + "\n" : ""));
    rep.set("strStates", Json::string(e.reason));
    Json history = Json::array();
    // Engine replay (base64 of the .rpl file), not BW packets: BW's replay viewer cannot play it.
    if (!replayBase64.empty()) history.push(Json::string(replayBase64));
    rep.set("history", history);
    return rep;
}

Json nodeHeartbeat(const NodeConfig& c, int games, int players, bool draining) {
    Json j = Json::object();
    j.set("nodeId", Json::string(c.nodeId));
    j.set("region", Json::string(c.region));
    j.set("publicHost", Json::string(c.publicHost));
    j.set("tcpPort", Json::number(int64_t(c.clientPort)));
    j.set("tcpPortSsl", Json::number(int64_t(c.clientPortSsl)));
    j.set("internalUrl", Json::string(c.internalUrl));
    j.set("games", Json::number(int64_t(games)));
    j.set("players", Json::number(int64_t(players)));
    j.set("capacity", Json::number(int64_t(c.capacity)));
    j.set("draining", Json::fromBool(draining));
    return j;
}

BattleNode::BattleNode(NodeConfig cfg) : cfg_(std::move(cfg)), http_([this](const HttpRequest& r) { return handle(r); }) {
    clockSec = [] { return int64_t(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()); };
    readReplay = [](const std::string& file) {
        std::ifstream f(file, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        const std::string s = ss.str();
        return s.empty() ? std::string() : enc::base64(s);
    };
}

void BattleNode::install(net::ServerConfig& sc) {
    sc.ticketCheck = [this](const std::string& ticket, std::string& externalId, uint64_t& userId) {
        auto t = validateTicket(ticket, cfg_.ticketSecret, clockSec ? clockSec() : 0);
        if (!t || t->role != 0 || t->userId <= 0) return false;
        externalId = std::to_string(t->gameId);
        userId = uint64_t(t->userId);
        return true;
    };
    sc.onExternalMatchStarted = [this](const std::string& id) {
        auto it = specs_.find(id);
        if (it == specs_.end()) return;
        Json started = Json::object();
        started.set("gameId", Json::number(jsonInt(it->second["gameId"])));
        started.set("gameStartTime", Json::number(int64_t(now_)));
        started.set("spec", it->second);
        if (!cfg_.metaBaseUrl.empty()) post(cfg_.metaBaseUrl + "games/" + id + "/started", started, 1);
        if (!cfg_.matchmakerUrl.empty()) {
            Json ev = Json::object();
            ev.set("gameId", Json::number(jsonInt(it->second["gameId"])));
            ev.set("customId", Json::number(jsonInt(it->second["customId"])));
            ev.set("type", Json::string("started"));
            post(cfg_.matchmakerUrl + "games/" + id + "/events", ev, 1);
        }
    };
    sc.onExternalMatchEnd = [this](const net::ExternalMatchEnd& e) { onEnd(e); };
}

void BattleNode::onEnd(const net::ExternalMatchEnd& e) {
    auto it = specs_.find(e.externalId);
    if (it == specs_.end()) return;
    const Json spec = it->second;
    specs_.erase(it);
    for (auto& s : e.seats) playersHosted_ -= !s.bot;
    if (playersHosted_ < 0) playersHosted_ = 0;
    const std::string replay = e.replayFile.empty() || !readReplay ? std::string() : readReplay(e.replayFile);
    if (!cfg_.metaBaseUrl.empty()) post(cfg_.metaBaseUrl + "games/" + e.externalId + "/result", gameReport(spec, e, replay), 3);
    if (!cfg_.matchmakerUrl.empty()) {
        Json ev = Json::object();
        ev.set("gameId", Json::number(jsonInt(spec["gameId"])));
        ev.set("customId", Json::number(jsonInt(spec["customId"])));
        ev.set("type", Json::string("closed"));
        ev.set("wasStarted", Json::fromBool(e.started));
        ev.set("hasResult", Json::fromBool(e.started && e.result != 0));
        post(cfg_.matchmakerUrl + "games/" + e.externalId + "/events", ev, 1);
    }
}

HttpResponse BattleNode::handle(const HttpRequest& req) {
    HttpResponse res;
    std::string path = req.url.substr(0, req.url.find('?'));
    if (path == "/health") { res.status = 200; res.body = "\"ok\""; return res; }
    if (path.rfind("/internal/", 0) != 0) { res.status = 404; return res; }
    const std::string token = HttpResponse{0, {}, req.headers, false, {}}.header("X-Internal-Token");
    const auto& want = cfg_.internalToken;
    if (want.empty() || !enc::equalBytes(reinterpret_cast<const uint8_t*>(token.data()), token.size(),
                                         reinterpret_cast<const uint8_t*>(want.data()), want.size())) {
        res.status = 401;
        return res;
    }
    if (path == "/internal/status" && req.method == "GET") {
        res.body = nodeHeartbeat(cfg_, int(specs_.size()), playersHosted_, draining_).dump(-1);
        return res;
    }
    if (path == "/internal/drain" && req.method == "POST") { draining_ = true; return res; }
    if (path == "/internal/games" && req.method == "POST") {
        if (draining_) { res.status = 503; res.body = "\"node is draining\""; return res; }
        Json spec;
        try { spec = Json::parse(req.body); } catch (const std::exception&) { res.status = 400; res.body = "\"invalid game spec\""; return res; }
        std::string err;
        auto r = reservationFromSpec(spec, cfg_, &err);
        if (!r || !server_) { res.status = 400; res.body = Json::string("invalid game spec: " + err).dump(-1); return res; }
        const std::string id = r->externalId;
        int humans = 0;
        for (auto& s : r->seats) humans += !s.bot;
        if (specs_.count(id) || !server_->reserve(std::move(*r), &err)) { res.status = 409; res.body = "\"game already exists\""; return res; }
        specs_[id] = spec;
        playersHosted_ += humans;
        res.status = 201;
        Json body = Json::object();
        body.set("id", Json::number(jsonInt(spec["gameId"])));
        const int64_t custom = jsonInt(spec["customId"]);
        body.set("roomId", Json::number(custom > 0 ? custom : jsonInt(spec["gameId"])));
        res.body = body.dump(-1);
        res.headers.push_back({"Location", "/internal/games/" + id});
        return res;
    }
    res.status = 404;
    return res;
}

void BattleNode::post(const std::string& url, const Json& body, int attempts) {
    Outgoing o;
    o.req.method = "POST";
    o.req.url = url;
    o.req.body = body.dump(-1);
    o.req.timeoutMs = 10000;
    o.req.headers = {{"Content-Type", "application/json; charset=utf-8"}, {"X-Internal-Token", cfg_.internalToken}};
    o.attemptsLeft = attempts;
    outbox_.push_back(std::move(o));
}

bool BattleNode::listen(std::string* error) {
    if (http_.listen(cfg_.internalPort)) return true;
    if (error) *error = "cannot bind internal HTTP port " + std::to_string(cfg_.internalPort);
    return false;
}

void BattleNode::update(uint64_t now) {
    now_ = now;
    http_.update(now);
    if (!cfg_.matchmakerUrl.empty() && (lastHeartbeat_ == 0 || now - lastHeartbeat_ >= cfg_.heartbeatMs)) {
        lastHeartbeat_ = now ? now : 1;
        post(cfg_.matchmakerUrl + "nodes/heartbeat", nodeHeartbeat(cfg_, int(specs_.size()), playersHosted_, draining_), 1);
    }
    // One request at a time per node keeps result reports ordered; results retry 3 times like the C# node.
    while (!outbox_.empty() && inFlight_ == 0) {
        Outgoing o = std::move(outbox_.front());
        outbox_.pop_front();
        ++inFlight_;
        auto done = [this, o](HttpResponse r) mutable {
            --inFlight_;
            const bool ok = r.status >= 200 && r.status < 300;
            if (!ok && --o.attemptsLeft > 0) outbox_.push_front(std::move(o));
            else if (!ok) std::fprintf(stderr, "battle node: giving up on %s (%d %s): %s\n", o.req.url.c_str(), r.status, r.error.c_str(), o.req.body.c_str());
        };
        if (transportOverride) transportOverride(o.req, done);
        else client_.request(o.req, done);
    }
    client_.update(now);
}

} // namespace aiverse::meta::bw
#endif
