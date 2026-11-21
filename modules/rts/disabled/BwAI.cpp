// Bunker Wars computer player adapter for packaged clients built without the "bw-ai" engine module
// (packaging.engineModules, CMake AIVERSE_GAME_WITH_BWAI=OFF). The controller only runs for rules
// ai.controller "bw"; such rules fail at load with a readable error, so games with other AI behave
// exactly as with the full module.
#include "../RtsBwAI.h"
#include "../RtsAI.h"
#include <stdexcept>

namespace aiverse::game {
namespace {
[[noreturn]] void missingModule() {
    throw std::runtime_error("ai.controller \"bw\": this client was built without the bw-ai module; "
                             "add \"bw-ai\" (or \"auto\") to packaging.engineModules in game.json");
}
} // namespace

std::shared_ptr<const BwAIConfig> loadBwAIConfig(const Json&, const Json&, const Rules&) { missingModule(); }
void BwAI::update(Simulation&, RtsModule&) { missingModule(); }
void BwAI::stateIo(Archive&) { missingModule(); }

// RtsAI owns a holder for every bot; it stays empty without the controller.
BwAIHolder::BwAIHolder() = default;
BwAIHolder::BwAIHolder(const BwAIHolder& o) : p(o.p ? std::make_unique<BwAI>(*o.p) : nullptr) {}
BwAIHolder& BwAIHolder::operator=(const BwAIHolder& o) { if (this != &o) p = o.p ? std::make_unique<BwAI>(*o.p) : nullptr; return *this; }
BwAIHolder::BwAIHolder(BwAIHolder&&) noexcept = default;
BwAIHolder& BwAIHolder::operator=(BwAIHolder&&) noexcept = default;
BwAIHolder::~BwAIHolder() = default;

} // namespace aiverse::game
