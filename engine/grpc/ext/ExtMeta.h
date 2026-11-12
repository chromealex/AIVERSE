// Drone Fall ("extraction_rts") meta backend client: a C++ port of the Unity client's
// MetaNetworkManager + MatchmakingManager + ClientServiceFactory/CallsThrottle, talking to the
// EXISTING backend through the protos in ext/proto (copied verbatim from ExtInterop/Proto).
//
//   connect():   EntrypointService.GetServiceMap(client_version, platform)
//                  -> IncompatibleVersion => VersionMismatch, Success => identity/matchmaking URLs
//                IdentityService.Token(device grant: persisted "auth_id" GUID) -> access/refresh
//                token, "user_id" saved; refresh grant every 10 s once expiry is < 60 s away.
//   matchmake(): MatchmakingService.GetBattleServers -> host pings every server (PingFn; the
//                LiteNetLib 0x50 unconnected ping belongs to the battle transport) -> FindMatch
//                bidi stream with FindMatchParams{server_pings, client_version = compat key}
//                -> Queued / QueueUpdate / MatchFound (BattleTicket) / MatchCancelled.
// Channels follow ClientServiceFactory: entrypoint + identity unary over gRPC-Web (HTTP/1.1),
// matchmaking with webSocketForUnary (everything over the cs-grpc-bridge-v1 WebSocket), auth
// "Bearer <access token>", one shared CallGate (CallsThrottle is static in Unity).
//
// Threading: everything on the owning thread; callbacks fire inside poll(). Not simulation code.
#pragma once
#include "engine/grpc/Grpc.h"
#include "engine/meta/Protocol.h"
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace aiverse::grpc::ext {

enum class ConnectionStatus { None, Connecting, Connected, VersionMismatch, NetworkError, Disabled };
enum class FindMatchStatus { None, Fetching, Pinging, Waiting, Found, NoServersReachable, Error, Disabled };
const char *connectionStatusName(ConnectionStatus s);
const char *findMatchStatusName(FindMatchStatus s);

// Values of extraction_rts.entrypoint.Platform.
enum class ClientPlatform { Unspecified = 0, Ios = 1, Android = 2, Steam = 3 };
ClientPlatform currentPlatform(); // the Unity editor reported Ios; here: by compile target

struct BattleServer {
    std::string id, address;
    int port = 0;
};

// Hand-off to the battle transport (agent-net-mobile): connect UDP to host:port and send
// extraction_rts.ConnectRequest{client_uid, room_id = matchId, access_token} (messages.proto).
struct BattleTicket {
    std::string matchId, host;
    int port = 0;
    std::string accessToken, userId;
};

struct ExtMetaConfig {
    bool enabled = true;          // GameplayNetworkConfig.backendEnabled
    std::string endpointUrl;      // GameplayNetworkConfig.endpointUri
    std::string clientVersion;    // Application.version
    ClientPlatform platform = currentPlatform();
    Protocol protocol = Protocol::GrpcWeb; // the backend serves gRPC-Web + WebSocket bridge
    TlsConfig tls;
    uint32_t rpcTimeoutMs = 30000; // Unity used no deadline; bounded here (0 = none)
    uint32_t refreshPeriodMs = 10000;
    int64_t refreshAheadSeconds = 60;
    uint32_t pingTimeoutMs = 11000; // a server whose ping never answers counts as unreachable
    // Profile storage ("auth_id", "user_id"); empty string = missing.
    std::function<std::string(const std::string &key)> load;
    std::function<void(const std::string &key, const std::string &value)> save;
    std::function<int64_t()> utcSeconds; // default: system clock
    std::function<uint64_t()> nowMs;     // default: steady clock
};

// Host-provided reachability probe: call done(rttMs) once, rttMs < 0 = unreachable.
using PingFn = std::function<void(const BattleServer &server, std::function<void(int rttMs)> done)>;

class ExtMetaClient {
  public:
    explicit ExtMetaClient(ExtMetaConfig cfg);
    ~ExtMetaClient();

    void connect();    // restarts the whole session (like MetaNetworkManager.Connect)
    void disconnect(); // drops channels, cancels everything (Unload)
    void logout(std::function<void(const Status &)> done = {});
    // "version|protocol:14|map:<mapId>" (MatchmakingManager.GetCompatibilityKey).
    static std::string compatibilityKey(const std::string &version, const std::string &mapId);
    void matchmake(const std::string &mapId, PingFn ping);
    void cancelMatchmaking();
    void poll();

    ConnectionStatus status() const { return status_; }
    FindMatchStatus findMatchStatus() const { return findStatus_; }
    const std::optional<BattleTicket> &ticket() const { return ticket_; }
    const std::vector<BattleServer> &battleServers() const { return servers_; }
    const std::string &identityUrl() const { return identityUrl_; }
    const std::string &matchmakingUrl() const { return matchmakingUrl_; }
    const std::string &accessToken() const { return accessToken_; }
    const std::string &userId() const { return userId_; }
    int64_t tokenExpiresAt() const { return nextRefresh_; }
    const std::string &lastError() const { return lastError_; }
    int playersInQueue() const { return playersInQueue_; }

    std::function<void(ConnectionStatus)> onStatus;
    std::function<void(FindMatchStatus)> onFindMatch;
    std::function<void(int playersInQueue)> onQueueUpdate;

  private:
    struct Impl;
    void setStatus(ConnectionStatus s);
    void setFind(FindMatchStatus s);
    void failConnect(const std::string &why, ConnectionStatus s);
    void authenticate(uint64_t gen);
    void refreshToken();
    void startFindMatch(uint64_t attempt, std::map<std::string, int32_t> pings);
    int64_t utcNow() const;
    uint64_t now() const;

    ExtMetaConfig cfg_;
    std::unique_ptr<Impl> impl_;
    ConnectionStatus status_ = ConnectionStatus::None;
    FindMatchStatus findStatus_ = FindMatchStatus::None;
    std::optional<BattleTicket> ticket_;
    std::vector<BattleServer> servers_;
    std::string identityUrl_, matchmakingUrl_, accessToken_, refreshToken_, userId_, lastError_;
    int64_t nextRefresh_ = 0;
    uint64_t nextRefreshCheck_ = 0;
    bool refreshing_ = false;
    int playersInQueue_ = 0;
    uint64_t generation_ = 0; // connect() epoch
    uint64_t attempt_ = 0;    // matchmaking attempt
};

// engine/meta Backend over ExtMetaClient, for hosts that drive online features through
// aiverse::meta (callbacks during pump(), owning thread). JSON protocol:
//   service "session":     "connect" -> {status, userId, identityUrl, matchmakingUrl}
//                          "status"  -> {status, userId, findMatch}
//                          "logout"  -> {}
//   service "matchmaking": "find" {mapId} -> {matchId, host, port, accessToken, userId}
//                          (reply when the attempt ends; progress via ExtMetaClient callbacks)
//                          "cancel" -> {}
// A failed / version-mismatched connect replies Unavailable / Denied ("version mismatch").
class ExtMetaBackend final : public meta::Backend {
  public:
    ExtMetaBackend(ExtMetaClient &client, PingFn ping) : client_(client), ping_(std::move(ping)) {}
    void request(meta::ServiceRequest request, Callback callback) override;
    void pump() override;

  private:
    ExtMetaClient &client_;
    PingFn ping_;
    std::vector<Callback> connectWaiters_, findWaiters_;
    std::vector<std::pair<meta::ServiceReply, Callback>> ready_;
};

} // namespace aiverse::grpc::ext
