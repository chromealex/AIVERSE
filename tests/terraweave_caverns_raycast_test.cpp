#include "engine/world/TerraWeaveCavernsRaycast.h"
#include <cassert>
#include <iostream>
using namespace aiverse::terraweave::caverns;
int main(){
    CaveVolumeSnapshot volume;
    volume.settings.chamberChance=0;
    volume.settings.tunnelChance=0;
    volume.settings.minYcm=-10000;
    volume.settings.maxYcm=-100;
    volume.stamps.push_back({1,{0,-3000,0},500,CaveStampMode::Carve});
    canonicalizeCaveSnapshot(volume);
    const CaveRayCm ray{{-1000,-3000,0},1<<20,0,0};
    const auto hit=raycastCaveSurface(volume,ray,2000,25);
    assert(hit);
    assert(hit->enteringVoid);
    assert(hit->distanceCm>=490 && hit->distanceCm<=510);
    assert(hit->point.y==-3000);
    const auto reverse=raycastCaveSurface(volume,{{0,-3000,0},1<<20,0,0},2000,25);
    assert(reverse && !reverse->enteringVoid);
    const auto miss=raycastCaveSurface(volume,{{-1000,-3000,2000},1<<20,0,0},2000,25);
    assert(!miss);
    auto changed=dirtyBoundsForStamp(volume.stamps.front(),100);
    auto chunks=dirtyCaveChunks(changed,16,100);
    assert(!chunks.empty());
    bool negative=false,positive=false;
    for(const auto& k:chunks) {
        negative|=k.x<0;positive|=k.x>=0;
    }
    assert(negative && positive);
    bool bad=false;
    try{(void)raycastCaveSurface(volume,{{0,0,0},0,0,0});}
    catch(const std::invalid_argument&){bad=true;}
    assert(bad);
    std::cout<<"caverns stage20 SDF ray and dirty bounds OK\n";
}
