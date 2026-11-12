// Extraction raids at run time (data: RtsRaidData.h, reference: docs/raid.md).
//
// State: the RaidStore world resource (present only in games with a rules "raid" block, so other games keep
// their checksums and saves; rollback snapshots copy it like any other resource). Carried items stay in the
// raider's ItemBag (rules "items", type "inventory"), ground loot in MechState::groundItems: picking up,
// dropping and dropping everything on death are the existing item mechanics; the raid adds what is secured,
// carried resources, channels, interactables, objectives, the match phases and the receipts.
//
// The "raid" system runs right before "death": raiders at 0 hp are still there, so a death is recorded with its
// killer and "loseOnDeath": "destroy" can empty the bag before the item drop. Everything iterates in ECS order
// or sorted vectors; random loot uses sim.rng() in that order.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "RtsRaidData.h"
#include "../../engine/core/ECS.h"
#include "../../engine/core/Fixed.h"

namespace aiverse { class Simulation; class Hasher; class Archive; struct Command; }

namespace aiverse::game {

class RtsModule;

enum class RaidPhase : uint8_t { Briefing = 0, Running = 1, Over = 2 };
enum class RaidPointPhase : uint8_t { Locked = 0, Sleep = 1, Inbound = 2, Open = 3, Departing = 4 };
enum class RaidStatus : uint8_t { Waiting = 0, InRaid = 1, Extracted = 2, Dead = 3, TimedOut = 4, Abandoned = 5 };
// sub-kinds of kEvRaidChannel / kEvRaidInteract (value >> 16)
enum class RaidChannelEv : uint8_t { Start = 0, Interrupted = 1, Done = 2 };
// raid commands: kCmdRaid param = op | point index << 8 (call: -1 << 8 = the nearest callable point)
enum RaidOp : int64_t { kRaidCall = 1, kRaidAbandon = 2 };

struct RaidPointState {
    uint8_t phase = 0;            // RaidPointPhase
    uint32_t until = 0;           // tick the phase ends (0: until something happens)
    Fixed fill{};                 // scope "point": shared progress (seconds)
    int used = 0;                 // extractions in this window
    uint8_t caller = 0;           // call points: who called it
    Fixed x{}, z{};               // current centre (follow points move with the caller's raider)
};
struct RaidChannel { uint8_t player = 0; uint16_t point = 0; Fixed progress{}; };
struct RaidUse {                  // an interactable that was touched
    uint64_t entity = 0;          // packed handle (sorted)
    uint8_t user = 0;
    Fixed progress{};
    int done = 0;                 // completions
    uint32_t readyAt = 0;
};
struct RaidPlayer {
    uint8_t slot = 0, team = 0;
    uint8_t status = 0;           // RaidStatus
    uint32_t endTick = 0;
    int16_t point = -1;           // extracted through
    uint8_t killer = 0;           // dead: owner slot of the last damage source
    int kills = 0;
    bool hadRaider = false;
    std::vector<std::pair<uint16_t, int>> secured, lost;   // item -> count (sorted by item)
    std::vector<Fixed> carriedRes, securedRes;             // per resource
    std::vector<int> objProgress;                          // per objective
    std::vector<uint8_t> objDone;                          // per objective: 0 open, 1 done
    Fixed hpSum{};                                         // raiders' hp at the end of the last raid tick (interrupts)
    bool hit = false;                                      // raiders lost hp this tick
};
struct RaidStore {
    uint8_t phase = 0;            // RaidPhase
    uint32_t phaseTick = 0;
    uint32_t endTick = 0;         // clock end (0: none)
    int winnerTeam = -1;
    std::vector<RaidPointState> points;
    std::vector<RaidChannel> channels;   // sorted by (player, point)
    std::vector<RaidUse> uses;           // sorted by entity
    std::vector<RaidPlayer> players;     // by slot order (only raid players: those with a raider at start)
    std::vector<std::string> receipts;   // per players[] entry, filled when the raid is over
    uint64_t matchId = 0;                // seed + map identity (receipt operation ids)
    RaidPlayer* find(uint8_t slot) { for (auto& p : players) if (p.slot == slot) return &p; return nullptr; }
};

class Raid {
public:
    bool active() const { return R_.enabled; }
    const RaidRules& rules() const { return R_; }
    void setup(Simulation& sim, RtsModule& m, const Json& map);   // after mechSetup (items, ground items)
    void tick(Simulation& sim);                                   // the "raid" system (before "death")
    void hash(Simulation& sim, Hasher& h) const;
    void io(Simulation& sim, Archive& a);
    bool command(Simulation& sim, const Command& cmd);           // kCmdRaid
    RaidStore* store(Simulation& sim) const;

    // ---- queries (Lua, bot, editor, tools)
    bool isRaider(Simulation& sim, Entity e) const;
    std::vector<Entity> raidersOf(Simulation& sim, uint8_t player) const;   // living, ECS order
    bool canUsePoint(Simulation& sim, uint8_t player, int point) const;     // requirements + team (not the phase)
    bool objectiveDone(Simulation& sim, uint8_t player, int objective) const;
    int64_t securedValue(const RaidPlayer& p) const;
    std::string receiptJson(Simulation& sim, const RaidPlayer& p) const;   // also valid while running (preview)

    // ---- actions (Lua raid.*, tests)
    bool extract(Simulation& sim, uint8_t player, int point);   // force (point -1: none)
    bool resolve(Simulation& sim, uint8_t player, RaidStatus st);
    void progress(Simulation& sim, uint8_t player, int objective, int amount);
    int spawnLoot(Simulation& sim, int table, Vec3 at, uint8_t player);   // rolls a table onto the ground; returns items placed
    bool give(Simulation& sim, uint8_t player, int item, int count, bool secured);

private:
    void start(Simulation& sim);
    void tickPoints(Simulation& sim);
    void tickChannels(Simulation& sim);
    void tickInteract(Simulation& sim);
    void tickPickup(Simulation& sim);
    void tickObjectives(Simulation& sim);
    void tickDeaths(Simulation& sim);
    void finish(Simulation& sim);
    void setPointPhase(Simulation& sim, size_t i, RaidPointPhase ph, uint32_t until);
    void secureCarried(Simulation& sim, RaidPlayer& p);
    void addSorted(std::vector<std::pair<uint16_t, int>>& v, uint16_t item, int n) const;
    Vec3 raiderPos(Simulation& sim, uint8_t player, bool& ok) const;
    RaidRules R_;
    RtsModule* m_ = nullptr;
};

// Raid bot (headless matches, match_run, raid_run): a player with raiders loots, completes required
// objectives and extracts. Pure function of the state; submits Commands like a human.
void raidBotThink(Simulation& sim, RtsModule& m, uint8_t player);

} // namespace aiverse::game
