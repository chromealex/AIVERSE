// Computer player controllers by name (rules ai.controller). The core knows "builtin" and "scripted"; any other
// controller registers itself here (a game's native module, an optional engine module) and the core never names it.
// A registered kind validates / loads its rules + map settings, creates one controller per bot slot and may ask for
// optional simulation counters (pointStats: per capture point produced / killed counts in the match stats).
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "../../engine/core/ECS.h"
#include "../../engine/data/Json.h"

namespace aiverse { class Simulation; class Archive; }

namespace aiverse::game {

struct Rules;

struct AIControllerConfig { virtual ~AIControllerConfig() = default; };

class AIController {
public:
    virtual ~AIController() = default;
    virtual std::unique_ptr<AIController> clone() const = 0;
    virtual void update(Simulation& sim) = 0;                 // every tick for its slot (sim.module(): the game module)
    virtual void stateIo(Archive& a) = 0;                     // bot save state (ComputerPlayer save state v4)
};

struct AIControllerKind {
    std::string name;                                         // rules ai.controller; also the ai.<name> settings block
    // rules ai block + map (Json() while only rules are validated); throws readable errors
    std::function<std::shared_ptr<const AIControllerConfig>(const Json& rulesAi, const Json& map, const Rules& r)> load;
    std::function<std::unique_ptr<AIController>(uint8_t player, uint64_t seed)> create;
    bool pointStats = false;                                  // keep (and hash) per point produced / kill counters
};

void registerAIController(AIControllerKind kind);             // replaces a kind of the same name
const AIControllerKind* findAIController(const std::string& name);
std::vector<std::string> aiControllerNames();                  // builtin, scripted, then registered kinds
// Defined by the code that links the core (engine/game/GameExtensions.cpp now, a game's native/ later):
// registers its controllers. Called once, before the first lookup.
void registerGameExtensions();

// Copyable owner of a bot's controller: copies of a bot diverge (rollback snapshots copy ComputerPlayer).
struct AIControllerHolder {
    std::unique_ptr<AIController> p;
    AIControllerHolder() = default;
    AIControllerHolder(const AIControllerHolder& o) : p(o.p ? o.p->clone() : nullptr) {}
    AIControllerHolder& operator=(const AIControllerHolder& o) { if (this != &o) p = o.p ? o.p->clone() : nullptr; return *this; }
    AIControllerHolder(AIControllerHolder&&) noexcept = default;
    AIControllerHolder& operator=(AIControllerHolder&&) noexcept = default;
};

} // namespace aiverse::game
