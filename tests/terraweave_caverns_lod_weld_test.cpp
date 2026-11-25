#include "engine/world/TerraWeaveCavernsLodWeld.h"
#include <cassert>
#include <iostream>
using namespace aiverse::terraweave::caverns;
int main(){
    CaveMesh original; original.key={0,0,0};
    original.vertices={{0,0,0,0,1,0},{1,0,0,0,1,0},{0,1,0,0,1,0},
                       {0,0,0,0,1,0},{1,0,0,0,1,0},{0,1,0,0,1,0}};
    original.indices={0,1,2,3,4,5,0,3,1};
    const auto welded=weldCaveMesh(original);
    assert(welded.mesh.vertices.size()==3);
    assert(welded.mesh.indices.size()==6);
    assert(welded.merged==3 && welded.degenerates==1);
    const auto second=weldCaveMesh(original);
    assert(welded.mesh.indices==second.mesh.indices);
    for(size_t i=0;i<welded.mesh.vertices.size();++i)
        assert(welded.mesh.vertices[i].ny==second.mesh.vertices[i].ny);
    std::vector<CaveLodEntry> lods={
        {{-1,0,0},8,200},{{0,0,0},16,100},{{1,0,0},8,200},
        {{10,0,0},8,200}
    };
    const auto planned=planCaveSeamSafeLod(lods);
    assert(planned.promoted==2);
    assert(planned.entries.size()==4);
    for(const auto& e:planned.entries){
        if(e.key.x<10)assert(e.resolution==16 && e.spacingCm==100);
        else assert(e.resolution==8 && e.spacingCm==200);
    }
    bool duplicate=false;
    try{(void)planCaveSeamSafeLod({{{0,0,0},8,200},{{0,0,0},8,200}});}
    catch(const std::invalid_argument&){duplicate=true;}
    assert(duplicate);
    bool invalidIndex=false;
    try{auto bad=original;bad.indices[0]=999;(void)weldCaveMesh(bad);}
    catch(const std::invalid_argument&){invalidIndex=true;}
    assert(invalidIndex);
    std::cout<<"caverns stage13 weld and LOD planner OK\n";
}
