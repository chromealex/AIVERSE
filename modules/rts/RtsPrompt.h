// Prompts (agent-gta2): choice menus that simulation scripts show to a player: a talk with a passer-by, an offer,
// a question. rules "prompts": true (docs/prompts.md).
//
// A script opens a prompt for a player (prompt.show: an id, a title, lines of text, options). The client shows it
// and sends kCmdPrompt with the option picked; the prompt closes and the game's global on_prompt(player,
// "<id>:<option>") runs. kCmdPrompt with -1 is the "interact" key with no prompt open: on_interact(player, "").
// One prompt per player; state lives in the PromptStore world resource (hashed, saved).
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "../../engine/core/ECS.h"
#include "../../engine/data/Json.h"

namespace aiverse { class Simulation; class Hasher; class Archive; struct Command; }

namespace aiverse::game {

class RtsModule;

struct Prompt {
    std::string id, title;
    std::vector<std::string> lines, options;
    bool open() const { return !id.empty(); }
};

struct PromptStore {
    std::vector<Prompt> players;   // by slot
};

constexpr int64_t kPromptInteract = -1;

class Prompts {
public:
    bool active() const { return enabled_; }
    void setup(Simulation& sim, RtsModule& m, const Json& rulesJson);
    bool command(Simulation& sim, const Command& cmd);   // true: kCmdPrompt handled
    void hash(Simulation& sim, Hasher& h) const;
    void io(Simulation& sim, Archive& a);
    PromptStore* store(Simulation& sim) const;
    const Prompt* of(Simulation& sim, uint8_t player) const;   // the open prompt, or null
    // simulation code only
    bool show(Simulation& sim, uint8_t player, Prompt p);      // false: bad player, empty id, no options, too long
    void close(Simulation& sim, uint8_t player);

private:
    RtsModule* m_ = nullptr;
    bool enabled_ = false;
};

} // namespace aiverse::game
