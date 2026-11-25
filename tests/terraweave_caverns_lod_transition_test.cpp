#include "engine/world/TerraWeaveCavernsLodTransition.h"
#include <cassert>
#include <iostream>
using namespace aiverse::terraweave::caverns;
static CaveMesh coarse(unsigned axis){
    CaveMesh m; m.key={0,0,0};
    auto v=[&](float a,float b,float c){
        MeshVertex p;p.x=a;p.y=b;p.z=c;return p;
    };
    // Shared plane is +axis at world coordinate 400 cm.
    if(axis==0)m.vertices={v(4,0,0),v(4,4,0),v(3,0,0)};
    if(axis==1)m.vertices={v(0,4,0),v(4,4,0),v(0,3,0)};
    if(axis==2)m.vertices={v(0,0,4),v(4,0,4),v(0,0,3)};
    m.indices={0,1,2};return m;
}
static CaveMesh fine(unsigned axis,bool divergent=false){
    CaveMesh m;m.key={axis==0?1:0,axis==1?1:0,axis==2?1:0};
    auto v=[&](float a,float b,float c){
        MeshVertex p;p.x=a;p.y=b;p.z=c;return p;
    };
    if(axis==0)m.vertices={v(0,0,0),v(0,2,divergent?.25f:0),v(0,4,0),v(1,0,0)};
    if(axis==1)m.vertices={v(0,0,0),v(2,0,divergent?.25f:0),v(4,0,0),v(0,1,0)};
    if(axis==2)m.vertices={v(0,0,0),v(2,0,divergent?.25f:0),v(4,0,0),v(0,0,1)};
    m.indices={0,1,3,1,2,3};
    return m;
}
int main(){
    for(unsigned axis=0;axis<3;++axis){
        const auto c=coarse(axis),f=fine(axis);
        const auto stitched=stitchCaveCollinearBoundary(c,2,200,f,4,100,axis);
        assert(stitched.supported && stitched.splitTriangles==1);
        assert(stitched.coarseStitched.indices.size()==6);
        const auto unchanged=stitchCaveCollinearBoundary(c,2,200,fine(axis,true),4,100,axis);
        assert(!unchanged.supported);
        assert(unchanged.coarseStitched.indices==c.indices);
        auto invalid=f;
        invalid.key={-1,-1,-1};
        bool rejected=false;
        try{(void)stitchCaveCollinearBoundary(c,2,200,invalid,4,100,axis);}
        catch(const std::invalid_argument&){rejected=true;}
        assert(rejected);
    }
    std::cout<<"mixed-LOD collinear cave seam contract OK\n";
}
