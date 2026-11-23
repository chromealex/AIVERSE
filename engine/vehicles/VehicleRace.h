// The one race adapter (rules "race" + map "race", the shared format of every project: mechanics/RaceSessionData.h,
// docs/races.md) over the vehicle-controllers race session (mechanics/RaceSession.h), for every vehicle host.
// Racer i + 1 is the i-th car the host lists at the start (ascending entity index on a grid map, ascending vehicle id
// on a metre map) and belongs to player i + 1: the host seats that player (or a bot) at the wheel, the doors stay shut
// until the race is over; bots race with the race bot along the racing line. Per tick the host calls
//   pedals()  before stepVehicles (countdown brakes, race bots, the players' own pedals -> RacePedals),
//   after()   after it (checkpoints, laps, standings, respawns, the result).
// State: the RaceState world resource (hashed, saved, copied by rollback snapshots); the rules and course are
// immutable (VehicleRace, owned by the host).
#pragma once
#include "Vehicles.h"
#include "engine/mechanics/RaceSession.h"
#include <memory>
#include <optional>
#include <vector>

namespace aiverse::vehicles {

struct RaceState {
    bool started = false;
    std::vector<Entity> racers;                    // racer id i + 1
    std::vector<lm::RaceGridSlot> grid;            // the cars' poses at the start (respawn before checkpoint 1)
    lm::RaceSessionState session;
    int32_t winnerTeam = -1;                       // set once when the race is over
};

class RaceHost {
public:
    virtual ~RaceHost() = default;
    virtual void placeOnGrid(size_t i, Entity car, VehicleDrive& d, const lm::RaceGridSlot& slot) = 0;   // map grid
    // Racer i (player i + 1) takes the wheel of `car`; true: a bot drives it (the race bot's pedals).
    virtual bool seat(size_t i, Entity car, VehicleDrive& d) = 0;
    virtual lm::VehicleInput pedals(size_t i, Entity car, const VehicleDrive& d) = 0;   // the racer's own pedals
    virtual bool wantsRespawn(size_t, Entity, const VehicleDrive&) { return false; }    // wrecked, fallen off the world
    virtual void respawned(size_t i, Entity car, VehicleDrive& d) = 0;                 // after the teleport: host pose
    // The race is over (winner: racer id, 0 nobody): the host decides its match; returns the winning team (0: draw).
    virtual int32_t raceOver(int64_t winner) = 0;
};

class VehicleRace {
public:
    lm::RaceSessionConfig config;
    lm::RaceCourse course;                         // grid only when the map has one (mapGrid)
    std::optional<lib::bots::RaceLine> line;
    bool mapGrid = false, crashDamage = false;
    std::vector<bool> bots;                        // map players[i].bot (racer i + 1)

    // Rules + map; null when the map has no race. metresPerUnit: map units -> metres. Throws readable errors.
    // Sets the RaceState resource.
    static std::shared_ptr<const VehicleRace> read(World& w, const Json& rules, const Json& map, Fixed metresPerUnit,
                                                   uint32_t tickRate, const DriveRules& drive);
    static RaceState* state(World& w) { return w.resource<RaceState>(); }
    static const RaceState* state(const World& w) { return w.resourceConst<RaceState>(); }
    bool started(const World& w) const { const RaceState* s = state(w); return s && s->started; }
    // Seats the racers in `cars` (at most one per grid slot and kRaceMaxRacers) and starts the session.
    // false: no race (no cars, bots without a racing line, ...).
    bool start(World& w, RaceHost& host, const DriveRules& drive, std::span<const Entity> cars, uint32_t tick) const;
    std::vector<RacePedals> pedals(World& w, RaceHost& host, uint32_t tick) const;
    // Host-only broad-phase admission: candidate teleport destinations for
    // the next race step, before collision publication and command mutation.
    // Includes previous checkpoint/grid and the next checkpoint if crossed
    // on the coming tick; sorted and unique for deterministic pinning.
    std::vector<Vec3> possibleRespawnPositions(const World& w) const;
    void after(World& w, RaceHost& host, const DriveRules& drive, uint32_t tick) const;
    int64_t racerOf(const World& w, Entity car) const;                // racer id, 0: not racing
    bool holds(const World& w, Entity car) const;                     // a racer during the race: nobody gets out
    bool crashHurts(const World& w, Entity car) const { return crashDamage || !holds(w, car); }
    lm::RaceView view(const World& w, int64_t racer, uint32_t tick) const;
    void hash(const World& w, Hasher& h) const;
    static void io(Archive& a, RaceState& s);      // fails on an invalid record
};

} // namespace aiverse::vehicles
