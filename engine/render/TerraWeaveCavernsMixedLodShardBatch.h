#pragma once
// Stage37: complete connected-residency LOD negotiation across shard borders.
// It resolves SIX face directions before any graphics owner is staged.
// Extra adjacent shards are included whenever their visual coarse mesh must
// be subdivided. Authoritative collision sources stay unmodified.
#include "engine/render/TerraWeaveCavernsShardCatalog.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave::caverns {
struct CaveMixedLodShardBatch {
    std::vector<CaveShardSnapshot> snapshots; // full affected source shards
    CaveMixedLodPlan visual;                  // final stitched visual-only mesh
    bool ready()const noexcept{return visual.ready();}
};
inline CaveMixedLodShardBatch planCaveMixedLodShardBatch(
    const CaveShardCatalog& catalog,
    const std::map<CaveShardKey,std::vector<CaveBatchChunk>,CaveShardLess>& edits,
    uint64_t batchRevision,size_t maxConnected=8192) {
    if(edits.empty()||!batchRevision||!maxConnected||maxConnected>8192)
        throw std::invalid_argument("Caverns LOD shard batch inputs");
    CaveMixedLodShardBatch batch;
    std::map<CaveShardKey,CaveShardSnapshot,CaveShardLess> snapshots;
    std::map<ChunkKey3,CaveBatchChunk,KeyLess> combined;
    for(const auto& [shard,group]:edits) {
        auto candidate=catalog.preview(shard,group,batchRevision);
        snapshots.emplace(shard,candidate);
        for(const auto& chunk:candidate.chunks)
            if(!combined.emplace(chunk.mesh.key,chunk).second)
                throw std::invalid_argument("Caverns duplicate LOD batch chunk");
    }
    // Traverse the connected set of currently loaded cave sources, not only
    // the GPU shard being edited. Otherwise a +X face on one shard could
    // silently crack against the neighbor's -X face in another shard.
    std::vector<ChunkKey3> work;
    for(const auto& [key,chunk]:combined){(void)chunk;work.push_back(key);}
    constexpr std::array<ChunkKey3,6> dirs={
        ChunkKey3{1,0,0},ChunkKey3{-1,0,0},
        ChunkKey3{0,1,0},ChunkKey3{0,-1,0},
        ChunkKey3{0,0,1},ChunkKey3{0,0,-1}};
    for(size_t cursor=0;cursor<work.size();++cursor){
        const auto key=work[cursor];
        for(const auto d:dirs) {
            const ChunkKey3 neighbor{key.x+d.x,key.y+d.y,key.z+d.z};
            if(combined.contains(neighbor))continue;
            const CaveBatchChunk* resident=catalog.find(neighbor);
            if(!resident)continue;
            if(combined.size()>=maxConnected)
                throw std::length_error("Caverns connected seam planning capacity");
            combined.emplace(neighbor,*resident);
            work.push_back(neighbor);
        }
    }
    std::vector<CaveCertifiedChunk> residents;
    residents.reserve(combined.size());
    for(const auto& [key,chunk]:combined){
        CaveCertifiedChunk c;
        c.mesh=chunk.mesh;
        c.sourceRevision=chunk.sourceRevision?chunk.sourceRevision:1;
        c.renderRevision=chunk.revision;
        c.resolution=chunk.resolution;
        c.spacingCm=chunk.spacingCm;
        residents.push_back(std::move(c));
    }
    batch.visual=planCaveMixedLodVisual(residents,maxConnected);
    // Legacy catalog entries without SDF source provenance cannot safely
    // participate in a new stitched mixed-LOD face. Rebuild both sides from
    // the current frozen source, even if their render revisions look valid.
    for(const auto& face:batch.visual.faces){
        const auto& a=combined.at(face.keys.negative);
        const auto& b=combined.at(face.keys.positive);
        if(face.decision!=CaveFaceDecision::ExactMatch &&
           (!a.sourceRevision||!b.sourceRevision)){
            batch.visual.sourceConflict=true;
            for(const auto* chunk:{&a,&b}){
                const auto it=batch.visual.rebuild.find(chunk->mesh.key);
                if(it==batch.visual.rebuild.end()||
                   it->second.spacingCm>face.targetSpacingCm)
                    batch.visual.rebuild[chunk->mesh.key]=CaveRebuildRequest{
                        chunk->mesh.key,face.targetResolution,
                        face.targetSpacingCm,
                        chunk->sourceRevision,0};
            }
        }
    }
    if(!batch.ready())return batch;

    // If a coarse neighbor must change its visual triangulation, its
    // ENTIRE shard must be staged and switched in the same owner barrier.
    // Never mutate the authoritative/catalog CaveMesh just to fix rendering.
    for(const auto& [key,original]:combined) {
        const auto& stitched=batch.visual.visual.at(key);
        if(stitched.mesh.indices==original.mesh.indices &&
           stitched.mesh.vertices.size()==original.mesh.vertices.size())
            continue;
        const CaveShardKey shard=caveShardFor(key,catalog.config());
        if(snapshots.contains(shard))continue;
        auto result=catalog.preview(shard,{},batchRevision);
        snapshots.emplace(shard,std::move(result));
    }
    batch.snapshots.reserve(snapshots.size());
    for(auto& [shard,snapshot]:snapshots){
        (void)shard;
        batch.snapshots.push_back(std::move(snapshot));
    }
    return batch;
}
} // namespace aiverse::terraweave::caverns
