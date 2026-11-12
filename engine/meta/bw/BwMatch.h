// Matchmaking handoff with the Bunker Wars servers. Meta and matchmaking stay on their backend;
// the battle itself runs on the engine net stack (engine/net, apps/server):
//
//   1. client: v/game/choose (meta HTTP) -> matchmaker host; TCP frames Connect(meta token) + Match(params)
//   2. matchmaker: Match.onUpdate... Match.onReady {battleHost, battlePort, battleTicket, ...}
//      (BunkerWars-Server-CS; the Java server without these fields cannot hand off)
//   3. client: connects the engine NetClient to battleHost:battlePort and joins the queue
//      "ticket:<battleTicket>" with its maps (BattleHandoff::queueId)
//   4. node: the matchmaker's POST /internal/games {GameSpec} became Server::reserve() (BwNode.h);
//      at the end the node posts GameReport to the meta /internal/battle/games/{id}/result.
//
// MatchSession is transport-agnostic: feed() received bytes, send takeOutgoing(), read events.
#pragma once
#include <optional>
#include <string>
#include <vector>

#include "BwWire.h"

namespace aiverse::meta::bw {

struct MatchParams {   // MultiplayerManager.Matching.Parameters
    int mapId = 0;
    int64_t roomId = 0;
    int matcher = 0;   // 0 Rating, 1 Custom, 3 Unranked, 4 Unranked2x2, 6..8 FFA, 9 Domination, 10 KingOfTheHill, 13 BattleRoyal
    int mode = 0;      // 0x1 FFA, 0x2 COOP
    int winMode = 0;   // 0x1 Conquest, 0x2 Domination, 0x4 KingOfTheHill, 0x8 TimeMod
    int closedSlots = 0, conditions = 0, botDifficulty = 0;
    bool real = true, emulator = false, withBotOnly = false, dailyMode = false;
    std::string group, exact, customInvitedUserId, specialParameters;
    Json toJson() const;
};

struct BattleHandoff {   // Matching.Result as delivered by Match.onReady / onSyncComplete
    int64_t roomId = 0;
    int mapId = 0, matcher = 0, mode = 0, winMode = 0, conditions = 0, players = 0;
    std::string battleHost, battleTicket;
    int battlePort = 0, battlePortSsl = 0;
    Json result;         // the whole payload (connectedPlayers, customGameConfig, ...)
    bool spectator = false;   // from onSpectateJoin: watch only (spectator ticket)
    bool hasNode() const { return !battleHost.empty() && !battleTicket.empty() && battlePort > 0; }
    std::string address() const { return battleHost + ":" + std::to_string(battlePort); }   // net::connectTo
    std::string queueId() const { return "ticket:" + battleTicket; }                        // NetClient::joinQueue
};
std::optional<BattleHandoff> parseHandoff(const Json& matchingResult);

class MatchSession {
public:
    enum class Phase { Idle, Connecting, Online, Searching, Countdown, Ready, Closed };
    struct Event {
        enum class Kind { Connected, Update, Countdown, Ready, SyncComplete, SpectateJoin, Progress, Pong, Error, Disconnected, Other } kind = Kind::Other;
        Frame frame;
        Json json;   // parsed msg (null when not JSON)
    };

    void connect(const std::string& tokenOrTicket);    // "Connect" with the meta token (nothing if empty, as MsgConnect)
    void match(const MatchParams& p);                  // "Match"
    void ready();                                      // "Match.Ready" (after onReadyToCountdown)
    void reconnect(int64_t roomId, int lastTick);      // "MatchReconnect"
    void spectate(int64_t roomId, bool customRoom);    // "Spectate" {roomId, custom} -> onSpectateJoin
    void ping(int64_t ms, int tick);                   // "GamePing" {"ms":…,"tick":…} (MultiplayerManager.Ping)
    void pingRaw(int64_t ms);                          // "GamePing" "<ms>" (TcpConnector.MsgPing)
    void send(const std::string& func, const std::string& msg, int idx = 0);

    void feed(const uint8_t* p, size_t n);
    void feed(const Bytes& b) { feed(b.data(), b.size()); }
    void closed();                                     // the transport dropped
    Bytes takeOutgoing();
    std::vector<Event> takeEvents();

    Phase phase() const { return phase_; }
    const std::optional<BattleHandoff>& handoff() const { return handoff_; }
    const Json& lastUpdate() const { return update_; }

private:
    void handle(Frame f);
    Phase phase_ = Phase::Idle;
    FrameReader reader_;
    Bytes out_;
    std::vector<Event> events_;
    std::optional<BattleHandoff> handoff_;
    Json update_;
};

} // namespace aiverse::meta::bw
