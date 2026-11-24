#include "engine/world/TerraWeaveCavernsEntrances.h"
#include <cassert>
#include <algorithm>
#include <iostream>
using namespace aiverse::terraweave::caverns;
int main() {
    Settings caves;
    caves.cellCm=2000;caves.chamberRadiusCm=900;caves.tunnelRadiusCm=400;
    caves.chamberChance=1000;caves.tunnelChance=1000;
    caves.minYcm=-30000;caves.maxYcm=-1000;
    EntranceSettings cfg;
    cfg.spacingCm=5000;cfg.radiusCm=500;cfg.chancePermille=1000;
    cfg.maxDepthCm=18000;cfg.depthStepCm=100;
    const auto flat=[](int64_t,int64_t){return int64_t(0);};
    auto all=generateEntrances(caves,cfg,{-10000,-10000,10000,10000},flat);
    auto a=generateEntrances(caves,cfg,{-10000,-10000,0,10000},flat);
    auto b=generateEntrances(caves,cfg,{0,-10000,10000,10000},flat);
    a.insert(a.end(),b.begin(),b.end());
    auto byId=[](const Entrance& l,const Entrance& r){return l.id<r.id;};
    std::sort(all.begin(),all.end(),byId);
    std::sort(a.begin(),a.end(),byId);
    assert(all==a);
    assert(!all.empty());
    for(const auto& e:all) {
        assert(e.caveYcm<e.surfaceYcm);
        assert(surfaceHoleAt(all,e.xCm,e.zCm));
        assert(combinedFieldCm(caves,all,{e.xCm,e.surfaceYcm,e.zCm})>0);
        assert(caveFieldCm(caves,{e.xCm,e.caveYcm,e.zCm})>0);
    }
    auto excluded=generateEntrances(caves,cfg,{-10000,-10000,10000,10000},flat,
        [](int64_t x,int64_t){return x<0;});
    for(const auto& e:excluded)assert(e.xCm>=0);
    EntranceSettings disabled=cfg;disabled.chancePermille=0;
    assert(generateEntrances(caves,disabled,{-10000,-10000,10000,10000},flat).empty());
    auto mesh=generateCaveMeshWithEntrances(caves,all,{0,-1,0},8,200);
    assert(mesh.indices.size()%3==0);
    std::cout<<"caverns entrance contract OK count="<<all.size()<<"\n";
}
