// Lua bindings of authored routes and the multiscreen view (RtsRoutes.cpp, docs/scripting.md):
// game.screen(), game.on_screen(x, z), game.set_screen(x, z), game.can_send(a, b), game.route_length(a, b).
#include "RtsScriptInternal.h"
#include "RtsInternal.h"

namespace aiverse::game::script {
namespace {
int screen(lua_State* L, Vm& vm) {
    const ScreenDef& s = vm.module->screen();
    if (!s.enabled) { lua_pushnil(L); return 1; }
    const Vec3 c = vm.module->screenCenter(*vm.sim);
    pushFx(L, c.x); pushFx(L, c.z); pushFx(L, s.size.x); pushFx(L, s.size.y);
    return 4;
}
int onScreen(lua_State* L, Vm& vm) {
    Vec3 p{};
    if (!argFx(L, vm, 1, p.x, "x") || !argFx(L, vm, 2, p.z, "z")) return -1;
    lua_pushboolean(L, vm.module->onScreen(*vm.sim, p));
    return 1;
}
int setScreen(lua_State* L, Vm& vm) {
    Vec3 p{};
    if (!argFx(L, vm, 1, p.x, "x") || !argFx(L, vm, 2, p.z, "z")) return -1;
    if (!vm.module->screen().enabled) return fail(vm, "set_screen: the map has no \"screen\"");
    vm.module->setScreenCenter(*vm.sim, p);
    return 0;
}
int canSend(lua_State* L, Vm& vm) {
    Entity a, b;
    if (!argEntity(L, 1, a) || !argEntity(L, 2, b)) return fail(vm, "can_send: expected two entities");
    auto& w = vm.sim->world();
    lua_pushboolean(L, w.alive(a) && w.alive(b) && w.has<Garrison>(a) && w.has<Garrison>(b) && vm.module->canSend(*vm.sim, a, b));
    return 1;
}
int routeLength(lua_State* L, Vm& vm) {
    Entity a, b;
    if (!argEntity(L, 1, a) || !argEntity(L, 2, b)) return fail(vm, "route_length: expected two entities");
    const Fixed l = vm.module->routeLength(*vm.sim, a, b);
    if (l.raw < 0) lua_pushnil(L); else pushFx(L, l);
    return 1;
}
} // namespace

void openRoutesApi(Vm& vm) {
    static const luaL_Reg queries[] = {{"screen", bindQ<screen>}, {"on_screen", bindQ<onScreen>}, {"can_send", bindQ<canSend>},
                                       {"route_length", bindQ<routeLength>}, {nullptr, nullptr}};
    static const luaL_Reg actions[] = {{"set_screen", bind<setScreen>}, {nullptr, nullptr}};
    luaL_setfuncs(vm.L, queries, 0);
    if (vm.mode == Mode::Sim) luaL_setfuncs(vm.L, actions, 0);
}

} // namespace aiverse::game::script
