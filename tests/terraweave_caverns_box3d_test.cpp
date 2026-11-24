#include "engine/world/TerraWeaveCavernsBox3D.h"
#include <cassert>
#include <iostream>
using namespace aiverse::terraweave::caverns;
int main(){
    CutNativePayload p;p.cell={0,0,0};p.revision=1;
    auto at=[](int x,int z){return Vec3{Fixed::fromInt(x),{},Fixed::fromInt(z)};};
    p.collision.push_back({at(0,0),at(10,10),at(10,0)});
    auto body=makeCavernsBox3DBody(p,20);
    assert(body.motion==physics::Motion::Static);
    assert(body.shape==physics::ShapeKind::Mesh);
    assert(body.vertices.size()==3);
    assert(body.indices.size()==3);
    bool reject=false;
    try{(void)makeCavernsBox3DBody(p,0);}catch(const std::invalid_argument&){reject=true;}
    assert(reject);
    std::cout<<"caverns Box3D payload OK\n";
}
