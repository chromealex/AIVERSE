// The engine game server as a Bunker Wars battle node (BunkerWars-Server-CS contract):
//   matchmaker -> node   POST /internal/games {GameSpec}   -> net::Server::reserve()
//                        GET  /internal/status, POST /internal/drain, GET /health
//   node -> matchmaker   POST {matchmakerUrl}nodes/heartbeat {NodeHeartbeat}, games/{id}/events
//   node -> meta         POST {metaBaseUrl}games/{id}/started | result {GameReport}
//   clients              battleTicket (HMAC, shared TicketSecret) as queue "ticket:<ticket>"
// Every internal request carries X-Internal-Token. Battles are the engine's lockstep/rollback
// matches; BW mapIds map to engine map files through the node config.
#pragma once
#ifndef __EMSCRIPTEN__
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>

#include "BwHttp.h"
#include "engine/net/server/Server.h"

namespace aiverse::meta::bw {

struct NodeConfig {
    std::string nodeId = "aiverse-1", region = "local";
    std::string publicHost = "127.0.0.1";    // what clients connect to (battleHost)
    int clientPort = 27015, clientPortSsl = 0; // engine UDP port; WebSocket/TLS port for web clients
    std::string internalUrl = "http://127.0.0.1:8090";
    uint16_t internalPort = 8090;
    int capacity = 200;
    std::string ticketSecret, internalToken;  // shared with the matchmaker / meta (>= 32 bytes secret)
    std::string metaBaseUrl;                  // e.g. http://meta:8080/internal/battle/
    std::string matchmakerUrl;                // e.g. http://matchmaker:8081/internal/
    uint32_t heartbeatMs = 5000;
    std::string gameId;                       // engine game id hosting the battles
    std::map<int, std::string> maps;          // BW mapId -> engine map file ("maps/bw_101.json")
    std::string defaultMap;                   // used when a mapId is not listed ("" = reject)
    uint32_t joinTimeoutSec = 30;
    net::RoomSettings settings;
    // {"nodeId","region","publicHost","clientPort","clientPortSsl","internalUrl","internalPort","capacity",
    //  "ticketSecret","internalToken","metaBaseUrl","matchmakerUrl","heartbeatMs","gameId",
    //  "maps":{"101":"maps/a.json"},"defaultMap","joinTimeoutSec","tickRate","inputDelay","rollbackWindow"}
    static NodeConfig parse(const Json& j);
};

// GameSpec -> engine reservation. Seats keep map order; bots stay bots.
std::optional<net::Reservation> reservationFromSpec(const Json& spec, const NodeConfig& cfg, std::string* error = nullptr);
// The meta's GameReport for a finished (closedFrom RESULT) or cancelled (PROGRESS) match.
Json gameReport(const Json& spec, const net::ExternalMatchEnd& end, const std::string& replayBase64);
Json nodeHeartbeat(const NodeConfig& cfg, int games, int players, bool draining);

class BattleNode {
public:
    explicit BattleNode(NodeConfig cfg);
    // Before constructing the server: installs ticket check and match callbacks into its config.
    void install(net::ServerConfig& sc);
    void attach(net::Server& server) { server_ = &server; }
    bool listen(std::string* error = nullptr);       // internal HTTP API
    void update(uint64_t nowMs);                     // after Server::update
    bool draining() const { return draining_; }
    size_t pendingReports() const { return outbox_.size(); }
    std::function<int64_t()> clockSec;               // unix seconds for ticket expiry (default: system clock)
    std::function<std::string(const std::string& replayFile)> readReplay;   // default: whole file, base64
    // Test hook: outgoing internal requests go here instead of the network.
    std::function<void(const HttpRequest&, std::function<void(HttpResponse)>)> transportOverride;
    HttpResponse handle(const HttpRequest& req);     // internal API (public for tests)

private:
    struct Outgoing { HttpRequest req; int attemptsLeft = 3; };
    void post(const std::string& url, const Json& body, int attempts = 3);
    void onEnd(const net::ExternalMatchEnd& e);
    NodeConfig cfg_;
    net::Server* server_ = nullptr;
    HttpServer http_;
    HttpClient client_;
    std::map<std::string, Json> specs_;              // externalId -> GameSpec (running or waiting)
    int playersHosted_ = 0;
    bool draining_ = false;
    uint64_t now_ = 0, lastHeartbeat_ = 0;
    std::deque<Outgoing> outbox_;
    int inFlight_ = 0;
};

} // namespace aiverse::meta::bw
#endif
