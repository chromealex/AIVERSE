// The `encounters` table of the scripting API (boss encounters, RtsEncounters.h). Queries in simulation and view
// scripts, actions in simulation scripts only. Events: on("boss_engaged" | "boss_phase" | "boss_enraged" |
// "boss_adds" | "boss_defeated" | "boss_window" | "telegraph_start" | "telegraph_strike" | "telegraph_end", fn);
// statuses an attack applies arrive as the ordinary "status_start". Reference: docs/encounters.md (keep
// scriptApiReference() in RtsScriptApi.cpp in sync).
#include "RtsScriptInternal.h"
#include "RtsInternal.h"

namespace aiverse::game::script {

namespace {

Encounters* encOf(Vm& vm) {
    Encounters* e = vm.module ? vm.module->encounters() : nullptr;
    return e && e->active() && vm.sim && e->store(*vm.sim) ? e : nullptr;
}
const char* const kMobMode[] = {"idle", "wander", "hunt", "flee", "return"};
const char* const kPhase[] = {"idle", "windup", "active", "recovery"};

int eActive(lua_State* L, Vm& vm) { lua_pushboolean(L, encOf(vm) != nullptr); return 1; }
// encounters.boss(unit) -> {type, phase, enraged, engaged, defeated, target, participant} | nil
int eBoss(lua_State* L, Vm& vm) {
    Encounters* en = encOf(vm);
    Entity e;
    if (!argEntity(L, 1, e)) return fail(vm, "unit expected");
    const EncBossState* b = en ? en->store(*vm.sim)->boss(e.packed()) : nullptr;
    if (!b) { lua_pushnil(L); return 1; }
    lua_newtable(L);
    lua_pushinteger(L, int64_t(b->brain.phase)); lua_setfield(L, -2, "phase");
    lua_pushboolean(L, b->brain.enraged); lua_setfield(L, -2, "enraged");
    lua_pushboolean(L, b->engaged); lua_setfield(L, -2, "engaged");
    lua_pushboolean(L, b->defeated); lua_setfield(L, -2, "defeated");
    lua_pushinteger(L, b->participant); lua_setfield(L, -2, "participant");
    if (b->target) { pushEntity(L, Entity::fromPacked(b->target)); lua_setfield(L, -2, "target"); }
    return 1;
}
// encounters.bosses() -> {unit, ...} (living bosses in entity order)
int eBosses(lua_State* L, Vm& vm) {
    Encounters* en = encOf(vm);
    lua_newtable(L);
    if (!en) return 1;
    int n = 0;
    for (auto& b : en->store(*vm.sim)->bosses) if (!b.defeated) { pushEntity(L, Entity::fromPacked(b.entity)); lua_rawseti(L, -2, ++n); }
    return 1;
}
// encounters.mob(unit) -> mode string | nil
int eMob(lua_State* L, Vm& vm) {
    Encounters* en = encOf(vm);
    Entity e;
    if (!argEntity(L, 1, e)) return fail(vm, "unit expected");
    const EncMobState* m = en ? en->store(*vm.sim)->mob(e.packed()) : nullptr;
    if (!m) { lua_pushnil(L); return 1; }
    lua_pushstring(L, kMobMode[std::min<int>(4, int(m->brain.mode))]);
    return 1;
}
// encounters.telegraphs() -> {{attack, caster, x, z, yaw, phase, progress}, ...} (progress 0..1000)
int eTelegraphs(lua_State* L, Vm& vm) {
    Encounters* en = encOf(vm);
    lua_newtable(L);
    if (!en) return 1;
    int n = 0;
    const uint32_t tick = vm.sim->tick();
    for (auto& c : en->store(*vm.sim)->casts) {
        const lg::TelegraphView v = en->view(c, tick);
        lua_newtable(L);
        lua_pushstring(L, en->rules().attacks[c.attack].id.c_str()); lua_setfield(L, -2, "attack");
        pushEntity(L, Entity::fromPacked(c.caster)); lua_setfield(L, -2, "caster");
        pushFx(L, v.pose.origin.x); lua_setfield(L, -2, "x");
        pushFx(L, v.pose.origin.y); lua_setfield(L, -2, "z");
        pushFx(L, v.pose.yaw); lua_setfield(L, -2, "yaw");
        lua_pushstring(L, kPhase[std::min<int>(3, int(v.phase))]); lua_setfield(L, -2, "phase");
        lua_pushinteger(L, v.progressMilli); lua_setfield(L, -2, "progress");
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}
int eWavesAllowed(lua_State* L, Vm& vm) {
    int64_t p;
    if (!argInt(L, vm, 1, p, "player")) return -1;
    Encounters* en = encOf(vm);
    lua_pushboolean(L, !en || en->wavesAllowed(*vm.sim, uint8_t(std::clamp<int64_t>(p, 0, 255))));
    return 1;
}
// encounters.cast(unit, attack, x, z [, target]) -> bool (simulation scripts)
int eCast(lua_State* L, Vm& vm) {
    Encounters* en = encOf(vm);
    if (!en) return fail(vm, "the game has no encounters (rules \"encounters\")");
    Entity e, t;
    Fixed x, z;
    if (!argEntity(L, 1, e)) return fail(vm, "unit expected");
    if (lua_type(L, 2) != LUA_TSTRING) return fail(vm, "attack id (string) expected");
    const int a = en->rules().findAttack(lua_tostring(L, 2));
    if (a < 0) return fail(vm, std::string("unknown attack '") + lua_tostring(L, 2) + "'");
    if (!argFx(L, vm, 3, x, "x") || !argFx(L, vm, 4, z, "z")) return -1;
    if (!lua_isnoneornil(L, 5) && !argEntity(L, 5, t)) return fail(vm, "target unit expected");
    lua_pushboolean(L, en->cast(*vm.sim, e, a, Vec3{x, Fixed{}, z}, t));
    return 1;
}

} // namespace

void openEncounterApi(Vm& vm) {
    lua_State* L = vm.L;
    static const luaL_Reg queries[] = {
        {"active", bindQ<eActive>}, {"boss", bindQ<eBoss>}, {"bosses", bindQ<eBosses>}, {"mob", bindQ<eMob>},
        {"telegraphs", bindQ<eTelegraphs>}, {"waves_allowed", bindQ<eWavesAllowed>}, {nullptr, nullptr}};
    static const luaL_Reg actions[] = {{"cast", bind<eCast>}, {nullptr, nullptr}};
    luaL_newlib(L, queries);
    if (vm.mode == Mode::Sim) luaL_setfuncs(L, actions, 0);
    lua_setglobal(L, "encounters");
}

} // namespace aiverse::game::script
