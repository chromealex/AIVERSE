#include "engine/world/TerraWeaveCavernsMultiNav.h"
#include <cassert>
#include <iostream>
using namespace aiverse::terraweave::caverns;
int main(){
    Settings caves;
    caves.cellCm=2400;caves.chamberRadiusCm=1000;
    caves.tunnelRadiusCm=600;caves.chamberChance=1000;caves.tunnelChance=1000;
    caves.minYcm=-10000;caves.maxYcm=-100;
    CaveNavConfig c;
    c.width=4;c.depth=4;c.originXcm=0;c.originZcm=0;c.columnCm=200;
    c.minYcm=-9000;c.maxYcm=-300;
    c.verticalStepCm=100;c.agentHeightCm=150;c.maxSpansPerColumn=32;
    auto terrain=[](int64_t,int64_t){return int64_t(0);};
    CaveNavSource src(caves,c,{},terrain);
    std::vector<nav::Span> spans;
    src.spans(0,0,spans);
    assert(spans.size()<=c.maxSpansPerColumn);
    for(size_t i=1;i<spans.size();++i)
        assert(spans[i-1].y < spans[i].y);
    auto cfg=caveNavMeshConfig(c);
    assert(cfg.width==4 && cfg.depth==4);
    std::cout<<"cave nav source contract OK spans="<<spans.size()<<"\n";
}
