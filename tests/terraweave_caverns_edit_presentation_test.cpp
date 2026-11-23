#include "engine/render/TerraWeaveCavernsEditPresentation.h"
#include <cassert>
#include <iostream>
#include <stdexcept>
using namespace aiverse::terraweave::caverns;
static CavePreparedEdit candidate(ChunkKey3 key,uint64_t sourceRevision,uint64_t renderRevision) {
    CavePreparedEdit item;
    item.dirty={key,sourceRevision,42};
    item.candidate.mesh.key=key;
    item.candidate.mesh.vertices={{0,0,0,0,1,0},
                                  {1,0,0,0,1,0},
                                  {0,0,1,0,1,0}};
    item.candidate.mesh.indices={0,1,2};
    item.candidate.sourceRevision=sourceRevision;
    item.candidate.renderRevision=renderRevision;
    item.candidate.resolution=4;
    item.candidate.spacingCm=100;
    return item;
}
int main() {
    const auto a=candidate({-1,-2,0},10,101);
    const auto b=candidate({0,-2,0},10,102);
    auto prepared=prepareCaveEditPresentation({b,a},3,{8,100,true});
    assert(prepared.models.size()==2);
    assert(prepared.vertexCount==6);
    assert(prepared.triangleCount==2);
    assert(prepared.manifest["instances"].size()==2);
    assert(prepared.manifest["strata"].getBool("enabled"));
    assert(prepared.manifest["instances"][size_t(0)]["position"][size_t(0)].toDouble()==-4.);
    auto bad=a;
    bad.candidate.renderRevision=0;
    bool rejected=false;
    try{(void)prepareCaveEditPresentation({bad},3);}
    catch(const std::invalid_argument&){rejected=true;}
    assert(rejected);
    bool duplicate=false;
    try{(void)prepareCaveEditPresentation({a,a},3);}
    catch(const std::invalid_argument&){duplicate=true;}
    assert(duplicate);
    std::cout<<"caverns stage23 edit presentation preparation OK\n";
}
