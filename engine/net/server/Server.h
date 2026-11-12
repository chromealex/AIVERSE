// Game server: lobby rooms, automatic matchmaking, and the input relay for lockstep/rollback
// matches. It never runs the simulation and needs no game data: it confirms each tick's input,
// compares players' checksums, records replays and keeps players' ratings.
//
//   Server srv(loadServerConfig(Json::parseFile("server.json")));
//   srv.listen();                         // UDP + WebSocket ports from the config
//   for (;;) { srv.update(nowMs()); sleep ~1 ms; }
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "../BattleProtocol.h"
#include "../Conn.h"
#include "../MatchTicket.h"
#include "../Protocol.h"
#ifndef __EMSCRIPTEN__
#include "../SecureUdp.h"
#endif

namespace aiverse { class Json; }

namespace aiverse::net {

struct QueueConfig {
    std::string id = "1v1", title = "@net.queue.default";   // shown to players: "@key" or literal text
    uint8_t teams = 2, teamSize = 1;
    RoomSettings settings;
    uint32_t botFillSec = 0;           // fill missing players with bots after this wait (0 = never)
    uint32_t ratingRange = 150;        // initial accepted rating difference
    uint32_t ratingWidenPerSec = 10;   // the range grows while players wait
};

// Deciding a match the players did not settle (everyone left, results disagree): the server
// re-simulates the confirmed input on its own copy of the game data. The server library itself
// knows no game; the application provides this (apps/server links the RTS module).
struct ArbiterJob {
    std::string gameId, mapFile;
    uint64_t dataHash = 0, seed = 0;
    uint32_t tickRate = 20, inputDelay = 2;
    uint32_t endTick = 0;                                  // re-simulate at most to here
    std::vector<Command> commands;                         // confirmed input
    std::vector<std::pair<uint32_t, uint64_t>> checkpoints; // checksums the players agreed on
};
struct ArbiterResult {
    bool ok = false;           // false: no data for this game / data hash differs / error
    int32_t result = 0;        // winning team, -1 draw, 0 still undecided at endTick
    uint32_t tick = 0;         // tick the result appeared (or endTick)
    uint64_t hash = 0;         // checksum there
    uint32_t mismatchTick = 0; // first agreed checkpoint the server's simulation disagrees with (0 = none)
    std::string error;
};
using Arbiter = std::function<ArbiterResult(const ArbiterJob&)>;

// ---- hosted battles (dedicated battle server mode, docs/battle-net.md)
// An orchestrator (matchmaker) allocates a match with everything it decided: players, loadouts,
// map, seed, compatibility key. Players connect with tickets (MatchTicket.h) over the secure
// transport; the server seats them by the ticket's player id and slot, never by what they send.
struct MatchAllocation {
    uint64_t matchId = 0;
    std::string gameId, compatKey, mapId;
    MapInfo map;                         // file + dataHash + slot teams, as describeMap() gives them
    RoomSettings settings;               // mobile defaults: battleSettings()
    uint64_t seed = 0;                   // 0 = the server picks one
    struct Player { std::string playerId; BattlePlayer info; };
    std::vector<Player> players;         // humans (playerId set) and bots
    Bytes gameData;
    uint32_t joinTimeoutMs = 30000;      // start without missing players after this (they may join late)
    uint32_t maxDurationSec = 3600;      // hard stop: the server re-simulates / ends the match
    std::vector<uint16_t> allowedCommands;   // empty = any; others are dropped (and counted)
    uint32_t maxCommandsPerTick = 16;    // per player
};

// Settings tuned for 10-20% loss / 150 ms RTT / jitter (see docs/battle-net.md for the numbers).
RoomSettings battleSettings(uint32_t tickRate = 20);

struct MatchReport {
    uint64_t matchId = 0;
    int32_t result = 0;                  // winning team, -1 draw, 0 none (aborted / no-show / time limit)
    NetText reason;
    uint32_t endTick = 0;
    uint64_t endHash = 0, durationMs = 0;
    bool verified = false;               // decided by the server's own re-simulation (arbiter)
    bool desync = false, aborted = false;
    struct Player {
        std::string playerId;
        uint8_t slot = 0, team = 0;
        bool joined = false, left = false, connectedAtEnd = false;
        uint32_t droppedCommands = 0;    // filtered by allowedCommands / maxCommandsPerTick
        Bytes report;                    // the player's ResultReport blob (empty = none)
    };
    std::vector<Player> players;
    Bytes report;                        // the blob every reporting player agreed on
    bool consensus = false;              // at least one report and all reports identical
    std::string replayPath;
};

struct HostedMatchInfo {
    uint64_t matchId = 0;
    uint8_t phase = 0;                   // 0 waiting for players, 1 running, 2 finished
    uint32_t joined = 0, expected = 0, tick = 0;
    uint64_t ageMs = 0;
};

struct ServerConfig {
    std::string name = "AIVERSE server";
    uint16_t udpPort = kDefaultUdpPort;   // 0 = disabled
    uint16_t wsPort = kDefaultWsPort;     // 0 = disabled
    std::string replayDir;                // empty = do not save replays
    std::string ratingsFile;              // empty = ratings live in memory only
    std::map<std::string, std::string> gameDirs; // game id -> folder, so saved replays point at real files
    std::vector<QueueConfig> queues;
    uint32_t maxClients = 5000;
    uint32_t reconnectGraceSec = 120;     // a dropped player keeps the slot this long
    // Command type that makes the issuing player give up (kCmdSurrender of the RTS module). A player
    // who leaves (or does not come back in time) surrenders through the normal input stream, so the
    // simulation decides the match and the replay shows it. 0 = the server decides by itself.
    uint16_t surrenderCommand = 0;
    Arbiter arbiter;                      // empty = matches nobody settled end without a winner
    bool log = true;
    // Battle server mode: only ticket-authenticated players (addSecureConnection) may say Hello;
    // lobby and queues are off for everyone else.
    bool hostedOnly = false;
    std::function<void(const MatchReport&)> onMatchReport;   // results hook for meta (also queued: takeReports())
};

// Parse a server config (all keys optional):
// {"name": "...", "udpPort": 27015, "wsPort": 27016, "replays": "replays", "ratings": "ratings.txt",
//  "games": {"com.aiverse.ironandgold": "templates/skirmish"}, "reconnectGraceSec": 120,
//  "queues": [{"id": "1v1", "title": "1 на 1", "teams": 2, "teamSize": 1, "botFillSec": 60,
//              "ratingRange": 150, "ratingWidenPerSec": 10,
//              "tickRate": 20, "inputDelay": 2, "rollbackWindow": 8, "lagToleranceMs": 300}]}
ServerConfig loadServerConfig(const Json& j);

struct ServerStats {
    uint32_t clients = 0, rooms = 0, activeMatches = 0, queued = 0;
    uint32_t matchesStarted = 0, matchesFinished = 0, desyncs = 0, verdicts = 0;
};

class Server {
public:
    explicit Server(ServerConfig cfg);
    ~Server();
    bool listen(std::string* error = nullptr);
    uint16_t udpPort() const;
    uint16_t wsPort() const;
    void addConnection(std::unique_ptr<Conn> c);   // in-process clients (tests, local play)
    void update(uint64_t now);
    ServerStats stats() const;
    bool busy() const;                             // a match verdict is still being computed
    int rating(const std::string& queueId, uint64_t clientKey) const;

