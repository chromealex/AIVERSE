#include "ExtMeta.h"
#include "entrypoint.grpc.h"
#include "identity.grpc.h"
#include "matchmaking.grpc.h"
#include <chrono>
#include <cstdio>
#include <random>

namespace aiverse::grpc::ext {

namespace ep = ::extraction_rts::entrypoint;
namespace id = ::extraction_rts::identity;
namespace mm = ::extraction_rts::matchmaking;

const char *connectionStatusName(ConnectionStatus s) {
    static const char *n[] = {"none", "connecting", "connected", "versionMismatch", "networkError", "disabled"};
    return n[int(s)];
}
const char *findMatchStatusName(FindMatchStatus s) {
    static const char *n[] = {"none", "fetching", "pinging", "waiting", "found", "noServersReachable", "error", "disabled"};
    return n[int(s)];
}

ClientPlatform currentPlatform() {
#if defined(__ANDROID__)
    return ClientPlatform::Android;
#elif defined(__APPLE__)
    return ClientPlatform::Ios; // the Unity editor (macOS) also reported iOS
#else
    return ClientPlatform::Unspecified;
#endif
}

namespace {
std::string newGuid() {
    std::random_device rd;
    std::mt19937_64 r((uint64_t(rd()) << 32) ^ rd());
    uint8_t b[16];
    for (int i = 0; i < 16; i += 8) {
        const uint64_t v = r();
        for (int k = 0; k < 8; ++k)
            b[i + k] = uint8_t(v >> (8 * k));
    }
    b[6] = uint8_t((b[6] & 0x0f) | 0x40); // version 4
    b[8] = uint8_t((b[8] & 0x3f) | 0x80); // RFC 4122 variant
    char out[37];
    std::snprintf(out, sizeof out, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[0], b[1],
                  b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    return out;
}
} // namespace

struct ExtMetaClient::Impl {
    std::shared_ptr<CallGate> gate = std::make_shared<CallGate>(1);
    std::unique_ptr<Channel> entry, identity, match;
    std::vector<std::unique_ptr<Channel>> graveyard;
    bool polling = false;
    Call serversCall;
    std::shared_ptr<Stream<mm::MatchClientMessage, mm::MatchEvent>> find;
    struct Pings {
        uint64_t attempt = 0;
        size_t pending = 0;
        uint64_t deadline = 0;
        bool done = false;
        std::map<std::string, int32_t> rtt;
    };
    std::shared_ptr<Pings> pings;
    std::string compatKey;

