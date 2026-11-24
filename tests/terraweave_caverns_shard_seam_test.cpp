#include "engine/world/TerraWeaveCavernsShards.h"
#include "engine/world/TerraWeaveCavernsSeamAudit.h"
#include <cassert>
#include <iostream>
using namespace aiverse::terraweave::caverns;
int main(){
    CaveShardRegistry registry({2,8});
    const CaveShardKey negative{-1,-1,-1};
    const CaveShardConfig config{2,8};
    assert(caveShardFor({-1,-1,-1},config)==negative);
    assert(caveShardFor({-2,-2,-2},config)==negative);
    registry.upsert({0,0,0},10);
    registry.upsert({1,0,0},11);
    registry.upsert({2,0,0},9);
    auto initial=registry.pump(2);
    assert(initial.size()==2);
    for(const auto& request:initial)assert(registry.acknowledge(request));
    assert(registry.dirtyShards()==0);
    registry.upsert({0,0,0},12);
    auto changed=registry.pump(2);
    assert(changed.size()==1); // unrelated shard need not reload
    assert(changed.front().keys.size()==2);
    registry.upsert({1,0,0},13);
    assert(!registry.acknowledge(changed.front())); // stale async completion
    auto replacement=registry.pump(2);
    assert(replacement.size()==1);
    assert(registry.fail(replacement.front()));
    replacement=registry.pump(2);
    assert(replacement.size()==1);
    assert(registry.acknowledge(replacement.front()));
    registry.erase({0,0,0});
    registry.erase({1,0,0});
    auto retired=registry.pump(2);
    assert(retired.size()==1&&retired.front().keys.empty());
    assert(registry.acknowledge(retired.front()));

    CaveMesh left,right;
    left.key={0,0,0};right.key={1,0,0};
    // boundary x=4m: both meshes share the exact same border edge.
    left.vertices={{4,0,0,0,1,0},{4,1,0,0,1,0},{3,0,0,0,1,0}};
    right.vertices={{0,0,0,0,1,0},{0,1,0,0,1,0},{1,0,0,0,1,0}};
    left.indices={0,1,2};right.indices={0,2,1};
    auto result=auditCaveSameLodSeam(left,right,4,100,0);
    assert(result.matched());
    assert(result.leftBoundaryEdges==1);
    right.vertices[1].y=1.25f;
    result=auditCaveSameLodSeam(left,right,4,100,0);
    assert(!result.matched()&&result.unmatchedLeft>0&&result.unmatchedRight>0);
    std::cout<<"caverns shard and seam contracts OK\n";
}
