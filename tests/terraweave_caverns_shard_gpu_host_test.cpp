#include "engine/render/TerraWeaveCavernsShardGpuHost.h"
#include <cassert>
#include <iostream>
#include <stdexcept>
using namespace aiverse::terraweave::caverns;
static CaveBatchChunk entry(ChunkKey3 k,uint64_t revision,bool populated) {
    CaveBatchChunk chunk;
    chunk.mesh.key=k;chunk.revision=revision;
    chunk.generatorVersion=1;chunk.resolution=4;chunk.spacingCm=100;
    if(populated) {
        chunk.mesh.vertices={{0,0,0,0,1,0},{1,0,0,0,1,0},{0,0,1,0,1,0}};
        chunk.mesh.indices={0,1,2};
    }
    return chunk;
}
int main(){
    CaveShardConfig config;config.chunksPerAxis=2;
    const ChunkKey3 a{-2,-2,-2},b{-1,-2,-2},c{0,0,0};
    const auto ga=caveShardFor(a,config);
    const auto gb=caveShardFor(c,config);
    assert(!(ga==gb));
    const auto grouped=groupCaveEditsByShard({
        entry(a,2,false),entry(b,2,true),entry(c,2,true)},config);
    assert(grouped.size()==2);
    assert(grouped.at(ga).size()==2);
    assert(grouped.at(gb).size()==1);
    bool duplicates=false;
    try {(void)groupCaveEditsByShard({entry(a,2,true),entry(a,3,false)},config);}
    catch(const std::invalid_argument&){duplicates=true;}
    assert(duplicates);
    CaveShardGpuHost gpu(config);
    assert(gpu.registerCommittedSource(entry(a,1,true)));
    assert(gpu.registerCommittedSource(entry(b,1,true)));
    assert(gpu.registerCommittedSource(entry(c,1,true)));
    assert(gpu.ownerCount()==0 && gpu.pendingCount()==0);
    // No device and no external host authorization: only CPU preview,
    // which must retain B and the separate shard C.
    const auto snap=gpu.catalog().preview(ga,{entry(a,2,false)},10);
    assert(snap.visibleChunks==1 && snap.chunks.size()==2);
    auto prepared=prepareFullCaveShardPresentation(snap,{8,100,true});
    assert(prepared.models.size()==1);
    assert(gpu.catalog().find(a)->revision==1); // uncommitted unchanged
    assert(gpu.catalog().find(c)->revision==1); // unrelated shard unchanged
    assert(gpu.ownerCount()==0); // no GPU scene silently created
    std::cout<<"caverns stage30 shard GPU host CPU contract OK\n";
}
