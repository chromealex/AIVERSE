// Career ladder adapter for packaged clients built without the "careers" engine module
// (packaging.engineModules, CMake AIVERSE_GAME_WITH_CAREERS=OFF). Careers stay inactive, exactly like
// rules without "careers"; rules with that block fail at match start with a readable error. The Lua
// table `career` keeps only active() (false).
#include "../RtsCareer.h"
#include "../RtsScriptInternal.h"
#include <stdexcept>

namespace aiverse::game {
namespace {
[[noreturn]] void missingModule() {
    throw std::runtime_error("rules.careers: this client was built without the careers module; "
                             "add \"careers\" (or \"auto\") to packaging.engineModules in game.json");
}
} // namespace

CareerRules Career::parse(const Json& j) {
    if (j.isObject()) missingModule();
    return {};
}
void Career::setup(Simulation&, RtsModule& m, const Json& rulesJson) {
    m_ = &m;
    if (rulesJson["careers"].isObject()) missingModule();
}
bool Career::command(Simulation&, const Command&) { return false; }
void Career::hash(Simulation&, Hasher&) const {}
void Career::io(Simulation&, Archive&) {}
CareerStore* Career::store(Simulation&) const { return nullptr; }
int Career::find(const std::string&) const { return -1; }
const CareerProgress* Career::progress(Simulation&, uint8_t, int) const { return nullptr; }
int Career::rank(Simulation&, uint8_t, int) const { return -1; }
bool Career::hasPerk(Simulation&, uint8_t, const std::string&) const { return false; }
bool Career::join(Simulation&, uint8_t, int) { return false; }
bool Career::leave(Simulation&, uint8_t, int) { return false; }
int Career::add(Simulation&, uint8_t, int, Fixed) { return 0; }
Json Career::save(Simulation&, uint8_t) const { return Json(); }
void Career::load(Simulation&, uint8_t, const Json&) {}
void Career::clear(Simulation&, uint8_t) {}

namespace script {
namespace {
int kActive(lua_State* L, Vm&) { lua_pushboolean(L, 0); return 1; }
} // namespace
void openCareerApi(Vm& vm) {
    lua_State* L = vm.L;
    static const luaL_Reg queries[] = {{"active", bindQ<kActive>}, {nullptr, nullptr}};
    luaL_newlib(L, queries);
    lua_setglobal(L, "career");
}
} // namespace script

} // namespace aiverse::game
