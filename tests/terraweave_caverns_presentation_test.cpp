#include "engine/render/TerraWeaveCavernsPresentationLeases.h"
#include <cassert>
#include <iostream>
using namespace aiverse::terraweave::caverns;
int main(){
    CavePresentationLeases registry(2);
    ChunkKey3 k{-1,-3,2};
    auto a=registry.begin(k,1,1);
    assert(registry.route(k)==CavePresentationRoute::Hidden);
    assert(!registry.activateStrata(a,true));
    assert(registry.confirmFallback(a));
    assert(registry.route(k)==CavePresentationRoute::Renderer3D);
    assert(registry.confirmStrataPrepared(a));
    assert(!registry.activateStrata(a,false));
    assert(registry.activateStrata(a,true));
    assert(registry.route(k)==CavePresentationRoute::Strata);
    assert(registry.deactivateStrata(a));
    assert(registry.route(k)==CavePresentationRoute::Renderer3D);
    auto b=registry.begin(k,2,4);
    assert(!registry.confirmStrataPrepared(a));
    assert(registry.route(k)==CavePresentationRoute::Renderer3D);
    assert(registry.confirmFallback(b));
    assert(registry.confirmStrataPrepared(b));
    assert(registry.activateStrata(b,true));
    assert(registry.requestRetirement(b));
    assert(!registry.finishRetirement(b,false));
    assert(registry.route(k)==CavePresentationRoute::Strata); // not retired prematurely
    assert(registry.finishRetirement(b,true));
    assert(registry.route(k)==CavePresentationRoute::Hidden);
    assert(!registry.confirmFallback(b));
    std::cout<<"Caverns presentation lease contract OK\n";
}
