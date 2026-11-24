// Bunker Wars computer player left out of a packaged client (packaging.engineModules without "bw-ai", CMake
// AIVERSE_GAME_WITH_BWAI=OFF): the controller "bw" is not registered, so rules ai.controller "bw" fail at load with
// "ai.controller: unknown 'bw' (this build has: builtin, scripted; ...)". Games with other AI behave as with it.
#include "../BwAI.h"

namespace aiverse::game {
void registerBwAIController() {}
} // namespace aiverse::game
