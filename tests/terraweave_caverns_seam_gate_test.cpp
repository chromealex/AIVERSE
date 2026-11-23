#include "engine/world/TerraWeaveCavernsSeamGate.h"
#include <cassert>
#include <iostream>
using namespace aiverse::terraweave::caverns;
static CaveCertifiedChunk sample(ChunkKey3 key,bool high,bool split,uint64_t revision=1){
    CaveCertifiedChunk c;c.mesh.key=key;c.sourceRevision=5;c.renderRevision=revision;
    c.resolution=high?4:2;c.spacingCm=high?100:200;
    if(key.x==0){
        c.mesh.vertices={{4,0,0,0,1,0},{4,4,0,0,1,0},{3,0,0,0,1,0}};
        c.mesh.indices={0,1,2};
    }else if(split){
        c.mesh.vertices={{0,0,0,0,1,0},{0,2,0,0,1,0},{0,4,0,0,1,0},{1,0,0,0,1,0}};
        c.mesh.indices={0,1,3,1,2,3};
    }else{
        c.mesh.vertices={{0,0,0,0,1,0},{0,4,0,0,1,0},{1,0,0,0,1,0}};
        c.mesh.indices={0,1,2};
    }
    return c;
}
int main(){
    CaveSeamPublicationGate gate;
    gate.upsert(sample({0,0,0},false,false));
    gate.upsert(sample({1,0,0},true,true));
    auto audit=gate.analyze();
    assert(audit.fallbackFaces==1 && audit.pendingChunks==1);
    assert(!gate.canPublish({0,0,0},1));
    assert(!gate.canPublish({1,0,0},1));
    auto jobs=gate.requests();
    assert(jobs.size()==1 && jobs[0].key.x==0 && jobs[0].key.y==0 && jobs[0].key.z==0);
    // Old result has a different source revision: rejected.
    auto stale=sample({0,0,0},true,false,2);
    stale.sourceRevision=4;
    assert(!gate.accept(jobs[0],stale));
    // A rebuilt high-resolution boundary compatible with the high neighbor.
    auto rebuilt=sample({0,0,0},true,false,2);
    rebuilt.mesh.vertices={{4,0,0,0,1,0},{4,2,0,0,1,0},{4,4,0,0,1,0},{3,0,0,0,1,0}};
    rebuilt.mesh.indices={0,1,3,1,2,3};
    assert(gate.accept(jobs[0],rebuilt));
    assert(!gate.canPublish({0,0,0},2)); // audit not refreshed
    audit=gate.analyze();
    assert(audit.fallbackFaces==0);
    assert(gate.canPublish({0,0,0},2));
    assert(gate.canPublish({1,0,0},1));
    // Changed mesh revision invalidates neighbor certification.
    auto changed=sample({1,0,0},true,true,3);
    gate.upsert(std::move(changed));
    assert(!gate.canPublish({0,0,0},2));
    audit=gate.analyze();
    assert(audit.fallbackFaces==0);
    assert(gate.canPublish({1,0,0},3));
    std::cout<<"caverns stage16 seam publication gate contract OK\n";
}
