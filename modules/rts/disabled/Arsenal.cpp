// Hero weapon adapter for packaged clients built without the "arsenal" engine module
// (packaging.engineModules, CMake AIVERSE_GAME_WITH_ARSENAL=OFF). The arsenal stays inactive, exactly
// like rules without "arsenal"; rules with that block fail at match start with a readable error.
#include "../RtsArsenal.h"
#include <stdexcept>

namespace aiverse::game {
namespace {
[[noreturn]] void missingModule() {
    throw std::runtime_error("rules.arsenal: this client was built without the arsenal module; "
                             "add \"arsenal\" (or \"auto\") to packaging.engineModules in game.json");
}
} // namespace

ArsenalRules Arsenal::parse(const Json& j, const std::vector<std::string>&, const std::vector<std::string>&) {
    if (j.isObject()) missingModule();
    return {};
}
void Arsenal::setup(Simulation&, RtsModule& m, const Json& rulesJson) {
    m_ = &m;
    if (rulesJson["arsenal"].isObject()) missingModule();
}
bool Arsenal::command(Simulation&, const Command&) { return false; }
void Arsenal::tick(Simulation&) {}
void Arsenal::hash(Simulation&, Hasher&) const {}
void Arsenal::io(Simulation&, Archive&) {}
ArsenalStore* Arsenal::store(Simulation&) const { return nullptr; }
const ArsenalHand* Arsenal::hand(Simulation&, uint8_t) const { return nullptr; }
bool Arsenal::isCrate(Simulation&, Entity) const { return false; }
void Arsenal::neutralKill(Simulation&, Entity, uint8_t) {}

} // namespace aiverse::game
