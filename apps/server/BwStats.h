// Bunker Wars PlayerStatistic (71 values per player, PlayerStatistic.Key) for the battle report the
// BW meta expects (GameReport.battleResult.matchStatistic). Collected by observing a simulation tick by
// tick (events + world), so it changes nothing in the sim; the battle node re-simulates the confirmed
// input of a finished match (like the server verdict) and reads the counters. Uses the match-modes
// counters (RtsModes, rules "matchStats") where the rules turn them on (morale stars, king of the hill).
#pragma once
#include <array>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "engine/net/server/Server.h"
#include "engine/sim/Command.h"
#include "engine/sim/Simulation.h"

namespace aiverse::netgame {

constexpr size_t kBwStatCount = 71;   // PlayerStatistic.Key None..PointsCatchedAll
using BwStatValues = std::array<double, kBwStatCount>;

class BwStatsCollector {
public:
    void begin(Simulation& sim);       // after setup, before the first step
    void afterStep(Simulation& sim);   // after every step
    void finish(Simulation& sim);      // final values (points, wins)
    const std::vector<BwStatValues>& values() const { return v_; }   // [slot], slot 0 = neutral

private:
    struct Arrival { uint8_t attacker = 0, defender = 0; uint64_t lastTick = 0; bool captured = false; };
    void snapshot(Simulation& sim);
    std::vector<BwStatValues> v_;
    std::map<uint64_t, std::pair<uint8_t, uint64_t>> movers_;   // unit -> (owner, order target)
    std::map<uint64_t, std::pair<uint8_t, int>> garrisons_;     // structure -> (owner, count)
    std::map<uint64_t, uint16_t> types_;                       // structure -> type
    std::map<std::pair<uint64_t, uint8_t>, Arrival> attacks_;  // (target, attacker) -> open attack
    std::vector<double> energy_;
    int energyRes_ = -1;
    uint64_t tick_ = 0;
};

struct BwStatsJob {
    std::string gameDir, mapFile;
    uint64_t seed = 0;
    uint32_t tickRate = 20, inputDelay = 2, endTick = 0;
    std::vector<Command> commands;
};
struct BwStatsResult {
    bool ok = false;
    std::string error;
    std::vector<BwStatValues> values;   // [slot]
    int32_t result = 0;
    uint32_t tick = 0;
    uint64_t hash = 0;                  // checksum where the re-simulation stopped (compare with the players')
};
BwStatsResult bwStatsReplay(const BwStatsJob& job);

// For meta::bw::BattleNode::battleStats: re-simulates an external match on the game folder of its
// game id and returns the values per slot. Fails (error) when the data is missing or the
// re-simulation does not reach the players' final checksum.
std::function<std::vector<std::vector<double>>(const net::ExternalMatchEnd&, std::string& error)>
bwBattleStats(std::map<std::string, std::string> gameDirs);

} // namespace aiverse::netgame
