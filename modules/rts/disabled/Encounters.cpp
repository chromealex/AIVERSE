// Boss encounter adapter for packaged clients built without the "encounters" engine module
// (packaging.engineModules, CMake AIVERSE_GAME_WITH_ENCOUNTERS=OFF). Encounters stay inactive, so no
// system, hash or save block is registered - games without rules "encounters" run as with the full
// module. Rules with an "encounters" block fail at match start with a readable error. The Lua table
// `encounters` keeps only active() (false).
#include "../RtsEncounters.h"
#include "../RtsScriptInternal.h"
#include <stdexcept>

namespace aiverse::game {
namespace {
[[noreturn]] void missingModule() {
    throw std::runtime_error("rules.encounters: this client was built without the encounters module; "
                             "add \"encounters\" (or \"auto\") to packaging.engineModules in game.json");
}
} // namespace

EncounterRules Encounters::parse(const Json& j, const Rules&, uint32_t) {
    if (j.isObject()) missingModule();
    return {};
}
void Encounters::setup(Simulation&, RtsModule& m, const Json& rulesJson, const Json&) {
    m_ = &m;
    if (rulesJson["encounters"].isObject()) missingModule();
}
void Encounters::tick(Simulation&) {}
void Encounters::hash(Simulation&, Hasher&) const {}
void Encounters::io(Simulation&, Archive&) {}
EncounterStore* Encounters::store(Simulation&) const { return nullptr; }
bool Encounters::monsterPlayer(uint8_t) const { return false; }
int64_t Encounters::teamKey(Simulation&, uint8_t) const { return 0; }
Fixed Encounters::speedMul(Simulation&, Entity) const { return Fixed::fromInt(1); }
void Encounters::onAbility(Simulation&, Entity, int, Entity, Vec3) {}
bool Encounters::cast(Simulation&, Entity, int, Vec3, Entity, int64_t) { return false; }
lg::TelegraphView Encounters::view(const EncCast&, uint32_t) const { return {}; }
bool Encounters::wavesAllowed(Simulation&, uint8_t) const { return true; }

namespace script {
namespace {
int eActive(lua_State* L, Vm&) { lua_pushboolean(L, 0); return 1; }
} // namespace
void openEncounterApi(Vm& vm) {
    lua_State* L = vm.L;
    static const luaL_Reg queries[] = {{"active", bindQ<eActive>}, {nullptr, nullptr}};
    luaL_newlib(L, queries);
    lua_setglobal(L, "encounters");
}
} // namespace script

} // namespace aiverse::game
