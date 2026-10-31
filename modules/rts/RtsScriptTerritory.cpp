// Generic flat-map territory brushes and deterministic scripted formation placement.
#include "RtsScriptInternal.h"
#include "RtsInternal.h"
#include <algorithm>

namespace rts::game::script {
namespace {
bool flatGrid(Vm& vm) {
    if (vm.module->state(*vm.sim).grid.surf) { vm.err = "territory brush/formation API requires a flat map"; return false; }
    return true;
}
bool player(lua_State* L, Vm& vm, int arg, uint8_t& out) {
    int64_t p;
    if (!argInt(L, vm, arg, p, "player")) return false;
    if (p < 0 || p > 254 || (p && !vm.sim->player(uint8_t(p)))) { vm.err = "unknown territory player"; return false; }
    out = uint8_t(p); return true;
}
void field(lua_State* L, const char* key, int64_t value) { lua_pushinteger(L, value); lua_setfield(L, -2, key); }
void field(lua_State* L, const char* key, Fixed value) { pushFx(L, value); lua_setfield(L, -2, key); }
int safeCell(const MapGrid& g, Vec3 p) {
    if (p.x < Fixed{} || p.z < Fixed{} || p.x >= Fixed::fromInt(g.w) || p.z >= Fixed::fromInt(g.h)) return -1;
    return g.cellOf(p);
}
int territoryAt(lua_State* L, Vm& vm) {
    Vec3 p{};
    if (!flatGrid(vm) || !argFx(L, vm, 1, p.x, "x") || !argFx(L, vm, 2, p.z, "z")) return -1;
    const auto& g = vm.module->state(*vm.sim).grid;
    const int c = safeCell(g,p);
    if (c < 0 || size_t(c) >= g.territory.size() || !(g.flags[size_t(c)] & kCellGround)) lua_pushnil(L);
    else lua_pushinteger(L, g.territory[size_t(c)]);
    return 1;
}
int orderTarget(lua_State* L, Vm& vm) {
    Entity e; auto& w = vm.sim->world();
    if (argEntity(L,1,e) && w.alive(e)) {
        if (auto* orders=w.tryGet<Orders>(e); orders && !orders->queue.empty()) {
            const auto& o=orders->queue.front();
            if (o.type==OrderType::Move || o.type==OrderType::AttackMove) {
                pushFx(L,o.point.x); pushFx(L,o.point.z); return 2;
            }
        }
    }
    lua_pushnil(L); return 1;
}
int territoryCount(lua_State* L, Vm& vm) {
    uint8_t p;
    if (!player(L, vm, 1, p)) return -1;
    lua_pushinteger(L, vm.module->territoryOf(*vm.sim, p)); return 1;
}
int frontier(lua_State* L, Vm& vm) {
    uint8_t p;
    if (!flatGrid(vm) || !player(L, vm, 1, p)) return -1;
    const auto& g = vm.module->state(*vm.sim).grid;
    lua_newtable(L);
    int n = 0;
    static constexpr int dx[] = {1, 0, -1, 0}, dz[] = {0, 1, 0, -1};
    // Sorted by cell then edge; each record is a unit-length land frontier segment.
    for (int z = 0; z < g.h; ++z) for (int x = 0; x < g.w; ++x) {
        const size_t c = size_t(g.index(x, z));
        if (g.territory[c] != p || !(g.flags[c] & kCellGround)) continue;
        for (int d = 0; d < 4; ++d) {
            const int xx = x + dx[d], zz = z + dz[d];
            if (!g.inside(xx, zz)) continue;
            const size_t next = size_t(g.index(xx, zz));
            if (!(g.flags[next] & kCellGround) || g.territory[next] == p) continue;
            lua_createtable(L, 0, 6);
            field(L, "x", Fixed::fromRatio(2 * x + 1 + dx[d], 2));
            field(L, "z", Fixed::fromRatio(2 * z + 1 + dz[d], 2));
            field(L, "nx", dx[d]); field(L, "nz", dz[d]);
            field(L, "owner", g.territory[next]); field(L, "cell", int64_t(next));
            lua_rawseti(L, -2, ++n);
        }
    }
    return 1;
}
int paint(lua_State* L, Vm& vm) {
    uint8_t owner; Vec3 p{}; Fixed radius;
    if (!flatGrid(vm) || !player(L, vm, 1, owner) || !argFx(L, vm, 2, p.x, "x") ||
        !argFx(L, vm, 3, p.z, "z") || !argFx(L, vm, 4, radius, "radius")) return -1;
    auto& m = *vm.module; auto& g = m.state(*vm.sim).grid;
    if (!m.rules().territory.enabled) { vm.err = "paint_territory requires rules.territory"; return -1; }
    // Bound before squaring or converting to cell coordinates (untrusted script arguments).
    if (radius < Fixed{} || radius > Fixed::fromInt(64)) { vm.err = "territory brush radius must be between 0 and 64"; return -1; }
    if (safeCell(g,p) < 0) { lua_pushinteger(L, 0); return 1; }
    const bool connected = lua_toboolean(L, 5);
    const int r = int(radius.floorInt()) + 1, cx = g.cellX(p.x), cz = g.cellY(p.z);
    std::vector<int> cells;
    for (int z = std::max(0, cz-r); z <= std::min(g.h-1, cz+r); ++z)
        for (int x = std::max(0, cx-r); x <= std::min(g.w-1, cx+r); ++x) {
            const int c = g.index(x, z);
            if (!(g.flags[size_t(c)] & kCellGround) || g.territory[size_t(c)] == owner) continue;
            if ((g.cellCenter(x,z)-p).xz().lengthSq() > radius*radius) continue;
            if (connected) {
                bool touch = false;
                for (auto [a,b] : {std::pair{x-1,z}, {x+1,z}, {x,z-1}, {x,z+1}})
                    if (g.inside(a,b) && g.territory[size_t(g.index(a,b))] == owner) touch = true;
                if (!touch) continue;
            }
            cells.push_back(c);
        }
    for (int c : cells) m.setTerritory(*vm.sim, c, owner);
    lua_pushinteger(L, lua_Integer(cells.size())); return 1;
}
int setPosition(lua_State* L, Vm& vm) {
    Entity e; Vec3 p{};
    if (!flatGrid(vm)) return -1;
    auto& w = vm.sim->world();
    if (!argEntity(L, 1, e) || !w.alive(e) || !w.has<Mobile>(e) || !w.has<Unit>(e) || !w.has<Transform>(e)) { lua_pushboolean(L, false); return 1; }
    if (!argFx(L, vm, 2, p.x, "x") || !argFx(L, vm, 3, p.z, "z")) return -1;
    auto& m = *vm.module; const auto& g = m.state(*vm.sim).grid;
    const int c = safeCell(g,p);
    const auto& unit = w.get<Unit>(e);
    const bool air = (m.rules().types[unit.type].layer & kLayerAir) != 0;
    if (c < 0 || (!air && !(g.flags[size_t(c)] & kCellGround)) || unit.hp <= Fixed{} ||
        (unit.flags & (kUnitOffMap | kUnitUnderConstruction))) { lua_pushboolean(L, false); return 1; }
    auto& tr = w.get<Transform>(e);
    const Vec3 delta{p.x-tr.pos.x, Fixed{}, p.z-tr.pos.z};
    if (delta.x.raw || delta.z.raw) w.get<Unit>(e).facing = delta / delta.length();
    p.y = m.groundHeight(*vm.sim, p);
    tr.pos = p; // preserve prevPos for render interpolation, and stop native locomotion
    w.get<Mobile>(e) = Mobile{};
    if (auto* orders = w.tryGet<Orders>(e)) orders->queue.clear();
    lua_pushboolean(L, true); return 1;
}
}
void openTerritoryApi(Vm& vm) {
    static const luaL_Reg queries[] = {{"order_target", bind<orderTarget>}, {"territory_at", bind<territoryAt>}, {"territory_count", bind<territoryCount>},
        {"frontier", bind<frontier>}, {nullptr, nullptr}};
    static const luaL_Reg actions[] = {{"paint_territory", bind<paint>}, {"set_position", bind<setPosition>}, {nullptr, nullptr}};
    luaL_setfuncs(vm.L, queries, 0);
    if (vm.mode == Mode::Sim) luaL_setfuncs(vm.L, actions, 0);
}
} // namespace rts::game::script
