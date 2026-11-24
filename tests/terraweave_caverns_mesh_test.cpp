#include "engine/world/TerraWeaveCavernsMesh.h"
#include <cassert>
#include <iostream>
using namespace aiverse::terraweave::caverns;
int main(){
    Settings s;s.cellCm=2000;s.tunnelRadiusCm=500;s.chamberRadiusCm=850;
    s.chamberChance=1000;s.tunnelChance=1000;s.minYcm=-30000;s.maxYcm=-100;
    auto a=generateCaveMesh(s,{0,-1,0},12,200);
    auto b=generateCaveMesh(s,{0,-1,0},12,200);
    assert(!a.vertices.empty());assert(a.vertices.size()==b.vertices.size());
    assert(a.indices==b.indices);
    for(size_t i=0;i<a.vertices.size();++i){
        assert(a.vertices[i].x==b.vertices[i].x);
        assert(a.vertices[i].y==b.vertices[i].y);
        assert(a.vertices[i].z==b.vertices[i].z);
    }
    assert(a.indices.size()%3==0);
    assert(caveFieldCm(s,{0,100,0})<0);
    s.chamberChance=0;s.tunnelChance=0;
    assert(generateCaveMesh(s,{0,-1,0},8,200).indices.empty());
    std::cout<<"caverns mesh OK triangles="<<a.indices.size()/3<<"\n";
}
