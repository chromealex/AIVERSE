#pragma once
// TerraWeave Caverns stage29: authoritative per-shard CPU presentation catalog.
// A changed chunk replaces only its entry; an empty mesh records a tombstone.
// GPU staging ALWAYS receives the FULL affected shard (including unchanged
// chunks), never only the changed nonempty subset. No Device/GPU operations.
#include "engine/render/TerraWeaveCavernsBatchGeometry.h"
#include "engine/world/TerraWeaveCavernsShards.h"
#include "engine/world/TerraWeaveCavernsMixedLodPlan.h"
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>
namespace aiverse::terraweave::caverns {
struct CaveShardSnapshot {
    CaveShardKey shard{};
    uint64_t revision=0;
    std::vector<CaveBatchChunk> chunks; // ALL resident source slots, incl empty
    size_t visibleChunks=0;
};
class CaveShardCatalog {
public:
    explicit CaveShardCatalog(CaveShardConfig cfg={}):config_(cfg) {
        validateCaveShardConfig(cfg);
    }
    CaveShardConfig config()const noexcept{return config_;}
    // Adopt only after a complete external graphics/physics/nav commit.
    // A no-mesh chunk remains in the source catalog so the owner knows the
    // slot exists but should have no GeometryScene instance.
    bool adopt(const CaveBatchChunk& chunk){
        if(!chunk.revision||!chunk.generatorVersion||
           !chunk.resolution||chunk.resolution>64||
           chunk.spacingCm<=0||chunk.spacingCm>10000||
           (chunk.mesh.indices.empty()!=chunk.mesh.vertices.empty()))
            throw std::invalid_argument("Caverns shard source entry");
        auto it=source_.find(chunk.mesh.key);
        if(it!=source_.end() && chunk.revision<=it->second.revision)
            return false;
        source_.insert_or_assign(chunk.mesh.key,chunk);
        return true;
    }
    // Source retirement means unload. Different from a zero-surface chunk:
    // both must produce no draw source in the new complete shard snapshot.
    bool erase(ChunkKey3 key,uint64_t expectedRevision) {
        auto it=source_.find(key);
        if(it==source_.end()||it->second.revision!=expectedRevision)return false;
        source_.erase(it);
        return true;
    }
    // Build a complete SHADOW snapshot before publishing any new sources.
    // Reject duplicate edits and any stale revision, leaving live unchanged.
    CaveShardSnapshot preview(CaveShardKey shard,
                              const std::vector<CaveBatchChunk>& edits,
                              uint64_t batchRevision)const {
        if(!batchRevision)throw std::invalid_argument("Caverns zero shard revision");
        std::map<ChunkKey3,CaveBatchChunk,KeyLess> combined;
        for(const auto& [key,chunk]:source_)
            if(caveShardFor(key,config_)==shard)
                combined.emplace(key,chunk);
        std::set<ChunkKey3,KeyLess> seen;
        for(const auto& chunk:edits) {
            const auto key=chunk.mesh.key;
            if(!(caveShardFor(key,config_)==shard)||!seen.insert(key).second||
               !chunk.revision||!chunk.generatorVersion||
               chunk.resolution==0||chunk.resolution>64||
               chunk.spacingCm<=0||chunk.spacingCm>10000||
               (chunk.mesh.indices.empty()!=chunk.mesh.vertices.empty()))
                throw std::invalid_argument("Caverns invalid shard preview edit");
            auto it=combined.find(key);
            if(it!=combined.end() && chunk.revision<=it->second.revision)
                throw std::invalid_argument("Caverns stale shard preview edit");
            combined.insert_or_assign(key,chunk);
        }
        if(combined.size()>size_t(config_.chunksPerAxis)*config_.chunksPerAxis*
                           config_.chunksPerAxis)
            throw std::length_error("Caverns shard slot budget");
        CaveShardSnapshot result;result.shard=shard;result.revision=batchRevision;
        for(const auto& [key,chunk]:combined) {
            (void)key;
            result.visibleChunks+=!chunk.mesh.indices.empty();
            result.chunks.push_back(chunk);
        }
        return result;
    }
    // The host must have committed the corresponding full GeometryScene and
    // the authoritative physics/nav revision before it calls adoptSnapshot.
    // This only mutates catalog bookkeeping, never a GPU/physics object.
    bool adoptSnapshot(const CaveShardSnapshot& snap) {
        const size_t capacity=size_t(config_.chunksPerAxis)*
            config_.chunksPerAxis*config_.chunksPerAxis;
        if(!snap.revision||snap.chunks.size()>capacity)return false;
        const auto previous=lastShardRevision_.find(snap.shard);
        if(previous!=lastShardRevision_.end()&&
           previous->second>=snap.revision)return false;
        std::set<ChunkKey3,KeyLess> seen;
        for(const auto& chunk:snap.chunks) {
            if(!(caveShardFor(chunk.mesh.key,config_)==snap.shard)||
               !seen.insert(chunk.mesh.key).second||
               !chunk.generatorVersion||!chunk.revision||
               !chunk.resolution||chunk.resolution>64||
               chunk.spacingCm<=0||chunk.spacingCm>10000||
               (chunk.mesh.indices.empty()!=chunk.mesh.vertices.empty()))
                return false;
            auto it=source_.find(chunk.mesh.key);
            if(it!=source_.end()&&chunk.revision<it->second.revision)
                return false;
        }
        // Remove obsolete keys of this shard (unloaded), then add every
        // supplied source including zero-geometry entries.
        for(auto it=source_.begin();it!=source_.end();) {
            if(caveShardFor(it->first,config_)==snap.shard &&
               !seen.contains(it->first))it=source_.erase(it);
            else ++it;
        }
        for(const auto& chunk:snap.chunks)
            source_.insert_or_assign(chunk.mesh.key,chunk);
        lastShardRevision_[snap.shard]=snap.revision;
        return true;
    }
    const CaveBatchChunk* find(ChunkKey3 key)const {
        const auto it=source_.find(key);
        return it==source_.end()?nullptr:&it->second;
    }
    size_t size()const noexcept{return source_.size();}
private:
    CaveShardConfig config_;
    std::map<ChunkKey3,CaveBatchChunk,KeyLess> source_;
    std::map<CaveShardKey,uint64_t,CaveShardLess> lastShardRevision_;
};
// Deterministic per-shard grouping of a complete multi-shard edit batch.
// Each shard may later be staged independently, but a cross-shard game edit
// must NOT be acknowledged until all affected shard owners commit.
inline std::map<CaveShardKey,std::vector<CaveBatchChunk>,CaveShardLess>
groupCaveEditsByShard(const std::vector<CaveBatchChunk>& edits,
                      CaveShardConfig config={}) {
    validateCaveShardConfig(config);
    if(edits.empty())throw std::invalid_argument("Caverns empty edit group");
    std::map<CaveShardKey,std::vector<CaveBatchChunk>,CaveShardLess> grouped;
    std::set<ChunkKey3,KeyLess> seen;
    for(const auto& chunk:edits){
        if(!seen.insert(chunk.mesh.key).second||
           !chunk.revision||!chunk.generatorVersion||
           !chunk.resolution||chunk.resolution>64||
           chunk.spacingCm<=0||chunk.spacingCm>10000||
           (chunk.mesh.indices.empty()!=chunk.mesh.vertices.empty()))
            throw std::invalid_argument("Caverns duplicate/invalid grouped edit");
        auto& group=grouped[caveShardFor(chunk.mesh.key,config)];
        group.push_back(chunk);
        const size_t limit=size_t(config.chunksPerAxis)*
                           config.chunksPerAxis*config.chunksPerAxis;
        if(group.size()>limit)
            throw std::length_error("Caverns grouped shard budget");
    }
    return grouped;
}
// The GPU owner can route an empty shard to stageEmpty without uploads.
// Otherwise, verify and transform presentation-only mixed LOD boundaries
// BEFORE creating GeometryScene models. Physics uses the ORIGINAL CaveMesh.
inline CaveMixedLodPlan planFullCaveShardLod(
    const CaveShardSnapshot& snapshot) {
    std::vector<CaveCertifiedChunk> sources;
    sources.reserve(snapshot.chunks.size());
    for(const auto& chunk:snapshot.chunks){
        CaveCertifiedChunk c;
        c.mesh=chunk.mesh;
        c.resolution=chunk.resolution;
        c.spacingCm=chunk.spacingCm;
        c.renderRevision=chunk.revision;
        // Legacy unversioned resident sources may render at a single exact
        // lattice. Mixed-LOD transitions REQUIRE explicit authored epochs.
        c.sourceRevision=chunk.sourceRevision?chunk.sourceRevision:1;
        sources.push_back(std::move(c));
    }
    return planCaveMixedLodVisual(sources);
}
inline CaveBatchPrepared prepareFullCaveShardPresentation(
    const CaveShardSnapshot& snapshot,CaveBatchSpec spec={}) {
    if(snapshot.chunks.empty()||snapshot.visibleChunks==0)return {};
    auto visualPlan=planFullCaveShardLod(snapshot);
    if(!visualPlan.ready())
        throw std::runtime_error("Caverns LOD borders need shared-SDF rebuild before GPU publication");
    std::vector<CaveBatchChunk> visualChunks=snapshot.chunks;
    for(auto& chunk:visualChunks) {
        const auto it=visualPlan.visual.find(chunk.mesh.key);
        if(it!=visualPlan.visual.end()) {
            // The stitched vertex/index data is PRESENTATION ONLY. A native
            // collider uses the unmodified authoritative chunk from snapshot.
            chunk.mesh=it->second.mesh;
        }
    }
    return prepareCaveBatch(std::move(visualChunks),spec);
}
