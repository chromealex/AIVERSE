#include "engine/world/TerraWeaveCavernsNav.h"
#include "engine/render/TerraWeaveCavernsStrata.h"
#include <cassert>
#include <iostream>
using namespace aiverse::terraweave::caverns;
int main(){
    Entrance e{91,500,500,0,-1000,200};
    terraweave::EarthworksNavConfig c;c.width=10;c.depth=10;
    c.originXcm=0;c.originZcm=0;c.cellCm=100;
    auto blocked=surfaceEntranceBlocked(c,{e});
    assert(blocked(5,5));
    assert(!blocked(0,0));
    auto combined=surfaceEntranceBlocked(c,{e},[](int32_t x,int32_t z){return x==0&&z==0;});
    assert(combined(0,0));
    assert(combined(5,5));
    auto lease=planCaveStrataLease({-1,0,2},1,13,27,true,true,true,true);
    assert(lease.route==CaveGeometryRoute::StrataEligible);
    assert(caveStrataLeaseCurrent(lease,1,13,27));
    assert(!caveStrataLeaseCurrent(lease,1,14,27));
    auto fallback=planCaveStrataLease({0,0,0},1,2,3,false,true,true,true);
    assert(fallback.route==CaveGeometryRoute::Renderer3DFallback);
    std::cout<<"caverns stage6 contracts OK\n";
}
