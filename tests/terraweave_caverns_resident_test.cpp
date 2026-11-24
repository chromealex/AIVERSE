#include "engine/render/TerraWeaveCavernsResident.h"
#include <cassert>
#include <iostream>
using namespace aiverse::terraweave::caverns;
int main(){
    aiverse::terraweave::TerrainPatch patch;
    patch.cell={0,0,0};patch.resolution=2;patch.cellSizeMetres=20;
    const auto p=[](int x,int z){
        return Vec3{Fixed::fromInt(x),Fixed::fromInt(0),Fixed::fromInt(z)};
    };
    patch.collision={{p(0,0),p(20,20),p(20,0)},
                     {p(0,0),p(0,20),p(20,20)}};
    const Entrance e{11,1000,1000,0,-500,250};
    const auto cut=cutTerrainEntrances(patch,{e});
    auto native=buildCutNativePayload(cut,20);
    assert(native.model);
    assert(native.collision.size()==cut.triangles.size());
    assert(native.model->indices.size()==native.collision.size()*3);
    assert(native.model->vertices.size()==native.collision.size()*3);
    assert(native.dirty.size()==1);
    for(const auto& tri:native.collision)assert(world::SpatialWorld::validTriangle(tri));
    bool invalid=false;
    try{(void)buildCutNativePayload(cut,0);}
    catch(const std::invalid_argument&){invalid=true;}
    assert(invalid);
    std::cout<<"caverns native payload contract OK triangles="<<native.collision.size()<<"\n";
}
