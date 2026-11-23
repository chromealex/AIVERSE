#include "engine/world/TerraWeaveCavernsPublication.h"
#include "engine/world/TerraWeaveCavernsColliderLeases.h"
#include <cassert>
#include <iostream>
using namespace aiverse::terraweave::caverns;
int main(){
    CavePublicationCoordinator c(2);
    constexpr uint32_t core=1|2|4|8;
    const ChunkKey3 k{0,-1,0};
    auto a=c.begin(k,5,7,core|16);
    assert(!c.readyForGameplay(k,5));
    assert(c.prepared(a,CavePublishPart::Renderer));
    assert(c.prepared(a,CavePublishPart::SpatialWorld));
    assert(c.prepared(a,CavePublishPart::Box3D));
    assert(c.prepared(a,CavePublishPart::Navigation));
    assert(!c.commit(a,true)); // STRATA candidate not yet staged
    assert(c.prepared(a,CavePublishPart::Strata));
    assert(c.snapshot(k)->status==CavePublishStatus::Ready);
    assert(c.commit(a,true));
    assert(c.readyForGameplay(k,5));
    auto b=c.begin(k,6,8,core);
    assert(!c.prepared(a,CavePublishPart::Renderer)); // stale ticket
    assert(!c.readyForGameplay(k,6));
    assert(c.abort(b));
    assert(!c.commit(b,true));
    auto d=c.begin(k,7,9,core);
    for(auto part:{CavePublishPart::Renderer,CavePublishPart::SpatialWorld,
                   CavePublishPart::Box3D,CavePublishPart::Navigation})
        assert(c.prepared(d,part));
    assert(!c.commit(d,false)); // publication barrier failed
    assert(!c.readyForGameplay(k,7));
    CaveColliderLeasePool pool(1000,10);
    const auto l=pool.reserve(4);assert(l);
    const auto m=pool.reserve(4);assert(m && m->first==1004);
    assert(!pool.reserve(3));
    assert(pool.release(*l));
    const auto n=pool.reserve(3);assert(n && n->first==1000);
    assert(!pool.release(*l)); // old ABA token cannot free reused range
    assert(pool.holds(*n));
    assert(pool.release(*n));
    assert(pool.release(*m));
    std::cout<<"caverns publication/lease contract OK\n";
}
