#include "BwMatch.h"

namespace aiverse::meta::bw {

Json MatchParams::toJson() const {
    Json j = Json::object();
    j.set("mapId", Json::number(int64_t(mapId)));
    j.set("roomId", Json::number(roomId));
    j.set("matcher", Json::number(int64_t(matcher)));
    j.set("mode", Json::number(int64_t(mode)));
    j.set("winMode", Json::number(int64_t(winMode)));
    j.set("closedSlots", Json::number(int64_t(closedSlots)));
    j.set("conditions", Json::number(int64_t(conditions)));
    j.set("real", Json::fromBool(real));
    j.set("emulator", Json::fromBool(emulator));
    if (!group.empty()) j.set("group", Json::string(group));
    if (!exact.empty()) j.set("exact", Json::string(exact));
    if (!customInvitedUserId.empty()) j.set("customInvitedUserId", Json::string(customInvitedUserId));
    if (!specialParameters.empty()) j.set("specialParameters", Json::string(specialParameters));
    j.set("withBotOnly", Json::fromBool(withBotOnly));
    j.set("botDifficulty", Json::number(int64_t(botDifficulty)));
    j.set("dailyMode", Json::fromBool(dailyMode));
    return j;
}

std::optional<BattleHandoff> parseHandoff(const Json& r) {
    if (!r.isObject()) return std::nullopt;
    BattleHandoff h;
    h.roomId = jsonInt(r["roomId"]);
    h.mapId = int(jsonInt(r["mapId"]));
    h.matcher = int(jsonInt(r["matcher"]));
    h.mode = int(jsonInt(r["mode"]));
    h.winMode = int(jsonInt(r["winMode"]));
    h.conditions = int(jsonInt(r["conditions"]));
    h.players = int(jsonInt(r["players"]));
    h.battleHost = r.getStr("battleHost");
    h.battleTicket = r.getStr("battleTicket");
    h.battlePort = int(jsonInt(r["battlePort"]));
    h.battlePortSsl = int(jsonInt(r["battlePortSsl"]));
    h.result = r;
    return h;
}

void MatchSession::send(const std::string& func, const std::string& msg, int idx) {
    Bytes b = encodeFrame({func, idx, msg});
    out_.insert(out_.end(), b.begin(), b.end());
}
void MatchSession::connect(const std::string& token) {
    send("Connect", token);
    phase_ = Phase::Connecting;
}
void MatchSession::match(const MatchParams& p) {
    send("Match", p.toJson().dump(-1));
    handoff_.reset();
    phase_ = Phase::Searching;
}
void MatchSession::ready() { send("Match.Ready", "{}"); }
void MatchSession::reconnect(int64_t roomId, int lastTick) {
    Json j = Json::object();
    j.set("roomId", Json::number(roomId));
    j.set("lastTick", Json::number(int64_t(lastTick)));
    send("MatchReconnect", j.dump(-1));
}
void MatchSession::ping(int64_t ms) { send("GamePing", std::to_string(ms)); }

Bytes MatchSession::takeOutgoing() { return std::move(out_); }
std::vector<MatchSession::Event> MatchSession::takeEvents() { return std::move(events_); }

void MatchSession::feed(const uint8_t* p, size_t n) {
    reader_.feed(p, n);
    Frame f;
    while (reader_.next(f)) handle(std::move(f));
}

void MatchSession::closed() {
    if (phase_ == Phase::Closed) return;
    phase_ = Phase::Closed;
    Event e;
    e.kind = Event::Kind::Disconnected;
    events_.push_back(std::move(e));
}

void MatchSession::handle(Frame f) {
    Event e;
    e.frame = f;
    if (!f.msg.empty() && (f.msg[0] == '{' || f.msg[0] == '[')) {
        try { e.json = Json::parse(f.msg); } catch (const std::exception&) {}
    }
    const std::string& fn = f.func;
    if (fn == "Connected") {
        e.kind = Event::Kind::Connected;
        if (phase_ == Phase::Connecting) phase_ = Phase::Online;
    } else if (fn == "Match.onUpdate") {
        e.kind = Event::Kind::Update;
        update_ = e.json;
    } else if (fn == "Match.onReadyToCountdown") {
        e.kind = Event::Kind::Countdown;
        phase_ = Phase::Countdown;
    } else if (fn == "Match.onReady") {
        e.kind = Event::Kind::Ready;
        handoff_ = parseHandoff(e.json);
        phase_ = Phase::Ready;
    } else if (fn == "onSyncComplete") {
        e.kind = Event::Kind::SyncComplete;
        if (e.json.isObject() && e.json.getStr("error").empty() && !e.json.getStr("battleTicket").empty()) {
            auto h = parseHandoff(e.json);
            if (h && handoff_) { h->roomId = handoff_->roomId; h->mapId = handoff_->mapId; }
            handoff_ = h;
        }
        if (e.json.isObject() && !e.json.getStr("error").empty()) e.kind = Event::Kind::Error;
    } else if (fn == "Match.onProgressUpdate" || fn == "Match.onProgressReady") {
        e.kind = Event::Kind::Progress;
    } else if (fn == "Disconnected" || fn == "Int.onDisconnect" || fn == "onClose") {
        e.kind = Event::Kind::Disconnected;
        phase_ = Phase::Closed;
    } else if (fn == "Custom.onJoinFailed" || (e.json.isObject() && e.json["error"].isObject())) {
        e.kind = Event::Kind::Error;
    }
    events_.push_back(std::move(e));
}

} // namespace aiverse::meta::bw
