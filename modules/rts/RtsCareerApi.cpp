// The `career` table of the scripting API (RtsCareer.h, docs/careers.md). Queries in simulation and view scripts,
// join / leave / add in simulation scripts only.
#include "RtsScriptInternal.h"
#include "RtsInternal.h"
#include "RtsCareer.h"

namespace aiverse::game::script {

namespace {

Career* careerOf(Vm& vm) {
    Career* c = vm.module ? vm.module->career() : nullptr;
    return c && c->active() && vm.sim && c->store(*vm.sim) ? c : nullptr;
}
// player, career id -> both checked
bool args(lua_State* L, Vm& vm, Career*& c, uint8_t& player, int& career) {
    c = careerOf(vm);
    if (!c) { vm.err = "the game has no careers (rules \"careers\")"; return false; }
    int64_t s;
    if (!argInt(L, vm, 1, s, "player")) return false;
    player = uint8_t(std::clamp<int64_t>(s, 0, 255));
    career = lua_type(L, 2) == LUA_TSTRING ? c->find(lua_tostring(L, 2)) : -1;
    if (career < 0) { vm.err = "career: unknown career"; return false; }
    return true;
}

int kActive(lua_State* L, Vm& vm) { lua_pushboolean(L, careerOf(vm) != nullptr); return 1; }
// career.list() -> {{id, name, open, ranks = {{name, points, perks}}}}
int kList(lua_State* L, Vm& vm) {
    Career* c = careerOf(vm);
    lua_newtable(L);
    if (!c) return 1;
    int n = 0;
    for (auto& t : c->rules().careers) {
        lua_newtable(L);
        lua_pushstring(L, t.id.c_str()); lua_setfield(L, -2, "id");
        lua_pushstring(L, t.name.c_str()); lua_setfield(L, -2, "name");
        lua_pushboolean(L, t.open); lua_setfield(L, -2, "open");
        lua_pushstring(L, t.office.c_str()); lua_setfield(L, -2, "office");
        lua_newtable(L);
        for (size_t r = 0; r < t.ranks.size(); ++r) {
            lua_newtable(L);
            lua_pushstring(L, t.ranks[r].name.c_str()); lua_setfield(L, -2, "name");
            pushFx(L, t.ranks[r].points); lua_setfield(L, -2, "points");
            lua_newtable(L);
            for (size_t k = 0; k < t.ranks[r].perks.size(); ++k) { lua_pushstring(L, t.ranks[r].perks[k].c_str()); lua_rawseti(L, -2, lua_Integer(k + 1)); }
            lua_setfield(L, -2, "perks");
            lua_rawseti(L, -2, lua_Integer(r + 1));
        }
        lua_setfield(L, -2, "ranks");
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}
// career.status(player, id) -> nil (not on it) | {points, rank, rank_name, next_points}
int kStatus(lua_State* L, Vm& vm) {
    Career* c; uint8_t p; int k;
    if (!args(L, vm, c, p, k)) return -1;
    const int r = c->rank(*vm.sim, p, k);
    if (r < 0) { lua_pushnil(L); return 1; }
    const CareerType& t = c->rules().careers[size_t(k)];
    lua_newtable(L);
    pushFx(L, c->progress(*vm.sim, p, k)->points); lua_setfield(L, -2, "points");
    lua_pushinteger(L, r + 1); lua_setfield(L, -2, "rank");
    lua_pushstring(L, t.ranks[size_t(r)].name.c_str()); lua_setfield(L, -2, "rank_name");
    if (size_t(r + 1) < t.ranks.size()) { pushFx(L, t.ranks[size_t(r + 1)].points); lua_setfield(L, -2, "next_points"); }
    return 1;
}
int kHasPerk(lua_State* L, Vm& vm) {
    Career* c = careerOf(vm);
    int64_t s;
    if (!argInt(L, vm, 1, s, "player")) return -1;
    lua_pushboolean(L, c && lua_type(L, 2) == LUA_TSTRING && c->hasPerk(*vm.sim, uint8_t(std::clamp<int64_t>(s, 0, 255)), lua_tostring(L, 2)));
    return 1;
}
int kJoin(lua_State* L, Vm& vm) {
    Career* c; uint8_t p; int k;
    if (!args(L, vm, c, p, k)) return -1;
    lua_pushboolean(L, c->join(*vm.sim, p, k));
    return 1;
}
int kLeave(lua_State* L, Vm& vm) {
    Career* c; uint8_t p; int k;
    if (!args(L, vm, c, p, k)) return -1;
    lua_pushboolean(L, c->leave(*vm.sim, p, k));
    return 1;
}
// career.add(player, id, points) -> ranks gained (0 when not on the career)
int kAdd(lua_State* L, Vm& vm) {
    Career* c; uint8_t p; int k; Fixed pts;
    if (!args(L, vm, c, p, k) || !argFx(L, vm, 3, pts, "points")) return -1;
    lua_pushinteger(L, c->add(*vm.sim, p, k, pts));
    return 1;
}

} // namespace

void openCareerApi(Vm& vm) {
    lua_State* L = vm.L;
    static const luaL_Reg queries[] = {
        {"active", bindQ<kActive>}, {"list", bindQ<kList>}, {"status", bindQ<kStatus>}, {"has_perk", bindQ<kHasPerk>}, {nullptr, nullptr}};
    static const luaL_Reg actions[] = {{"join", bind<kJoin>}, {"leave", bind<kLeave>}, {"add", bind<kAdd>}, {nullptr, nullptr}};
    luaL_newlib(L, queries);
    if (vm.mode == Mode::Sim) luaL_setfuncs(L, actions, 0);
    lua_setglobal(L, "career");
}

} // namespace aiverse::game::script
