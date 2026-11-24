#include "engine/world/TerraWeaveCavernsStreaming.h"
#include "engine/world/TerraWeaveCavernsShaftMesh.h"
#include <cassert>
#include <iostream>
using namespace aiverse::terraweave::caverns;
int main(){
    CaveStreamPlanner planner({1,0,9,3,2});
    planner.setCenter({0,0,0});
    auto actions=planner.pump();
    assert(actions.size()==3);
    assert(actions.front().action==StreamAction::Build);
    const auto stale=actions.front();
    planner.setSourceRevision(8);
    assert(!planner.accept(stale,8));
    actions=planner.pump();
    assert(!actions.empty());
    for(const auto& req:actions)if(req.action==StreamAction::Build)
        assert(planner.accept(req,8));
    planner.setCenter({100,0,0});
    actions=planner.pump();
    unsigned retires=0,builds=0;
    for(const auto& r:actions){
        retires+=r.action==StreamAction::Retire;
        builds+=r.action==StreamAction::Build;
    }
    assert(retires<=2 && builds<=3);
    Entrance e{42,0,0,0,-1500,300};
    const auto mesh=makeEntranceShaftSurface(e,[](int64_t,int64_t){return int64_t(0);},16,4);
    assert(mesh.vertices.size()==16*5);
    assert(mesh.indices.size()==16*4*6);
    for(uint32_t i=0;i<16;++i){
        assert(mesh.vertices[i].y==0);
        assert(mesh.vertices[64+i].y==-1500);
    }
    std::cout<<"streaming and shaft contracts OK\n";
}
