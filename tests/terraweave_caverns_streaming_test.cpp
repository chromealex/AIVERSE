#include "engine/world/TerraWeaveCavernsStreaming.h"
#include "engine/world/TerraWeaveCavernsChartPreview.h"
#include "engine/world/TerraWeaveCavernsShaftMesh.h"
#include <cassert>
#include <iostream>
using namespace aiverse::terraweave::caverns;
int main(){
    for(int cap:{1,100,199,200,201,399,400,401,1000,15000}){
        const auto lattice=caveChartPreviewLattice(cap);
        assert(!lattice.keys.empty()&&lattice.keys.size()<=64);
        const int64_t span=int64_t(lattice.resolution)*lattice.spacingCm;
        assert(span%25==0);
        const auto lo=floorDiv(-cap,span),hi=floorDiv(cap-1,span);
        assert(lattice.keys.contains({lo,lo,lo})&&lattice.keys.contains({hi,hi,hi}));
        assert(caveChartPreviewField(500,{cap+1,0,0},cap)==-1);
        assert(caveChartPreviewField(500,{cap,0,0},cap)==0);
        assert(caveChartPreviewField(-7,{0,0,0},cap)==-7);
    }
    {
        const auto lattice=caveChartPreviewLattice(1000);
        std::set<ChunkKey3,KeyLess> exact{{0,0,0}};
        const auto lods=caveChartPreviewLods(lattice.keys,exact,{100,100,100},{});
        assert(lods.at({0,0,0})==0&&lods.at({-1,-1,-1})==2);
        StreamSettings settings;settings.maxResident=64;settings.maxBuildsPerPump=64;
        CaveWorldResidency residency(settings);
        residency.sourceRevision(1);residency.setExplicitVisualDemand(lattice.keys);
        residency.setExplicitVisualLods(lods);
        const auto tickets=residency.pump();
        assert(tickets.size()==lattice.keys.size());
        for(const auto& ticket:tickets)assert(ticket.lod==lods.at(ticket.key));
        const auto next=caveChartPreviewLods(lattice.keys,exact,{0,0,0},lods);
        residency.setExplicitVisualLods(next);
        for(const auto& ticket:tickets)assert(!residency.current(ticket));
        const auto fresh=residency.pump();
        assert(fresh.size()==lattice.keys.size());
        for(const auto& ticket:fresh)assert(ticket.lod==next.at(ticket.key));
    }
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
