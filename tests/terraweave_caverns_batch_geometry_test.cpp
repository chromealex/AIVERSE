#include "engine/render/TerraWeaveCavernsBatchGeometry.h"
#include <cassert>
#include <iostream>
using namespace aiverse::terraweave::caverns;
static CaveBatchChunk make(ChunkKey3 k,uint64_t revision=1){
    CaveBatchChunk c;c.mesh.key=k;c.revision=revision;c.resolution=4;c.spacingCm=100;
    c.mesh.vertices={{0,0,0,0,1,0},{1,0,0,0,1,0},{0,0,1,0,1,0}};
    c.mesh.indices={0,1,2};
    return c;
}
int main(){
    auto first=prepareCaveBatch({make({1,0,0}),make({-1,-1,0})}, {8,100,true});
    auto second=prepareCaveBatch({make({-1,-1,0}),make({1,0,0})}, {8,100,true});
    assert(first.vertexCount==6 && first.triangleCount==2);
    assert(first.models.size()==2);
    assert(first.manifest==second.manifest); // independent of job completion order
    assert(first.manifest["instances"].size()==2);
    assert(first.manifest["instances"][size_t(0)]["position"][size_t(0)].toDouble()==-4.0);
    assert(first.manifest["instances"][size_t(0)]["position"][size_t(1)].toDouble()==-4.0);
    assert(first.manifest["strata"].getBool("enabled"));
    bool duplicate=false;
    try {(void)prepareCaveBatch({make({1,0,0}),make({1,0,0})});}
    catch(const std::invalid_argument&){duplicate=true;}
    assert(duplicate);
    bool budget=false;
    try {(void)prepareCaveBatch({make({1,0,0}),make({2,0,0})},{8,5,false});}
    catch(const std::length_error&){budget=true;}
    assert(budget);
    CaveBatchChunk empty;empty.mesh.key={3,0,0};
    const auto mixed=prepareCaveBatch({make({1,0,0}),empty},{8,100,false});
    assert(mixed.models.size()==1);
    assert(mixed.manifest["instances"].size()==1);
    std::cout<<"caverns batch manifest OK\n";
}