    // ---- hosted battles
    bool allocateMatch(const MatchAllocation& a, std::string* error = nullptr);
    bool releaseMatch(uint64_t matchId);                       // aborts a waiting or running match
    // Would this ticket be seated? (SecureListener's admit callback.)
    bool admits(const TicketClaims& claims) const;
    // A connection whose identity was proven by a ticket (SecureListener). Claims are trusted.
    void addSecureConnection(std::unique_ptr<Conn> c, const TicketClaims& claims);
    std::vector<HostedMatchInfo> hostedMatches() const;
    std::vector<MatchReport> takeReports();                    // finished hosted matches since the last call
#ifndef __EMSCRIPTEN__
    // Secure battle transport: ticket holders connect here; tickets are admitted by admits().
    bool listenSecure(uint16_t port, const Key& serverKey, bool loopbackOnly = false, std::string* error = nullptr);
    void attachSecure(std::unique_ptr<DatagramPort> port, const Key& serverKey, SecureLinkConfig link = {});  // SimNet in tests
    uint16_t securePort() const;
    SecureStats secureStats() const;
    void setTicketClock(std::function<uint64_t()> clock);      // tests: fake unix clock for ticket expiry
#endif

    struct Impl;
private:
    std::unique_ptr<Impl> impl_;
};

} // namespace aiverse::net
