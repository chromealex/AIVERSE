// Optional controllers and game extensions linked with the core. Each registers itself; the core only knows
// names from data (rules ai.controller). A packaged client built without an optional module links its empty
// registration (engine/game/disabled/), so rules that need it fail with a readable "unknown controller" error.
#include "../../engine/game/AIControllers.h"
#include "BwAI.h"

namespace aiverse::game {

void registerGameExtensions() {
    registerBwAIController();   // "bw"
}

} // namespace aiverse::game
