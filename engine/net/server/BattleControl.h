// Control plane of a battle server process: an orchestrator (matchmaker) allocates and releases
// matches, reads status / capacity and collects results. One JSON request per line over TCP
// (private network or loopback), each line "<mac> <json>" with mac = keyed BLAKE2b of the JSON under
// the control key, plus a timestamp and a nonce against replays. Responses are signed the same way.
// The channel carries no secrets: tickets are issued by the matchmaker itself (MatchTicket.h).
//
//   {"op":"allocate","ts":<unix ms>,"nonce":"r1","match":{"matchId":"42","gameId":"...","compatKey":"...",
//     "mapId":"square","map":"maps/square.json" | {"file":..,"dataHash":"<hex>","slotTeams":[1,2]},
//     "seed":"7","tickRate":20,"joinTimeoutMs":30000,"maxDurationSec":1800,"gameData":"<hex>",
//     "allowedCommands":[61696,61697],"maxCommandsPerTick":16,
//     "players":[{"playerId":"u1","slot":1,"team":1,"name":"Alex","loadout":"<hex>"},{"slot":2,"team":2,"bot":true}]}}
//   {"op":"release","matchId":"42"}   {"op":"status"}   {"op":"reports"}   (each with ts + nonce)
#pragma once
#include <deque>
#include <memory>
#include <set>
#include <string>
#include "Server.h"
#include "../../data/Json.h"

namespace aiverse::net {

struct BattleControlConfig {
    Key controlKey{};
    uint32_t maxMatches = 64;            // capacity of this process (allocate fails beyond)
    std::string publicAddress;           // "host:port" players connect to (returned by allocate)
    uint32_t clockSkewMs = 30000;
    std::function<uint64_t()> clock = unixMs;
};

// JSON <-> engine structs (also used by the aiverse_battle tool). gameDirs resolves "map" given as a
// file name (describeMap); without it the map must be an object with file / dataHash / slotTeams.
bool allocationFromJson(const Json& j, const std::map<std::string, std::string>& gameDirs, MatchAllocation& out, std::string& error);
Json reportToJson(const MatchReport& r);
Json hostedToJson(const HostedMatchInfo& h);

std::string signControl(const Key& key, const std::string& json);           // "<mac> <json>"
bool verifyControl(const Key& key, const std::string& line, std::string& json);

class BattleControl {
public:
    BattleControl(Server& server, BattleControlConfig cfg, std::map<std::string, std::string> gameDirs = {});
    ~BattleControl();
#ifndef __EMSCRIPTEN__
    bool listen(uint16_t port, bool loopbackOnly, std::string* error = nullptr);
    uint16_t port() const;
#endif
    void update(uint64_t now);
    // One signed request line -> one signed response line (also usable in-process without TCP).
    std::string handleLine(const std::string& line);
    uint32_t running() const;

private:
    Json handle(const Json& req);
    Server& server_;
    BattleControlConfig cfg_;
    std::map<std::string, std::string> gameDirs_;
    std::set<std::string> nonces_;
    std::deque<std::string> nonceOrder_;
    struct Net;
    std::unique_ptr<Net> net_;
};

} // namespace aiverse::net