    void bury(std::unique_ptr<Channel> &ch) {
        if (ch)
            graveyard.push_back(std::move(ch));
    }
};

ExtMetaClient::ExtMetaClient(ExtMetaConfig cfg) : cfg_(std::move(cfg)), impl_(std::make_unique<Impl>()) {}
ExtMetaClient::~ExtMetaClient() = default;

int64_t ExtMetaClient::utcNow() const {
    if (cfg_.utcSeconds)
        return cfg_.utcSeconds();
    using namespace std::chrono;
    return int64_t(duration_cast<seconds>(system_clock::now().time_since_epoch()).count());
}

uint64_t ExtMetaClient::now() const {
    if (cfg_.nowMs)
        return cfg_.nowMs();
    using namespace std::chrono;
    return uint64_t(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

std::string ExtMetaClient::compatibilityKey(const std::string &version, const std::string &mapId) {
    return version + "|protocol:14|map:" + mapId;
}

void ExtMetaClient::setStatus(ConnectionStatus s) {
    if (status_ == s)
        return;
    status_ = s;
    if (onStatus)
        onStatus(s);
}

void ExtMetaClient::setFind(FindMatchStatus s) {
    if (findStatus_ == s)
        return;
    findStatus_ = s;
    if (onFindMatch)
        onFindMatch(s);
}

void ExtMetaClient::failConnect(const std::string &why, ConnectionStatus s) {
    lastError_ = why;
    setStatus(s);
}

void ExtMetaClient::disconnect() {
    ++generation_;
    cancelMatchmaking();
    impl_->bury(impl_->entry);
    impl_->bury(impl_->identity);
    impl_->bury(impl_->match);
    if (!impl_->polling)
        impl_->graveyard.clear();
    refreshing_ = false;
    setStatus(ConnectionStatus::None);
}

void ExtMetaClient::connect() {
    disconnect();
    const uint64_t gen = generation_;
    if (!cfg_.enabled) {
        setStatus(ConnectionStatus::Disabled);
        return;
    }
    setStatus(ConnectionStatus::Connecting);
    ChannelConfig cc;
    cc.target = cfg_.endpointUrl;
    cc.protocol = cfg_.protocol;
    cc.tls = cfg_.tls;
    cc.gate = impl_->gate;
    cc.nowMs = cfg_.nowMs;
    impl_->entry = std::make_unique<Channel>(cc);
    ep::GetServiceMapRequest req;
    req.client_version = cfg_.clientVersion;
    req.platform = ep::Platform(int(cfg_.platform));
    CallOptions o;
    o.timeoutMs = cfg_.rpcTimeoutMs;
    ep::EntrypointService::Client(*impl_->entry)
        .GetServiceMap(req, [this, gen](const Status &st, ep::GetServiceMapResponse &&r) {
            if (gen != generation_)
                return;
            if (!st.ok())
                return failConnect("GetServiceMap: " + st.str(), ConnectionStatus::NetworkError);
            if (r.has_incompatible_version())
                return failConnect(r.incompatible_version.error_message, ConnectionStatus::VersionMismatch);
            if (!r.has_success())
                return failConnect("Unknown network error, result type None", ConnectionStatus::NetworkError);
            identityUrl_ = r.success.identity_url;
            matchmakingUrl_ = r.success.matchmaking_url;
            authenticate(gen);
        }, o);
}

void ExtMetaClient::authenticate(uint64_t gen) {
    ChannelConfig cc;
    cc.target = identityUrl_;
    cc.protocol = cfg_.protocol;
    cc.tls = cfg_.tls;
    cc.gate = impl_->gate;
    cc.nowMs = cfg_.nowMs;
    cc.authToken = [this] { return accessToken_; };
    impl_->identity = std::make_unique<Channel>(cc);
    std::string authId = cfg_.load ? cfg_.load("auth_id") : std::string();
    if (authId.empty()) {
        authId = newGuid();
        if (cfg_.save)
            cfg_.save("auth_id", authId);
    }
    id::TokenRequest req;
    req.set_device().device_id = authId;
    CallOptions o;
    o.timeoutMs = cfg_.rpcTimeoutMs;
    id::IdentityService::Client(*impl_->identity)
        .Token(req, [this, gen](const Status &st, id::TokenResponse &&r) {
            if (gen != generation_)
                return;
            if (!st.ok())
                return failConnect("Token: " + st.str(), ConnectionStatus::NetworkError);
            accessToken_ = r.access_token;
            refreshToken_ = r.refresh_token;
            nextRefresh_ = r.expires_at;
            userId_ = r.user_id;
            if (cfg_.save)
                cfg_.save("user_id", r.user_id);
            ChannelConfig mc;
            mc.target = matchmakingUrl_;
            mc.protocol = cfg_.protocol;
            mc.webSocketForUnary = true; // ClientServiceFactory: webSocketMode = true
            mc.tls = cfg_.tls;
            mc.gate = impl_->gate;
            mc.nowMs = cfg_.nowMs;
            mc.authToken = [this] { return accessToken_; };
            impl_->match = std::make_unique<Channel>(mc);
            nextRefreshCheck_ = now() + cfg_.refreshPeriodMs;
            lastError_.clear();
            setStatus(ConnectionStatus::Connected);
        }, o);
}

void ExtMetaClient::refreshToken() {
    if (refreshToken_.empty() || refreshing_ || !impl_->identity)
        return;
    if (nextRefresh_ > utcNow() + cfg_.refreshAheadSeconds)
        return;
    refreshing_ = true;
    const uint64_t gen = generation_;
    id::TokenRequest req;
    req.set_refresh_token().refresh_token = refreshToken_;
    CallOptions o;
    o.timeoutMs = cfg_.rpcTimeoutMs;
    id::IdentityService::Client(*impl_->identity)
        .Token(req, [this, gen](const Status &st, id::TokenResponse &&r) {
            if (gen != generation_)
                return;
            refreshing_ = false;
            if (!st.ok()) {
                lastError_ = "token refresh: " + st.str(); // retried on the next period, like Unity
                return;
            }
            accessToken_ = r.access_token;
            refreshToken_ = r.refresh_token;
            nextRefresh_ = r.expires_at;
        }, o);
}

void ExtMetaClient::logout(std::function<void(const Status &)> done) {
    if (!impl_->identity) {
        if (done)
            done({StatusCode::FailedPrecondition, "not connected", {}});
        return;
    }
    const uint64_t gen = generation_;
    CallOptions o;
    o.timeoutMs = cfg_.rpcTimeoutMs;
    id::IdentityService::Client(*impl_->identity)
        .Logout({}, [this, gen, done = std::move(done)](const Status &st, id::LogoutResponse &&) {
            if (gen == generation_ && st.ok()) {
                accessToken_.clear();
                refreshToken_.clear();
                cancelMatchmaking();
                setStatus(ConnectionStatus::None);
            }
            if (done)
                done(st);
        }, o);
}

void ExtMetaClient::cancelMatchmaking() {
    ++attempt_; // an obsolete attempt never overwrites the current search state
    impl_->serversCall.cancel();
    if (impl_->find) {
        impl_->find->cancel();
        impl_->find.reset();
    }
    impl_->pings.reset();
}

void ExtMetaClient::matchmake(const std::string &mapId, PingFn ping) {
    if (!cfg_.enabled) {
        cancelMatchmaking();
        setFind(FindMatchStatus::Disabled);
        return;
    }
    cancelMatchmaking();
    const uint64_t att = attempt_;
    ticket_.reset();
    playersInQueue_ = 0;
    if (mapId.empty() || !impl_->match || status_ != ConnectionStatus::Connected) {
        lastError_ = mapId.empty() ? "A selected map is required." : "not connected to the meta backend";
        setFind(FindMatchStatus::Error);
        return;
    }
    impl_->compatKey = compatibilityKey(cfg_.clientVersion, mapId);
    setFind(FindMatchStatus::Fetching);
    CallOptions o;
    o.timeoutMs = cfg_.rpcTimeoutMs;
    impl_->serversCall = mm::MatchmakingService::Client(*impl_->match)
        .GetBattleServers({}, [this, att, ping = std::move(ping)](const Status &st, mm::GetBattleServersResponse &&r) {
            if (att != attempt_)
                return;
            if (!st.ok()) {
                lastError_ = "GetBattleServers: " + st.str();
                return setFind(FindMatchStatus::Error);
            }
            servers_.clear();
            for (auto &s : r.servers)
                servers_.push_back({s.server_id, s.address, s.port});
            setFind(FindMatchStatus::Pinging);
            auto state = std::make_shared<Impl::Pings>();
            state->attempt = att;
            state->pending = servers_.size();
            state->deadline = now() + cfg_.pingTimeoutMs;
            impl_->pings = state;
            if (!ping)
                state->pending = 0;
            for (const auto &server : servers_) {
                if (!ping)
                    break;
                std::weak_ptr<Impl::Pings> w = state;
                ping(server, [w, id = server.id](int rtt) {
                    auto s = w.lock();
                    if (!s || s->done)
                        return;
                    if (rtt >= 0)
                        s->rtt[id] = rtt;
                    if (s->pending)
                        --s->pending;
                });
            }
        }, o);
}

void ExtMetaClient::startFindMatch(uint64_t att, std::map<std::string, int32_t> pings) {
    setFind(FindMatchStatus::Waiting);
    StreamHandlers<mm::MatchEvent> h;
    h.onMessage = [this, att](mm::MatchEvent &&e) {
        if (att != attempt_)
            return;
        using Case = mm::MatchEvent::EventCase;
        switch (e.event_case) {
        case Case::queued:
            break;
        case Case::queue_update:
            playersInQueue_ = e.queue_update.players_in_queue;
            if (onQueueUpdate)
                onQueueUpdate(playersInQueue_);
            break;
        case Case::match_found: {
            ticket_ = BattleTicket{e.match_found.match_id, e.match_found.battle_server_address,
                                   e.match_found.battle_server_port, accessToken_, userId_};
            if (impl_->find)
                impl_->find->cancel(); // the Unity client disposes the call once matched
            setFind(FindMatchStatus::Found);
            break;
        }
        case Case::match_cancelled:
            lastError_ = "Match canceled, reason: " + e.match_cancelled.reason;
            if (impl_->find)
                impl_->find->cancel();
            setFind(FindMatchStatus::Error);
            break;
        default:
            break;
        }
    };
    h.onFinish = [this, att](const Status &st) {
        if (att != attempt_ || findStatus_ != FindMatchStatus::Waiting)
            return;
        lastError_ = st.ok() ? "Match stream ended without a match" : "FindMatch: " + st.str();
        setFind(FindMatchStatus::Error);
    };
    impl_->find = mm::MatchmakingService::Client(*impl_->match).FindMatch(std::move(h));
    mm::MatchClientMessage msg;
    auto &p = msg.set_find_match_params();
    p.server_pings = std::move(pings);
    p.client_version = impl_->compatKey;
    impl_->find->write(msg);
}

void ExtMetaClient::poll() {
    if (impl_->polling)
        return;
    impl_->polling = true;
    // Ping phase: all answered or timed out -> FindMatch.
    if (auto p = impl_->pings; p && !p->done && p->attempt == attempt_ && (p->pending == 0 || now() >= p->deadline)) {
        p->done = true;
        impl_->pings.reset();
        if (p->rtt.empty()) {
            lastError_ = "No battle servers reachable";
            setFind(FindMatchStatus::NoServersReachable);
        } else {
            startFindMatch(p->attempt, std::move(p->rtt));
        }
    }
    if (status_ == ConnectionStatus::Connected && now() >= nextRefreshCheck_) {
        nextRefreshCheck_ = now() + cfg_.refreshPeriodMs;
        refreshToken();
    }
    for (auto *ch : {&impl_->entry, &impl_->identity, &impl_->match})
        if (*ch)
            (*ch)->poll();
    impl_->graveyard.clear();
    impl_->polling = false;
}

// ---------------------------------------------------------------------------------------------
void ExtMetaBackend::request(meta::ServiceRequest r, Callback cb) {
    auto reply = [&](meta::ServiceReply rep) { ready_.emplace_back(std::move(rep), std::move(cb)); };
    auto ok = [](Json data) { return meta::ServiceReply{{meta::Code::Ok, {}}, std::move(data)}; };
    if (r.service == "session") {
        if (r.action == "connect") {
            client_.connect();
            connectWaiters_.push_back(std::move(cb));
            return;
        }
        if (r.action == "status") {
            Json d = Json::object();
            d.set("status", Json::string(connectionStatusName(client_.status())));
            d.set("findMatch", Json::string(findMatchStatusName(client_.findMatchStatus())));
            d.set("userId", Json::string(client_.userId()));
            return reply(ok(std::move(d)));
        }
        if (r.action == "logout") {
            auto queue = &ready_;
            client_.logout([queue, cb = std::move(cb)](const Status &st) mutable {
                queue->emplace_back(st.ok() ? meta::ServiceReply{{meta::Code::Ok, {}}, Json::object()}
                                            : meta::transportFailure("logout: " + st.str()),
                                    std::move(cb));
            });
            return;
        }
    } else if (r.service == "matchmaking") {
        if (r.action == "find") {
            client_.matchmake(r.arguments.getStr("mapId"), ping_);
            findWaiters_.push_back(std::move(cb));
            return;
        }
        if (r.action == "cancel") {
            client_.cancelMatchmaking();
            for (auto &w : findWaiters_)
                ready_.emplace_back(meta::ServiceReply{{meta::Code::Unavailable, "matchmaking cancelled"}, Json::object()},
                                    std::move(w));
            findWaiters_.clear();
            return reply(ok(Json::object()));
        }
    }
    reply({{meta::Code::Unsupported, "ext meta: unknown " + r.service + "." + r.action}, Json::object()});
}

void ExtMetaBackend::pump() {
    client_.poll();
    const auto st = client_.status();
    if (!connectWaiters_.empty() && st != ConnectionStatus::Connecting) {
        meta::ServiceReply rep;
        if (st == ConnectionStatus::Connected) {
            Json d = Json::object();
            d.set("status", Json::string(connectionStatusName(st)));
            d.set("userId", Json::string(client_.userId()));
            d.set("identityUrl", Json::string(client_.identityUrl()));
            d.set("matchmakingUrl", Json::string(client_.matchmakingUrl()));
            rep = {{meta::Code::Ok, {}}, std::move(d)};
        } else if (st == ConnectionStatus::VersionMismatch) {
            rep = {{meta::Code::Denied, "version mismatch: " + client_.lastError()}, Json::object()};
        } else if (st == ConnectionStatus::Disabled) {
            rep = {{meta::Code::Unsupported, "backend disabled"}, Json::object()};
        } else {
            rep = meta::transportFailure(client_.lastError());
        }
        for (auto &w : connectWaiters_)
            ready_.emplace_back(rep, std::move(w));
        connectWaiters_.clear();
    }
    const auto fs = client_.findMatchStatus();
    if (!findWaiters_.empty() && (fs == FindMatchStatus::Found || fs == FindMatchStatus::NoServersReachable ||
                                  fs == FindMatchStatus::Error || fs == FindMatchStatus::Disabled)) {
        meta::ServiceReply rep;
        if (fs == FindMatchStatus::Found && client_.ticket()) {
            const auto &t = *client_.ticket();
            Json d = Json::object();
            d.set("matchId", Json::string(t.matchId));
            d.set("host", Json::string(t.host));
            d.set("port", Json::number(int64_t(t.port)));
            d.set("accessToken", Json::string(t.accessToken));
            d.set("userId", Json::string(t.userId));
            rep = {{meta::Code::Ok, {}}, std::move(d)};
        } else if (fs == FindMatchStatus::Disabled) {
            rep = {{meta::Code::Unsupported, "backend disabled"}, Json::object()};
        } else if (fs == FindMatchStatus::NoServersReachable) {
            rep = {{meta::Code::Unavailable, "no battle servers reachable"}, Json::object()};
        } else {
            rep = meta::transportFailure(client_.lastError());
        }
        for (auto &w : findWaiters_)
            ready_.emplace_back(rep, std::move(w));
        findWaiters_.clear();
    }
    auto ready = std::move(ready_);
    ready_.clear();
    for (auto &[rep, cb] : ready)
        if (cb)
            cb(std::move(rep));
}

} // namespace aiverse::grpc::ext
