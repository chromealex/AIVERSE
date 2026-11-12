// Headless battle load / soak run: one battle server process with N hosted HeroDuel matches,
// every player a bot client with its own simulation, secure transport over the simulated mobile
// network (loss, bursts, latency, jitter, NAT rebinding, app suspend), all on a virtual clock.
// Used by the tests (small), the aiverse_battle tool (`aiverse_battle load ...`) and the MCP op.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "engine/net/SimNet.h"
#include "engine/net/server/Server.h"

namespace aiverse::battle {

struct BattleLoadConfig {
    uint32_t matches = 4, playersPerMatch = 2;
    uint32_t seconds = 20;                 // match length (the match ends by time)
    net::LinkProfile link = net::linkPreset("bad");   // every client's radio link
    uint32_t tickRate = 20;
    uint64_t seed = 1;
    uint32_t stepMs = 10;                  // virtual frame time of clients and server
    bool suspendOne = false;               // first player of match 1 backgrounds the app mid-match
    uint32_t suspendMs = 6000;
    bool rebindOne = false;                // second player of match 1 changes address (NAT / handover)
    bool desyncOne = false;                // test only: one client's simulation is corrupted
    net::RoomSettings settings{};          // tickRate 0 = net::battleSettings(tickRate)
    bool customSettings = false;
};

struct BattleLoadResult {
    uint32_t matches = 0, finished = 0, consensus = 0, aborted = 0, desyncs = 0, brokenSessions = 0;
    uint64_t rollbacks = 0, resimulatedTicks = 0, predictedTicks = 0, stalls = 0;
    uint32_t maxRollback = 0;
    double aheadAvg = 0;                   // ticks the client ran ahead of confirmation (average)
    uint32_t aheadMax = 0;
    double serverUpdateP50Ms = 0, serverUpdateP99Ms = 0, serverUpdateMaxMs = 0;
    double serverCpuShare = 0;             // server CPU time / simulated wall time
    double upBytesPerSec = 0, downBytesPerSec = 0;   // per player, server side incl. headers
    uint64_t handshakes = 0, migrations = 0, reconnects = 0, rejected = 0;
    double wallMs = 0;
    std::vector<net::MatchReport> reports;
    std::string summary() const;
};

BattleLoadResult runBattleLoad(const BattleLoadConfig& cfg);

} // namespace aiverse::battle
