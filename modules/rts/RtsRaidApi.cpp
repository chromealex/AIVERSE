// The `raid` table of the scripting API (extraction raids, RtsRaid.h). Queries in simulation and view scripts,
// actions in simulation scripts only. Events: on("raid_phase" | "raid_point" | "raid_channel" | "raid_extracted" |
// "raid_loot" | "raid_interact" | "raid_objective" | "raid_player", fn); veto hook: raid_can_extract(player, point_id).
// Reference: docs/raid.md (keep scriptApiReference() in RtsScriptApi.cpp in sync).
#include "RtsScriptInternal.h"
#include "RtsInternal.h"

namespace aiverse::game::script {

namespace {

Raid* raidOf(Vm& vm) {
    Raid* r = vm.module ? vm.module->raid() : nullptr;
    return r && r->active() && vm.sim && r->store(*vm.sim) ? r : nullptr;
}
bool needRaid(Vm& vm, Raid*& r) {
    r = raidOf(vm);
    if (!r) { vm.err = "the game has no raid (rules \"raid\")"; return false; }
    return true;
}
bool playerArg(lua_State* L, Vm& vm, int idx, Raid* r, RaidPlayer*& p) {
    int64_t s;
    if (!argInt(L, vm, idx, s, "player")) return false;
    p = s > 0 && s < 256 ? r->store(*vm.sim)->find(uint8_t(s)) : nullptr;
    if (!p) { vm.err = "player " + std::to_string(s) + " is not a raider"; return false; }
    return true;
}
const char* const kPhase[] = {"briefing", "running", "over"};
const char* const kPointPhase[] = {"locked", "sleep", "inbound", "open", "departing"};
const char* const kStatus[] = {"waiting", "in_raid", "extracted", "dead", "timeout", "abandoned"};

int rActive(lua_State* L, Vm& vm) { lua_pushboolean(L, raidOf(vm) != nullptr); return 1; }
int rPhase(lua_State* L, Vm& vm) {
    Raid* r = raidOf(vm);
    if (!r) { lua_pushnil(L); return 1; }
    lua_pushstring(L, kPhase[std::min<int>(2, r->store(*vm.sim)->phase)]);
    return 1;
}
int rTimeLeft(lua_State* L, Vm& vm) {
    Raid* r = raidOf(vm);
    RaidStore* st = r ? r->store(*vm.sim) : nullptr;
    if (!st || !st->endTick) { lua_pushnil(L); return 1; }
    const uint32_t now = vm.sim->tick();
    pushFx(L, Fixed::fromInt(int64_t(st->endTick > now ? st->endTick - now : 0)) / int64_t(vm.sim->tickRate()));
    return 1;
}
int rPlayers(lua_State* L, Vm& vm) {
    Raid* r = raidOf(vm);
    lua_newtable(L);
    if (!r) return 1;
    int n = 0;
    for (auto& p : r->store(*vm.sim)->players) { lua_pushinteger(L, p.slot); lua_rawseti(L, -2, ++n); }
    return 1;
}
void pushItems(lua_State* L, const Rules& rules, const std::vector<std::pair<uint16_t, int>>& v) {
    lua_newtable(L);
    for (auto& [it, n] : v) { lua_pushinteger(L, n); lua_setfield(L, -2, rules.mech.items[it].id.c_str()); }
}
int rPlayer(lua_State* L, Vm& vm) {
    Raid* r; RaidPlayer* p;
    if (!needRaid(vm, r) || !playerArg(L, vm, 1, r, p)) return -1;
    const Rules& rules = vm.module->rules();
    lua_createtable(L, 0, 10);
    lua_pushinteger(L, p->slot); lua_setfield(L, -2, "slot");
    lua_pushinteger(L, p->team); lua_setfield(L, -2, "team");
    lua_pushstring(L, kStatus[std::min<int>(5, p->status)]); lua_setfield(L, -2, "status");
    lua_pushinteger(L, r->securedValue(*p)); lua_setfield(L, -2, "value");
    lua_pushinteger(L, p->kills); lua_setfield(L, -2, "kills");
    pushItems(L, rules, p->secured); lua_setfield(L, -2, "secured");
    pushItems(L, rules, p->lost); lua_setfield(L, -2, "lost");
    std::vector<std::pair<uint16_t, int>> carried;
    for (Entity e : r->raidersOf(*vm.sim, p->slot))
        if (auto* bag = vm.sim->world().tryGet<ItemBag>(e))
            for (int16_t it : bag->items) {
                if (it < 0) continue;
                auto c = std::find_if(carried.begin(), carried.end(), [&](auto& x) { return x.first == uint16_t(it); });
                if (c == carried.end()) carried.push_back({uint16_t(it), 1}); else ++c->second;
            }
    std::sort(carried.begin(), carried.end());
    pushItems(L, rules, carried); lua_setfield(L, -2, "carried");
    lua_newtable(L);
    for (size_t k = 0; k < r->rules().objectives.size(); ++k) {
        lua_createtable(L, 0, 2);
        lua_pushboolean(L, p->objDone[k]); lua_setfield(L, -2, "done");
        lua_pushinteger(L, p->objProgress[k]); lua_setfield(L, -2, "progress");
        lua_setfield(L, -2, r->rules().objectives[k].id.c_str());
    }
    lua_setfield(L, -2, "objectives");
    return 1;
}
void pushPoint(lua_State* L, Raid* r, Simulation& sim, size_t i) {
    const RaidPointDef& d = r->rules().points[i];
    const RaidPointState& ps = r->store(sim)->points[i];
    lua_createtable(L, 0, 9);
    lua_pushstring(L, d.id.c_str()); lua_setfield(L, -2, "id");
    lua_pushstring(L, kPointPhase[std::min<int>(4, ps.phase)]); lua_setfield(L, -2, "phase");
    pushFx(L, ps.x); lua_setfield(L, -2, "x");
    pushFx(L, ps.z); lua_setfield(L, -2, "z");
    pushFx(L, d.radius); lua_setfield(L, -2, "radius");
    pushFx(L, ps.fill); lua_setfield(L, -2, "fill");
    pushFx(L, d.channel); lua_setfield(L, -2, "channel");
    lua_pushinteger(L, ps.used); lua_setfield(L, -2, "used");
    lua_pushinteger(L, ps.caller); lua_setfield(L, -2, "caller");
}
int rPoints(lua_State* L, Vm& vm) {
    Raid* r = raidOf(vm);
    lua_newtable(L);
    if (!r) return 1;
    for (size_t i = 0; i < r->rules().points.size(); ++i) { pushPoint(L, r, *vm.sim, i); lua_rawseti(L, -2, lua_Integer(i + 1)); }
    return 1;
}
int pointArg(lua_State* L, Vm& vm, Raid* r, int idx) {
    if (lua_type(L, idx) != LUA_TSTRING) { vm.err = "point id (string) expected"; return -2; }
    const int i = r->rules().findPoint(lua_tostring(L, idx));
    if (i < 0) vm.err = std::string("unknown extraction point '") + lua_tostring(L, idx) + "'";
    return i < 0 ? -2 : i;
}
int objectiveArg(lua_State* L, Vm& vm, Raid* r, int idx) {
    if (lua_type(L, idx) != LUA_TSTRING) { vm.err = "objective id (string) expected"; return -1; }
    const int i = r->rules().findObjective(lua_tostring(L, idx));
    if (i < 0) vm.err = std::string("unknown objective '") + lua_tostring(L, idx) + "'";
    return i;
}
int rPoint(lua_State* L, Vm& vm) {
    Raid* r;
    if (!needRaid(vm, r)) return -1;
    const int i = pointArg(L, vm, r, 1);
    if (i < 0) return -1;
    pushPoint(L, r, *vm.sim, size_t(i));
    return 1;
}
int rCanUse(lua_State* L, Vm& vm) {
    Raid* r; RaidPlayer* p;
    if (!needRaid(vm, r) || !playerArg(L, vm, 1, r, p)) return -1;
    const int i = pointArg(L, vm, r, 2);
    if (i < 0) return -1;
    lua_pushboolean(L, r->canUsePoint(*vm.sim, p->slot, i));
    return 1;
}
int rReceipt(lua_State* L, Vm& vm) {
    Raid* r; RaidPlayer* p;
    if (!needRaid(vm, r) || !playerArg(L, vm, 1, r, p)) return -1;
    const std::string s = r->receiptJson(*vm.sim, *p);
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}

// ---- actions
int rExtract(lua_State* L, Vm& vm) {
    Raid* r; RaidPlayer* p;
    if (!needRaid(vm, r) || !playerArg(L, vm, 1, r, p)) return -1;
    int point = -1;
    if (!lua_isnoneornil(L, 2) && (point = pointArg(L, vm, r, 2)) < 0) return -1;
    lua_pushboolean(L, r->extract(*vm.sim, p->slot, point));
    return 1;
}
int rFail(lua_State* L, Vm& vm) {
    Raid* r; RaidPlayer* p;
    if (!needRaid(vm, r) || !playerArg(L, vm, 1, r, p)) return -1;
    const std::string how = lua_type(L, 2) == LUA_TSTRING ? lua_tostring(L, 2) : "dead";
    RaidStatus s = how == "timeout" ? RaidStatus::TimedOut : how == "abandoned" ? RaidStatus::Abandoned : RaidStatus::Dead;
    if (how != "dead" && how != "timeout" && how != "abandoned") return fail(vm, "raid.fail: \"dead\", \"timeout\" or \"abandoned\" expected");
    lua_pushboolean(L, r->resolve(*vm.sim, p->slot, s));
    return 1;
}
int rProgress(lua_State* L, Vm& vm) {
    Raid* r; RaidPlayer* p; int64_t n = 1;
    if (!needRaid(vm, r) || !playerArg(L, vm, 1, r, p)) return -1;
    const int k = objectiveArg(L, vm, r, 2);
    if (k < 0 || (!lua_isnoneornil(L, 3) && !argInt(L, vm, 3, n, "amount"))) return -1;
    r->progress(*vm.sim, p->slot, k, int(std::clamp<int64_t>(n, -100000, 100000)));
    lua_pushboolean(L, r->objectiveDone(*vm.sim, p->slot, k));
    return 1;
}
int rComplete(lua_State* L, Vm& vm) {
    Raid* r; RaidPlayer* p;
    if (!needRaid(vm, r) || !playerArg(L, vm, 1, r, p)) return -1;
    const int k = objectiveArg(L, vm, r, 2);
    if (k < 0) return -1;
    r->progress(*vm.sim, p->slot, k, r->rules().objectives[size_t(k)].count);
    lua_pushboolean(L, r->objectiveDone(*vm.sim, p->slot, k));
    return 1;
}
int rGive(lua_State* L, Vm& vm) {
    Raid* r; RaidPlayer* p; int64_t n = 1;
    if (!needRaid(vm, r) || !playerArg(L, vm, 1, r, p)) return -1;
    if (lua_type(L, 2) != LUA_TSTRING) return fail(vm, "item id (string) expected");
    const int item = vm.module->rules().mech.findItem(lua_tostring(L, 2));
    if (item < 0) return fail(vm, std::string("unknown item '") + lua_tostring(L, 2) + "'");
    if (!lua_isnoneornil(L, 3) && !argInt(L, vm, 3, n, "count")) return -1;
    lua_pushboolean(L, r->give(*vm.sim, p->slot, item, int(std::clamp<int64_t>(n, 0, 1000)), lua_toboolean(L, 4)));
    return 1;
}
int rSpawnLoot(lua_State* L, Vm& vm) {
    Raid* r; Fixed x, z; int64_t slot = 0;
    if (!needRaid(vm, r)) return -1;
    if (lua_type(L, 1) != LUA_TSTRING) return fail(vm, "loot table id (string) expected");
    const int t = r->rules().findLoot(lua_tostring(L, 1));
    if (t < 0) return fail(vm, std::string("unknown loot table '") + lua_tostring(L, 1) + "'");
    if (!argFx(L, vm, 2, x, "x") || !argFx(L, vm, 3, z, "z") || (!lua_isnoneornil(L, 4) && !argInt(L, vm, 4, slot, "player"))) return -1;
    lua_pushinteger(L, r->spawnLoot(*vm.sim, t, {x, Fixed{}, z}, uint8_t(std::clamp<int64_t>(slot, 0, 255))));
    return 1;
}

} // namespace

void openRaidApi(Vm& vm) {
    lua_State* L = vm.L;
    static const luaL_Reg queries[] = {
        {"active", bindQ<rActive>}, {"phase", bindQ<rPhase>}, {"time_left", bindQ<rTimeLeft>}, {"players", bindQ<rPlayers>},
        {"player", bindQ<rPlayer>}, {"points", bindQ<rPoints>}, {"point", bindQ<rPoint>}, {"can_use", bindQ<rCanUse>},
        {"receipt", bindQ<rReceipt>}, {nullptr, nullptr}};
    static const luaL_Reg actions[] = {
        {"extract", bind<rExtract>}, {"fail", bind<rFail>}, {"progress", bind<rProgress>}, {"complete", bind<rComplete>},
        {"give", bind<rGive>}, {"spawn_loot", bind<rSpawnLoot>}, {nullptr, nullptr}};
    luaL_newlib(L, queries);
    if (vm.mode == Mode::Sim) luaL_setfuncs(L, actions, 0);
    lua_setglobal(L, "raid");
}

} // namespace aiverse::game::script
