// City traffic adapter for packaged clients built without the "traffic" engine module
// (packaging.engineModules, CMake AIVERSE_GAME_WITH_TRAFFIC=OFF). Traffic stays inactive, so the
// simulation never registers its system, hash or save block: games without rules "traffic" run,
// hash and save exactly as with the full module. Rules that enable traffic fail at match start.
#include "../RtsTraffic.h"
#include <stdexcept>

namespace aiverse::game {
namespace {
[[noreturn]] void missingModule() {
    throw std::runtime_error("rules.traffic: this client was built without the traffic module; "
                             "add \"traffic\" (or \"auto\") to packaging.engineModules in game.json");
}
bool enabled(const Json& j) { return j.isObject() && j.getBool("enabled", true); }
} // namespace

TrafficRules Traffic::parse(const Json& j, const std::vector<std::string>&, uint32_t) {
    if (enabled(j)) missingModule();
    return {};
}
void Traffic::parseGangs(const Json& j, TrafficRules&, const std::vector<std::string>&, uint32_t) { if (enabled(j)) missingModule(); }
void Traffic::parsePopulation(const Json& j, TrafficRules&, const std::vector<std::string>&, uint32_t) { if (enabled(j)) missingModule(); }
void Traffic::parseCity(const Json& j, TrafficRules&, const std::vector<std::string>&, uint32_t) { if (enabled(j)) missingModule(); }

void Traffic::setup(Simulation&, RtsModule& m, const Json& rulesJson, const Json& map) {
    m_ = &m;
    const Json& mapBlock = map["traffic"];
    if (mapBlock.type() == Json::Type::Bool && !mapBlock.boolean(true)) return;   // this map has no traffic
    if (enabled(rulesJson["traffic"])) missingModule();
}
void Traffic::tick(Simulation&) {}
void Traffic::collect(Simulation&) {}
void Traffic::hash(Simulation&, Hasher&) const {}
void Traffic::io(Simulation&, Archive&) {}
TrafficStore* Traffic::store(Simulation&) const { return nullptr; }
bool Traffic::aiInput(Simulation&, Entity, lib::genre::VehicleInput&) const { return false; }
lib::genre::TrafficLight Traffic::light(Simulation&, int, lib::genre::TrafficAxis) const { return lib::genre::TrafficLight::Green; }
int Traffic::junctionAt(Fixed, Fixed) const { return -1; }
bool Traffic::walkAllowed(Simulation&, int) const { return true; }
uint32_t Traffic::report(Simulation&, uint8_t, Vec2, Entity, Entity, bool) { return 0; }
void Traffic::scaleLimits(int) {}
int Traffic::minuteOfDay(Simulation&) const { return 0; }
const TrafficCitizen* Traffic::citizenOf(Simulation&, Entity) const { return nullptr; }
int Traffic::present(Simulation&, int) const { return 0; }
int Traffic::kindOf(Simulation&, Entity) const { return -1; }
bool Traffic::onRoad(Vec2) const { return false; }
bool Traffic::projectReturnCar(Simulation&, TrafficStore&, int, Entity) { return false; }
// Referenced by the inline project* helpers of RtsTraffic.h.
bool Traffic::roadSpot(Vec2, int, int32_t&, Fixed&) const { return false; }
bool Traffic::reroute(Simulation&, TrafficCar&, Vec2, Vec2) { return false; }
void Traffic::depart(Simulation&, TrafficStore&, int32_t, uint8_t) {}
bool Traffic::onShift(const TrafficCitizen&, int, int) const { return false; }

} // namespace aiverse::game
