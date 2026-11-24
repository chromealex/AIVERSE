#include "engine/render/graph/TextureGraphSharedScratch.h"
#include <cstdlib>
#include <iostream>
using namespace aiverse::render::texture_graph::gpu;
static void check(bool ok){if(!ok)std::abort();}
int main(){
    SharedScalarScratch cache;
    const ScalarScratchKey a{1,2,128,false},b{1,3,128,false};
    check(!cache.begin(a));
    check(!cache.begin(a)); // no completed producer
    cache.accept();
    check(cache.begin(a));
    cache.accept();
    check(!cache.begin(b)); // writing another node invalidates A
    check(!cache.begin(a));
    cache.accept();
    check(!cache.begin({2,2,128,false}));
    cache.accept();
    check(!cache.begin({2,2,256,false}));
    cache.accept();
    check(!cache.begin({2,2,256,true}));
    cache.accept();
    check(cache.begin({2,2,256,true}));
    cache.invalidate(); // cancel/failure/device loss/resize
    cache.accept(); // a late completion cannot revive invalidated storage
    check(!cache.begin({2,2,256,true}));
    std::cout<<"PASS: scratch identity, acceptance and invalidation\n";
}
