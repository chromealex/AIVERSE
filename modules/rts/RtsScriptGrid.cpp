// Pure grid queries for scripted navigation. No world mutation or persistent cache.
#include "RtsScriptInternal.h"
#include <algorithm>
#include <cstdlib>
#include <limits>
#include <queue>

namespace rts::game::script {
namespace {
int gridPath(lua_State* L, Vm& vm) {
    int64_t width, height, start, goal, label = 0;
    if (!argInt(L, vm, 1, width, "width") || !argInt(L, vm, 2, height, "height") ||
        !argInt(L, vm, 4, start, "start") || !argInt(L, vm, 5, goal, "goal")) return -1;
    if (width < 1 || height < 1 || width > 4096 || height > 4096 || width * height > 262144)
        return fail(vm, "grid_path: dimensions must fit 262144 cells (each axis 1..4096)");
    if (!lua_istable(L, 3)) return fail(vm, "grid_path: cells must be a table");
    const bool labeled = !lua_isnoneornil(L, 6);
    if (labeled && !argInt(L, vm, 6, label, "label")) return -1;
    const int w = int(width), count = int(width * height);
    if (start < 0 || goal < 0 || start >= count || goal >= count) { lua_pushnil(L); return 1; }
    auto passable = [&](int c) {
        lua_rawgeti(L, 3, c);
        const bool yes = labeled ? (lua_isinteger(L, -1) && lua_tointeger(L, -1) == label)
                                 : (lua_isboolean(L, -1) && lua_toboolean(L, -1));
        lua_pop(L, 1);
        return yes;
    };
    // Allow a unit to leave a newly blocked start, as long as later cells are legal.
    if (start == goal) { lua_newtable(L); return 1; }
    if (!passable(int(goal))) { lua_pushnil(L); return 1; }
    struct Node { int cell, cost, remaining; };
    struct Later {
        bool operator()(const Node& a, const Node& b) const {
            if (a.cost + a.remaining != b.cost + b.remaining)
                return a.cost + a.remaining > b.cost + b.remaining;
            if (a.remaining != b.remaining) return a.remaining > b.remaining;
            return a.cell > b.cell;
        }
    };
    auto remaining = [&](int c) { return std::abs(c % w - int(goal) % w) + std::abs(c / w - int(goal) / w); };
    std::vector<int> costs(size_t(count), std::numeric_limits<int>::max());
    std::vector<int> previous(size_t(count), -1);
    std::priority_queue<Node, std::vector<Node>, Later> open;
    costs[size_t(start)] = 0;
    open.push({int(start), 0, remaining(int(start))});
    while (!open.empty()) {
        const Node n = open.top(); open.pop();
        if (n.cost != costs[size_t(n.cell)]) continue;
        if (n.cell == goal) {
            std::vector<int> path;
            for (int c = int(goal); c != start; c = previous[size_t(c)]) path.push_back(c);
            lua_createtable(L, int(path.size()), 0);
            int i = 1;
            for (auto it = path.rbegin(); it != path.rend(); ++it) { lua_pushinteger(L, *it); lua_rawseti(L, -2, i++); }
            return 1;
        }
        const int x = n.cell % w;
        const int neighbors[] = {x > 0 ? n.cell - 1 : -1, x + 1 < w ? n.cell + 1 : -1,
                                 n.cell >= w ? n.cell - w : -1, n.cell + w < count ? n.cell + w : -1};
        for (int c : neighbors) {
            if (c < 0 || n.cost + 1 >= costs[size_t(c)] || !passable(c)) continue;
            costs[size_t(c)] = n.cost + 1; previous[size_t(c)] = n.cell;
            open.push({c, n.cost + 1, remaining(c)});
        }
    }
    lua_pushnil(L); return 1;
}
// Bounded fixed-point sampling for scripted steering/visibility masks. The five
// probes are a cross, not a swept-circle collision solver.
int gridSampleSegment(lua_State* L, Vm& vm) {
    int64_t width, height, label=0, density=8;
    Fixed x,z,tx,tz,margin;
    if (!argInt(L,vm,1,width,"width") || !argInt(L,vm,2,height,"height") ||
        !argFx(L,vm,4,x,"x") || !argFx(L,vm,5,z,"z") ||
        !argFx(L,vm,6,tx,"target x") || !argFx(L,vm,7,tz,"target z") ||
        !optFx(L,vm,9,margin,Fixed{},"margin")) return -1;
    if (width<1 || height<1 || width>4096 || height>4096 || width*height>262144)
        return fail(vm,"grid_sample_segment: dimensions must fit 262144 cells (each axis 1..4096)");
    if (!lua_istable(L,3)) return fail(vm,"grid_sample_segment: cells must be a table");
    const bool labeled=!lua_isnoneornil(L,8);
    if (labeled && !argInt(L,vm,8,label,"label")) return -1;
    if (!lua_isnoneornil(L,10) && !argInt(L,vm,10,density,"samples per cell")) return -1;
    if (margin<Fixed{} || margin>Fixed::fromInt(4096) || density<1 || density>16)
        return fail(vm,"grid_sample_segment: margin must be 0..4096, density 1..16");
    auto inBounds=[&](Fixed px,Fixed pz) {
        return px>=Fixed{} && pz>=Fixed{} && px<Fixed::fromInt(width) && pz<Fixed::fromInt(height);
    };
    // Bound coordinates before differences/products, including hostile fx.raw inputs.
    if (!inBounds(x,z) || !inBounds(tx,tz)) { lua_pushboolean(L,false);return 1; }
    const Fixed dx=tx-x,dz=tz-z;
    const int64_t steps=(fxSqrt(dx*dx+dz*dz)*density).floorInt()+1;
    if (steps>65536) return fail(vm,"grid_sample_segment: sample limit exceeded (65536)");
    auto own=[&](Fixed px,Fixed pz) {
        if (!inBounds(px,pz)) return false;
        lua_rawgeti(L,3,pz.floorInt()*width+px.floorInt());
        const bool yes=labeled ? (lua_isinteger(L,-1) && lua_tointeger(L,-1)==label)
                               : (lua_isboolean(L,-1) && lua_toboolean(L,-1));
        lua_pop(L,1);return yes;
    };
    for (int64_t i=1;i<=steps;++i) {
        // Match fx(i,steps): division truncates, unlike fromRatio rounding.
        const Fixed fraction=Fixed::fromInt(i)/Fixed::fromInt(steps);
        const Fixed px=x+dx*fraction,pz=z+dz*fraction;
        if (!own(px,pz) || (margin>Fixed{} &&
            (!own(px-margin,pz) || !own(px+margin,pz) || !own(px,pz-margin) || !own(px,pz+margin)))) {
            lua_pushboolean(L,false);return 1;
        }
    }
    lua_pushboolean(L,true);return 1;
}
}
void openGridApi(Vm& vm) {
    lua_pushcfunction(vm.L, bind<gridPath>);
    lua_setfield(vm.L, -2, "grid_path");
    lua_pushcfunction(vm.L, bind<gridSampleSegment>);
    lua_setfield(vm.L, -2, "grid_sample_segment");
}
}
