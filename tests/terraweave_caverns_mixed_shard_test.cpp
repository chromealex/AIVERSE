#include "engine/render/TerraWeaveCavernsShardCatalog.h"
#include <cassert>
#include <iostream>
#include <stdexcept>
using namespace aiverse::terraweave::caverns;
static CaveBatchChunk chunk(ChunkKey3 key,uint64_t revision,bool visible) {
    CaveBatchChunk c;c.mesh.key=key;c.revision=revision;
    c.generatorVersion=7;c.resolution=4;c.spacingCm=100;
    if(visible) {
        c.mesh.vertices={{0,0,0,0,1,0},{1,0,0,0,1,0},{0,0,1,0,1,0}};
        c.mesh.indices={0,1,2};
    }
    return c;
}
int main() {
    CaveShardCatalog catalog({2,16});
    const ChunkKey3 untouched{-2,-2,-2},deleted{-1,-2,-2},updated{-2,-1,-2};
    assert(catalog.adopt(chunk(untouched,1,true)));
    assert(catalog.adopt(chunk(deleted,1,true)));
    assert(catalog.adopt(chunk(updated,1,true)));
    const auto shard=caveShardFor(untouched,{2,16});
    assert(shard==caveShardFor(deleted,{2,16}));
    assert(shard==caveShardFor(updated,{2,16}));
    const auto candidate=catalog.preview(shard,{
        chunk(deleted,2,false),chunk(updated,2,true)},9);
    assert(candidate.chunks.size()==3);
    assert(candidate.visibleChunks==2);
    assert(catalog.find(deleted)->revision==1); // shadow must be invisible
    auto prepared=prepareFullCaveShardPresentation(candidate,{8,100,true});
    assert(prepared.models.size()==2);
    assert(prepared.manifest["instances"].size()==2);
    assert(prepared.vertexCount==6);
    bool stale=false;
    try {(void)catalog.preview(shard,{chunk(updated,1,true)},10);}
    catch(const std::invalid_argument&){stale=true;}
    assert(stale);
    assert(catalog.adoptSnapshot(candidate));
    assert(catalog.find(deleted)->mesh.indices.empty());
    assert(catalog.find(untouched)->revision==1); // unchanged source retained
    assert(catalog.find(updated)->revision==2);
    assert(!catalog.adoptSnapshot(candidate)); // cannot re-commit old shard
    const auto allEmpty=catalog.preview(shard,{
        chunk(untouched,2,false),chunk(updated,3,false)},11);
    assert(allEmpty.visibleChunks==0);
    auto noModels=prepareFullCaveShardPresentation(allEmpty);
    assert(noModels.models.empty()); // host must stageEmpty, never dummy mesh
    assert(catalog.adoptSnapshot(allEmpty));
    assert(catalog.find(deleted)!=nullptr); // tombstone remains
    std::cout<<"caverns stage29 mixed shard snapshot OK\n";
}
