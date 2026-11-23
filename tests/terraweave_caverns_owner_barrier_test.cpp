#include "engine/world/TerraWeaveCavernsOwnerBarrier.h"
#include <cassert>
#include <iostream>
using namespace aiverse::terraweave::caverns;
int main(){
    CaveOwnerBarrier barrier;
    auto first=barrier.begin(7,40,2);
    assert(!barrier.ready(first));
    assert(!barrier.prepared(first,CaveOwnerPart(static_cast<uint32_t>(32))));
    assert(barrier.prepared(first,CaveOwnerPart::Geometry));
    assert(barrier.prepared(first,CaveOwnerPart::Spatial));
    assert(barrier.prepared(first,CaveOwnerPart::Box3D));
    assert(!barrier.confirm(first,true));
    assert(barrier.prepared(first,CaveOwnerPart::Navigation));
    assert(barrier.ready(first));
    assert(!barrier.confirm(first,false));
    assert(barrier.state()==CaveOwnerBarrierState::Aborted);
    assert(!barrier.confirm(first,true));
    auto second=barrier.begin(8,41,1);
    assert(!barrier.prepared(first,CaveOwnerPart::Geometry)); // stale
    assert(barrier.prepared(second,CaveOwnerPart::Geometry));
    assert(barrier.prepared(second,CaveOwnerPart::Spatial));
    assert(barrier.prepared(second,CaveOwnerPart::Box3D));
    assert(barrier.prepared(second,CaveOwnerPart::Navigation));
    assert(barrier.ready(second));
    assert(barrier.confirm(second,true));
    assert(barrier.state()==CaveOwnerBarrierState::Committed);
    assert(!barrier.abort(second));
    std::cout<<"caverns stage25 owner barrier PASS\n";
}
