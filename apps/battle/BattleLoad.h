// Headless battle load / soak run: one battle server process with N hosted matches (HeroDuel or an RTS game),
// every player a bot client with its own simulation, secure transport over the simulated mobile
// network (loss, bursts, latency, jitter, NAT rebinding, app suspend), all on a virtual clock.
// Used by the tests (small), the aiverse_battle tool (`aiverse_battle load ...`) and the MCP op.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "engine/core/Random.h"
#include "engine/net/BattleProtocol.h"
#include "engine/net/HeroInput.h"
#include "engine/net/SimNet.h"
#include "engine/net/server/Server.h"
#include "engine/sim/Simulation.h"

namespace aiverse::battle {

// A bot's virtual thumbs (state a BattleGame keeps per player between frames).
struct BotThumbs {
    Random rng;
    float mx = 0, mz = 0;                  // wander direction
    uint64_t nextTurn = 0, nextInput = 0, nextAbility = 0;
    net::HeroInputQuantizer quant;
};

// The game a load run hosts. Every game speaks the engine's hero commands (net::kCmdHeroInput / kCmdHeroAbility),
// which is what the allocation's command filter allows by default.
class BattleGame {
public:
    virtual ~BattleGame() = default;
    virtual std::string id() const = 0;                                  // map id (compatibility key, allocation)
    virtual std::string mapJson(uint32_t players, uint32_t ticks) const = 0;   // the map every client builds (hashed)
    virtual std::vector<uint16_t> commands() const { return {net::kCmdHeroInput, net::kCmdHeroAbility}; }
    virtual std::vector<uint8_t> loadout(uint32_t player) const { (void)player; return {}; }
    // A client's simulation from the hosted start payload (setup done).
    virtual std::unique_ptr<Simulation> create(const SimConfig& cfg, const net::MatchPayloadMsg* payload, const std::string& mapJson) = 0;
    virtual std::vector<uint8_t> resultBlob(Simulation& sim) = 0;        // deterministic, frozen once decided
    // Bot thumbs for one frame: commands to submit (player slot set by the caller).
    virtual void bot(Simulation& sim, uint8_t slot, BotThumbs& t, uint64_t nowMs, std::vector<Command>& out) = 0;
    // Presentation: positions of the objects a rollback may move (heroes ...), keyed by a stable id.
    virtual void heroPositions(Simulation& sim, std::map<uint64_t, std::pair<float, float>>& out) = 0;
    virtual void corrupt(Simulation& sim, uint8_t slot) = 0;             // tests: change confirmed state (forced desync)
};
std::unique_ptr<BattleGame> heroDuelGame();                              // the default: apps/battle/HeroDuel.h

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
    std::shared_ptr<BattleGame> game;      // null: HeroDuel
    float smoothingHalfLifeMs = 50;        // rollback corrections of heroes go through net::CorrectionSmoother
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
    // rollback hooks (RollbackSession::setRollbackHook) feeding the correction smoother, over all clients
    uint64_t corrections = 0, snaps = 0;   // hero corrections blended / too large to blend (snapped)
    float maxCorrection = 0;               // world units
    float maxRawJump = 0, maxDrawnJump = 0; // largest per-frame jump of a hero: sim position / drawn (smoothed), world units
    std::vector<net::MatchReport> reports;
    std::string summary() const;
};

BattleLoadResult runBattleLoad(const BattleLoadConfig& cfg);

} // namespace aiverse::battle
