#include "engine/world/TerraWeaveCavernsSurfaceCut.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace aiverse::terraweave::caverns;
int main() {
    // Triangle pair representing one 20x20 metre native terrain tile.
    aiverse::terraweave::TerrainPatch tile;
    tile.cell={0,0,0}; tile.resolution=2;tile.cellSizeMetres=20;
    const auto p=[](int x,int z){
        return Vec3{Fixed::fromInt(x),Fixed::fromInt(0),Fixed::fromInt(z)};
    };
    tile.collision.push_back({p(0,0),p(20,20),p(20,0)});
    tile.collision.push_back({p(0,0),p(0,20),p(20,20)});
    Entrance e{123,1000,1000,0,-400,250};
    const auto empty=cutTerrainEntrances(tile,{});
    assert(empty.triangles.size()==2);
    const auto cut=cutTerrainEntrances(tile,{e});
    assert(cut.triangles.size()>2);
    assert(!cut.dirtyRegions.empty());
    // The exact center of the hole is never inside a surviving triangle.
    auto inside=[](CutTriangleCm t,double x,double z){
        auto orient=[&](CutVertexCm a,CutVertexCm b){
            return (b.x-a.x)*(z-a.z)-(b.z-a.z)*(x-a.x);
        };
        double a=orient(t.a,t.b),b=orient(t.b,t.c),c=orient(t.c,t.a);
        return (a>=-1e-5&&b>=-1e-5&&c>=-1e-5)||
               (a<=1e-5&&b<=1e-5&&c<=1e-5);
    };
    for(const auto& t:cut.triangles)assert(!inside(t,1000,1000));
    auto again=cutTerrainEntrances(tile,{e});
    assert(cut.triangles.size()==again.triangles.size());
    for(size_t i=0;i<cut.triangles.size();++i){
        assert(cut.triangles[i].a.x==again.triangles[i].a.x);
        assert(cut.triangles[i].b.z==again.triangles[i].b.z);
    }
    bool bad=false;
    try {(void)cutTerrainEntrances(tile,{e},5);}catch(const std::invalid_argument&){bad=true;}
    assert(bad);
    std::cout<<"surface cut contract OK triangles="<<cut.triangles.size()<<"\n";
}
