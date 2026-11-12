// The `driving` table of the scripting API (RtsDriving.h, docs/driving.md): queries for mission scripts and the HUD.
#include "RtsScriptInternal.h"
#include "RtsInternal.h"
#include "RtsDriving.h"
#include "RtsTopDown.h"

namespace aiverse::game::script {

namespace {

bool unitArg(lua_State* L, Vm& vm, int idx, Entity& e) {
    World& w = vm.sim->world();
    return argEntity(L, idx, e) && w.alive(e) && w.has<Unit>(e) && w.has<Transform>(e);
}
Driving* drivingOf(Vm& vm) {
    Driving* d = vm.module ? vm.module->driving() : nullptr;
    return d && d->active() && vm.sim && d->store(*vm.sim) ? d : nullptr;
}
// driving.car_of(player) -> the vehicle the player's hero drives, or nil
int dCarOf(lua_State* L, Vm& vm) {
    Driving* d = drivingOf(vm);
    int64_t s;
    if (!argInt(L, vm, 1, s, "player")) return -1;
    const DrivingCar* c = nullptr;
    if (d && s > 0 && s < 256 && vm.module->topDown() && vm.module->topDown()->active()) {
        const Entity hero = vm.module->topDown()->heroOf(*vm.sim, uint8_t(s));
        if (hero.valid()) c = d->carOf(*vm.sim, hero);
    }
    if (c) pushEntity(L, c->e); else lua_pushnil(L);
    return 1;
}
// driving.speed(vehicle) -> forward speed in m/s (0 for anything that is not a vehicle)
int dSpeed(lua_State* L, Vm& vm) {
    Driving* d = drivingOf(vm);
    Entity e;
    if (!d || !unitArg(L, vm, 1, e)) { pushFx(L, Fixed{}); return 1; }
    pushFx(L, d->speed(*vm.sim, e));
    return 1;
}
// driving.driver(vehicle) -> the hero at the wheel, or nil
int dDriver(lua_State* L, Vm& vm) {
    Driving* d = drivingOf(vm);
    Entity e;
    const DrivingCar* c = d && unitArg(L, vm, 1, e) ? d->car(*vm.sim, e) : nullptr;
    if (c && c->driver.valid()) pushEntity(L, c->driver); else lua_pushnil(L);
    return 1;
}
int dIsVehicle(lua_State* L, Vm& vm) {
    Driving* d = drivingOf(vm);
    Entity e;
    lua_pushboolean(L, d && unitArg(L, vm, 1, e) && d->car(*vm.sim, e) != nullptr);
    return 1;
}

} // namespace

void openDrivingApi(Vm& vm) {
    lua_State* L = vm.L;
    static const luaL_Reg queries[] = {
        {"car_of", bindQ<dCarOf>}, {"speed", bindQ<dSpeed>}, {"driver", bindQ<dDriver>}, {"is_vehicle", bindQ<dIsVehicle>}, {nullptr, nullptr}};
    luaL_newlib(L, queries);
    lua_setglobal(L, "driving");
}

} // namespace aiverse::game::script
