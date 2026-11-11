#include <aiverselib/bots/NavMeshData.h>
#include <aiverselib/bots/Fps.h>
#include <iostream>
#include <stdexcept>
using namespace aiverse;using namespace aiverse::lib::bots;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #x);}while(false)
world::SpatialWorld floorWorld(){world::SpatialWorld w;w.add({1,{{-20_fx,-1_fx,-20_fx},{20_fx,0_fx,20_fx}}});return w;}
NavBuildConfig config(){NavBuildConfig c;c.bounds={{-10_fx,-1_fx,-10_fx},{10_fx,6_fx,10_fx}};c.cellSize=1_fx;return c;}
int main(){try{
    auto w=floorWorld();auto c=config();auto first=buildNavMesh(w,c);CHECK(first.mesh&&first.mesh->polygons.size()==400);
    CHECK(navMeshJson(*first.mesh).dump(-1)==navMeshJson(*buildNavMesh(w,c).mesh).dump(-1));
    CHECK(navPath(*first.mesh,w,{-5_fx,Fixed::fromRatio(1,100),0_fx},{5_fx,Fixed::fromRatio(1,100),0_fx}));
    // Wall with an open end: path goes around, rather than through the collision.
    w.add({2,{{-1_fx,0_fx,-6_fx},{1_fx,4_fx,6_fx}}});CHECK(!navPath(*first.mesh,w,{-5_fx,{},{}},{5_fx,{},{}}));
    auto around=buildNavMesh(w,c);CHECK(around.mesh);auto path=navPath(*around.mesh,w,{-5_fx,Fixed::fromRatio(1,100),0_fx},{5_fx,Fixed::fromRatio(1,100),0_fx});CHECK(path);
    bool detour=false;for(auto p:*path)if(fxAbs(p.z)>6_fx)detour=true;CHECK(detour);
    CHECK(!navPath(*around.mesh,w,{-5_fx,{},{}},{5_fx,{},{}},1));
    // Real motor follows generated cell corridors to an objective, with no hand-authored waypoints.
    auto rules=Json::parse(R"({"fps":{"roundTicks":10000}})");auto map=Json::parse(R"({"players":[{"name":"A","team":1},{"name":"B","team":1}],"spawns":[[-5,0.01,0],[-8,0.01,-8]],"colliders":[{"id":1,"min":[-20,-1,-20],"max":[20,0,20]},{"id":2,"min":[-1,0,-6],"max":[1,4,6]}]})");
    Simulation sim({1,60,0},std::make_unique<fps::Module>());sim.setup(rules,map);auto& module=static_cast<fps::Module&>(sim.module());auto mesh=buildNavMesh(module.collision(),c);CHECK(mesh.mesh);FpsState state;FpsConfig bot;
    for(int i=0;i<900;++i){auto cmd=fpsCommand(sim,module,1,bot,{},state,Vec3{5_fx,Fixed::fromRatio(1,100),0_fx},&*mesh.mesh);CHECK(cmd);sim.submit(*cmd);sim.step();}
    CHECK((module.state(sim).actors[0].pos-Vec3{5_fx,Fixed::fromRatio(1,100),0_fx}).xz().length()<1_fx);
    // Disconnected platforms must not be bridged by a clear horizontal sweep.
    world::SpatialWorld gap;gap.add({1,{{-10_fx,-1_fx,-10_fx},{-1_fx,0_fx,10_fx}}});gap.add({2,{{1_fx,-1_fx,-10_fx},{10_fx,0_fx,10_fx}}});
    auto islands=buildNavMesh(gap,c);CHECK(islands.mesh&&!navPath(*islands.mesh,gap,{-5_fx,{},{}},{5_fx,{},{}}));
    // Agent erosion closes a corridor narrower than its diameter; reducing cell size cannot reopen it.
    auto narrow=floorWorld();narrow.add({2,{{-4_fx,0_fx,-10_fx},{Fixed::fromRatio(-2,10),4_fx,10_fx}}});narrow.add({3,{{Fixed::fromRatio(2,10),0_fx,-10_fx},{4_fx,4_fx,10_fx}}});
    auto small=c;small.cellSize=Fixed::fromRatio(1,4);auto corridor=buildNavMesh(narrow,small);CHECK(corridor.mesh&&!navPolygonAt(*corridor.mesh,{0_fx,{},0_fx}));
    // Low ceilings remove lower floor, while stacked walkable tops remain a distinct layer.
    auto ceiling=floorWorld();ceiling.add({2,{{-4_fx,1_fx,-4_fx},{4_fx,2_fx,4_fx}}});auto layers=buildNavMesh(ceiling,c);CHECK(layers.mesh&&!navPolygonAt(*layers.mesh,{0_fx,{},0_fx})&&navPolygonAt(*layers.mesh,{0_fx,2_fx,0_fx}));
    auto stairs=floorWorld();stairs.add({2,{{0_fx,0_fx,-10_fx},{10_fx,Fixed::fromRatio(2,10),10_fx}}});auto stepped=buildNavMesh(stairs,c);CHECK(stepped.mesh&&navPath(*stepped.mesh,stairs,{-5_fx,{},{}},{5_fx,Fixed::fromRatio(2,10),{}}));
    stairs.remove(2);stairs.add({2,{{0_fx,0_fx,-20_fx},{20_fx,2_fx,20_fx}}});auto cliff=buildNavMesh(stairs,c);CHECK(cliff.mesh&&!navPath(*cliff.mesh,stairs,{-5_fx,{},{}},{5_fx,2_fx,{}}));
    auto budget=c;budget.maxCells=10;CHECK(!buildNavMesh(w,budget).mesh);
    CHECK(!navPolygonAt(*around.mesh,{100_fx,{},100_fx}));
    std::cout<<"Automatic navmesh: deterministic bake, motor wall detour, gaps, clearance, steps, layers, stale geometry and budgets: OK\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
