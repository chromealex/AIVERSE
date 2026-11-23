#include "engine/render/TerraWeaveCavernsSwapPolicy.h"
#include <cassert>
#include <iostream>
using namespace aiverse::terraweave::caverns;
int main() {
    assert(caveCanStageScene(0,0,1,0,8));
    assert(caveCanStageScene(4,0,5,3,8));
    assert(!caveCanStageScene(4,0,4,1,8)); // stale
    assert(!caveCanStageScene(4,7,6,2,8)); // older than already staged
    assert(!caveCanStageScene(4,7,8,8,8)); // GPU owner budget reached
    assert(!caveCanStageScene(4,7,8,1,0)); // invalid max
    assert(!caveCanStageScene(4,0,0,1,8));
    assert(caveCanActivateScene(4,5));
    assert(!caveCanActivateScene(4,4));
    assert(!caveCanActivateScene(4,0));
    std::cout<<"caverns stage24 swap policy PASS\n";
}
