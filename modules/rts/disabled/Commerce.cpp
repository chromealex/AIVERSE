// City economy adapter for packaged clients built without the "commerce" engine module
// (packaging.engineModules, CMake AIVERSE_GAME_WITH_COMMERCE=OFF). Commerce stays inactive: no system,
// hash, save block or command handling, exactly like rules without "commerce". Rules with a "commerce"
// block fail at match start with a readable error. The Lua table `commerce` keeps only active() (false).
#include "../RtsCommerce.h"
#include "../RtsScriptInternal.h"
#include <stdexcept>

namespace aiverse::game {
namespace {
[[noreturn]] void missingModule() {
    throw std::runtime_error("rules.commerce: this client was built without the commerce module; "
                             "add \"commerce\" (or \"auto\") to packaging.engineModules in game.json");
}
} // namespace

CommerceRules Commerce::parse(const Json& j, const std::vector<std::string>&, uint32_t, const std::vector<std::string>&) {
    if (j.isObject()) missingModule();
    return {};
}
void Commerce::setup(Simulation&, RtsModule& m, const Json& rulesJson, const Json&) {
    m_ = &m;
    if (rulesJson["commerce"].isObject()) missingModule();
}
bool Commerce::command(Simulation&, const Command&) { return false; }
void Commerce::tick(Simulation&) {}
void Commerce::hash(Simulation&, Hasher&) const {}
void Commerce::io(Simulation&, Archive&) {}
CommerceStore* Commerce::store(Simulation&) const { return nullptr; }
const char* Commerce::refusal(Simulation&, uint8_t, CommerceOp, int, int64_t) const { return "commerce is not part of this build"; }
bool Commerce::act(Simulation&, uint8_t, CommerceOp, int, int64_t) { return false; }
void Commerce::fine(Simulation&, uint8_t, Fixed) {}
Fixed Commerce::cash(Simulation&, uint8_t) const { return {}; }
bool Commerce::bind(Simulation&, uint8_t, uint64_t) { return false; }
void Commerce::unbind(Simulation&, uint8_t) {}
bool Commerce::sellOut(Simulation&, uint64_t) { return false; }
const CommerceAccount* Commerce::account(Simulation&, uint8_t) const { return nullptr; }
const CommerceAccount* Commerce::accountById(Simulation&, uint64_t) const { return nullptr; }
Json Commerce::exportAccount(Simulation&, uint64_t) const { return Json(); }
bool Commerce::importAccount(Simulation&, uint64_t, const Json&) { return false; }
uint8_t Commerce::slotOf(Simulation&, uint16_t) const { return 0; }
Fixed Commerce::creditLimit(Simulation&, uint8_t) const { return {}; }
int Commerce::findPremise(Simulation&, const std::string&) const { return -1; }
int Commerce::findBusiness(const std::string&) const { return -1; }
int Commerce::findOffer(const std::string&) const { return -1; }
bool Commerce::visit(Simulation&, int) { return false; }
Fixed Commerce::expectedToday(const Premise&) const { return {}; }
uint32_t Commerce::day(const Simulation&) const { return 0; }

namespace script {
namespace {
int cActive(lua_State* L, Vm&) { lua_pushboolean(L, 0); return 1; }
} // namespace
void openCommerceApi(Vm& vm) {
    lua_State* L = vm.L;
    static const luaL_Reg queries[] = {{"active", bindQ<cActive>}, {nullptr, nullptr}};
    luaL_newlib(L, queries);
    lua_setglobal(L, "commerce");
}
} // namespace script

} // namespace aiverse::game
