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
#include "../Conn.h"
#include "../Protocol.h"

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

// Matches placed by an external matchmaker (a meta backend such as the Bunker Wars servers, see
// docs/meta-bunkerwars.md): the seats are known in advance and each player proves its seat with a
// ticket, sent as the queue id "ticket:<ticket>" (QueueJoin with the client's maps, so the data hash
// is still checked). The match starts when every human seat is taken.
struct ReservedSeat {
    uint8_t team = 0;            // informational; the map's slot teams decide the simulation
    bool bot = false;
    std::string name;
    uint64_t userId = 0;         // backend user id the ticket must name
};
struct Reservation {
    std::string externalId;      // backend game id (opaque to the server)
    std::string gameId;          // engine game id (game.json "id")
    std::string mapFile;         // relative to the game folder; players must have it with one data hash
    RoomSettings settings;
    std::vector<ReservedSeat> seats;   // map slot order (slot 1 first)
    uint32_t joinTimeoutSec = 30;
    bool fillMissingWithBots = false;  // false: an untaken seat cancels the match at the timeout
};
struct ExternalSeatResult { uint8_t slot = 0, team = 0; uint64_t userId = 0; bool bot = false, joined = false; };
struct ExternalMatchEnd {
    std::string externalId;
    bool started = false;        // false: cancelled before start (seats not taken in time)
    int32_t result = 0;          // winning team, -1 draw, 0 undecided
    uint32_t endTick = 0;
    uint64_t endHash = 0, startMs = 0, endMs = 0;
    bool verified = false, desync = false;
    std::vector<ExternalSeatResult> seats;
    std::string replayFile;      // saved replay ("" if replays are off)
    std::string reason;          // why it was cancelled
};
// ticket -> seat owner. Returns false for an invalid/expired ticket.
using TicketCheck = std::function<bool(const std::string& ticket, std::string& externalId, uint64_t& userId)>;

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
    // External matchmaking (Server::reserve). Callbacks run inside update() on the server thread.
    TicketCheck ticketCheck;
    std::function<void(const std::string& externalId)> onExternalMatchStarted;
    std::function<void(const ExternalMatchEnd&)> onExternalMatchEnd;
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
    // Host a match an external matchmaker formed (see Reservation). False if the id is taken or the
    // reservation is malformed.
    bool reserve(Reservation r, std::string* error = nullptr);
    size_t reservations() const;                   // waiting for players

    struct Impl;
private:
    std::unique_ptr<Impl> impl_;
};

} // namespace aiverse::net
