// The driving host's adapter for races (rules "race" + map "race": the shared format of every project,
// genre/RaceSessionData.h, docs/races.md) over the vehicle-controllers race session (genre/RaceSession.h). Racer i is
// the i-th car of the map (entity order) and belongs to player i + 1: that player's hero is seated in it at the start
// (the doors stay shut until the race is over); a player marked "bot" or without a hero races with the race bot along
// the racing line. Driving (RtsDriving.cpp) feeds the session's pedals to the cars, steps and collides them, reports
// their positions and teleports respawns; the race decides the match (winner = team of place 1).
#pragma once
#include "RtsDriving.h"
#include "../../libraries/include/aiverselib/genre/RaceSession.h"
#include <optional>
#include <vector>

namespace aiverse::game {

// Immutable: rules + map (metres, library yaw). The grid is taken from the cars' poses at the start (state).
struct DrivingRace {
    lib::genre::RaceSessionConfig config;
    lib::genre::RaceCourse course;                     // grid only when the map has one (mapGrid)
    std::optional<lib::bots::RaceLine> line;
    bool mapGrid = false, crashDamage = false;
    std::vector<bool> bots;                            // map players[i].bot (racer i + 1)
};

// World resource (only on maps with a race): hashed, saved after the cars, copied by rollback snapshots.
struct DrivingRaceState {
    bool started = false;
    std::vector<Entity> racers;                        // racer id i + 1, entity order at the start
    std::vector<lib::genre::RaceGridSlot> grid;        // the cars' poses at the start (respawn before checkpoint 1)
    lib::genre::RaceSessionState session;
    int32_t winnerTeam = -1;                           // set once when the race is over
};

} // namespace aiverse::game
