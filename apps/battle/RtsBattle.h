// RTS-module battles on the hosted battle server (docs/battle-net.md, docs/topdown.md): a top-down game project
// (templates/topdown_arena, templates/extraction, a Drone Fall port) behind the BattleLoad.h BattleGame interface.
// Every client builds the RtsModule simulation from the project's rules + map, bots steer their heroes with the
// computer pilot (TopDown::botCommand -> kCmdTopDown) and tap hero abilities (kCmdTopDownAbility), the server lets
// only those two command types through, and the result report is the module's frozen outcome blob
// (RtsModule::outcomeBlob). Rollback corrections of heroes and squad members feed the correction smoother.
#pragma once
#include <memory>
#include <string>
#include "BattleLoad.h"

namespace aiverse::battle {

struct RtsBattleOptions {
    std::string project;        // directory with game.json (rules, maps, scripts)
    std::string map;            // map file relative to the project (empty: the first map of game.json)
    std::string mapJson;        // inline map (instead of a file; tile maps are applied either way)
    std::string rulesPatch;     // JSON object deep-merged over the rules (tests: shorter fights)
    uint32_t abilityEveryMs = 2500;   // bots tap a random hero ability about this often (0: never)
    uint32_t steerEveryMs = 100;      // bots re-run the computer pilot this often
};

// Throws std::runtime_error with a readable message when the project cannot be loaded.
std::unique_ptr<BattleGame> rtsBattleGame(const RtsBattleOptions& options);

} // namespace aiverse::battle
