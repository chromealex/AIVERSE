#pragma once
// TerraWeave Caverns Stage30: one GPU-owner GeometryScene per bounded shard.
// Stage the COMPLETE shard snapshot and keep unrelated active shards unchanged.
// The renderer and physics host must own commit barriers; this class never
// reports an atomic Box3D/SpatialWorld/NavMesh transaction by itself.
#include "engine/render/TerraWeaveCavernsGpuSwap.h"
#include "engine/render/TerraWeaveCavernsShardCatalog.h"
#include "engine/render/TerraWeaveCavernsMixedLodShardBatch.h"
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>
namespace aiverse::terraweave::caverns {

class CaveShardGpuHost {
public:
    explicit CaveShardGpuHost(CaveShardConfig cfg={}):catalog_(cfg){
        validateCaveShardConfig(cfg);
    }
    CaveShardGpuHost(const CaveShardGpuHost&)=delete;
    CaveShardGpuHost& operator=(const CaveShardGpuHost&)=delete;

    // Register existing committed source geometry only for initial loaded
    // world bootstrapping. Never call to acknowledge an unpublished edit.
    bool registerCommittedSource(const CaveBatchChunk& chunk){
        if(pending_.contains(caveShardFor(chunk.mesh.key,catalog_.config())))
            return false;
        return catalog_.adopt(chunk);
    }

    // Bookkeeping ONLY, called after a real owner-wide streaming retirement
    // has safely removed the corresponding GPU source and physical/nav data.
    // A staged shard must not be concurrently rewritten or forgotten.
    bool forgetCommittedSource(ChunkKey3 key,uint64_t revision) {
        if(pending_.contains(caveShardFor(key,catalog_.config())))return false;
        return catalog_.erase(key,revision);
    }

    // All source edits must belong to ONE shard. They may be zero-geometry
    // tombstones. This prepares an entire replacement shard GPU scene from
    // unchanged + edited sources, not just edited models.
    // Graphics owner calls OUTSIDE a draw pass; failed stage never adopts
    // the CPU catalog and never switches active visuals.
    bool stage(gfx::Device& device,CaveShardKey shard,
               const std::vector<CaveBatchChunk>& edits,
               uint64_t batchRenderRevision,CaveBatchSpec spec={}) {
        if(edits.empty()||!batchRenderRevision)return false;
        const std::map<CaveShardKey,std::vector<CaveBatchChunk>,CaveShardLess>
            grouped{{shard,edits}};
        auto plan=planCaveMixedLodShardBatch(catalog_,grouped,
                                            batchRenderRevision);
        if(!plan.ready()){
            lastLodRebuilds_=plan.visual.rebuild;
            lastLodSourceConflict_=plan.visual.sourceConflict;
            return false;
        }
        // A single-shard call cannot atomically publish stitched patches
        // to a different shard. The grouped API is required in that case.
        if(plan.snapshots.size()!=1)return false;
        return stageSnapshot(device,plan.snapshots.front(),
                             plan.visual,spec);
    }

    // Bridge adapter: group prepared CPU edits by spatial shard, then GPU
    // stage each full shard (including unchanged sources). On any rejection,
    // cancel the stages created by THIS invocation; failed GPU allocations
    // retain their GeometryScene owner for later GPU-idle recovery.
    // Returns all affected keys only when every shard has staged.
    bool stagePreparedEdits(gfx::Device& device,
                            const std::vector<CavePreparedEdit>& edits,
                            uint64_t generatorVersion,
                            uint64_t batchRenderRevision,
                            std::vector<CaveShardKey>& stagedShards,
                            CaveBatchSpec spec={}) {
        if(!generatorVersion||edits.empty()||!batchRenderRevision)
            return false;
        std::vector<CaveBatchChunk> chunks;
        chunks.reserve(edits.size());
        const auto sourceRevision=edits.front().candidate.sourceRevision;
        for(const auto& item:edits){
            const auto& c=item.candidate;
            if(!sourceRevision||c.sourceRevision!=sourceRevision||
               c.sourceRevision!=item.dirty.sourceRevision||
               c.mesh.key!=item.dirty.key||!c.renderRevision)
                return false;
            CaveBatchChunk itemChunk;
            itemChunk.mesh=c.mesh;
            itemChunk.generatorVersion=generatorVersion;
            itemChunk.revision=c.renderRevision;
            itemChunk.sourceRevision=c.sourceRevision;
            itemChunk.resolution=c.resolution;
            itemChunk.spacingCm=c.spacingCm;
            chunks.push_back(std::move(itemChunk));
        }
        const auto grouped=groupCaveEditsByShard(chunks,catalog_.config());
        // A complete connected face plan includes stitched neighboring
        // shards even when no edited chunk originated inside those shards.
        auto plan=planCaveMixedLodShardBatch(catalog_,grouped,
                                            batchRenderRevision);
        if(!plan.ready()){
            lastLodRebuilds_=plan.visual.rebuild;
            lastLodSourceConflict_=plan.visual.sourceConflict;
            return false;
        }
        lastLodRebuilds_.clear();
        lastLodSourceConflict_=false;
        std::vector<CaveShardKey> local;
        local.reserve(plan.snapshots.size());
        try{
            for(const auto& snapshot:plan.snapshots){
                if(!stageSnapshot(device,snapshot,plan.visual,spec)){
                    for(auto key:local)(void)cancel(key);
                    return false;
                }
                local.push_back(snapshot.shard);
            }
        }catch(...){
            for(auto key:local)(void)cancel(key);
            throw;
        }
        stagedShards=std::move(local);
        return true;
    }

    bool hasPending(CaveShardKey shard)const{
        return pending_.contains(shard);
    }
    const CaveShardSnapshot* pending(CaveShardKey shard)const{
        const auto it=pending_.find(shard);
        return it==pending_.end()?nullptr:&it->second;
    }

    // Real graphics/physics/nav owner proves all-or-none visibility outside
    // this class. On refusal, keep both active scene and pending snapshot.
    // On successful callback the exact GPU shard is activated and the
    // catalog is advanced. Do NOT supply a no-op success callback.
    template<class OwnerCommit>
    bool activate(CaveShardKey shard,OwnerCommit&& commitOwners){
        auto it=pending_.find(shard);
        if(it==pending_.end())return false;
        auto jt=owners_.find(shard);
        if(jt==owners_.end()||!jt->second ||
           jt->second->stagedRevision()!=it->second.revision)
            return false;
        // The catalog's committed state must remain unchanged while the
        // external host performs its synchronized visibility barrier.
        const CaveShardSnapshot snap=it->second;
        bool success=jt->second->activate([&](uint64_t revision){
            return revision==snap.revision && commitOwners(snap);
        });
        if(!success)return false;
        // This should be infallible after a valid preview with no concurrent
        // writers. If not, never claim success silently.
        if(!catalog_.adoptSnapshot(snap))
            throw std::logic_error("Caverns committed GPU shard not reflected in source catalog");
        pending_.erase(it);
        return true;
    }

    // One host barrier can coordinate ALL shards changed by a single brush
    // edit, preventing partially committed neighboring GeometryScene shards.
    // Call only with Scene draws and all physics/nav readers paused. The
    // callback MUST truly commit physics+nav for the whole batch atomically.
    // The host must guarantee no exceptions or concurrent mutation after
    // that success; this is NOT a substitute for native physics rollback.
    template<class OwnerBatchCommit>
    bool activateGroup(const std::vector<CaveShardKey>& shardKeys,
                       OwnerBatchCommit&& commitOwners) {
        if(shardKeys.empty())return false;
        std::set<CaveShardKey,CaveShardLess> unique;
        std::vector<CaveShardSnapshot> snapshots;
        snapshots.reserve(shardKeys.size());
        for(auto key:shardKeys){
            if(!unique.insert(key).second)return false;
            const auto pendingIt=pending_.find(key);
            const auto ownerIt=owners_.find(key);
            if(pendingIt==pending_.end()||
               ownerIt==owners_.end()||!ownerIt->second||
               !ownerIt->second->hasStaged()||
               ownerIt->second->stagedRevision()!=pendingIt->second.revision)
                return false;
            snapshots.push_back(pendingIt->second);
        }
        bool committed=false;
        try {committed=commitOwners(snapshots);}
        catch(...) {return false;}
        if(!committed)return false;
        // Every GPU candidate and revision was verified before invoking
        // the callback. With owner-exclusive access, these activate calls
        // are noexcept-in-practice except for an engine invariant failure.
        for(size_t i=0;i<shardKeys.size();++i){
            auto& owner=*owners_.at(shardKeys[i]);
            if(!owner.activate([&](uint64_t revision){
                    return revision==snapshots[i].revision;
                }))
                throw std::logic_error(
                    "Caverns GPU shard activation failed after committed host barrier");
        }
        for(const auto& snapshot:snapshots)
            if(!catalog_.adoptSnapshot(snapshot))
                throw std::logic_error(
                    "Caverns shard catalog failed after committed host barrier");
        for(auto key:shardKeys)pending_.erase(key);
        return true;
    }

    // EXPLICIT EDITOR-ONLY VISUAL PREVIEW. Does not publish game physics,
    // does not certify navigation, and must NEVER ACK the gameplay dirty queue.
    // The caller must keep native editor/play views separate. This is a
    // presentation change only and no cross-owner transaction is implied.
    bool activateVisualOnly(const std::vector<CaveShardKey>& shardKeys) {
        if(shardKeys.empty())return false;
        std::set<CaveShardKey,CaveShardLess> unique;
        for(const auto& key:shardKeys){
            if(!unique.insert(key).second)return false;
            const auto a=pending_.find(key);
            const auto b=owners_.find(key);
            if(a==pending_.end()||b==owners_.end()||!b->second||
               !b->second->hasStaged()||
               b->second->stagedRevision()!=a->second.revision)
                return false;
        }
        // This callback asserts ONLY the visual state was staged. Do not
        // substitute it for activateGroup's real gameplay owner callback.
        return activateGroup(shardKeys,[](const auto&){return true;});
    }

    bool cancel(CaveShardKey shard) {
        auto it=pending_.find(shard);
        if(it==pending_.end())return false;
        auto owner=owners_.find(shard);
        if(owner==owners_.end()||!owner->second||
           !owner->second->cancelStage())return false;
        pending_.erase(it);
        return true;
    }

    // Native Scene must draw ONLY this active source for the cave shards.
    // The rest of the engine's terrain must still draw normally.
    void draw(render::Renderer3D& renderer) {
        for(auto& [key,gpu]:owners_){
            (void)key;
            if(gpu)gpu->draw(renderer);
        }
    }
    // Use only after a real GPU idle proof, not per brush dab.
    // Keep owners on failed retirement rather than destruct them.
    bool collectAfterGpuIdle(gfx::Device& device){
        for(auto& [key,gpu]:owners_){
            (void)key;
            if(gpu&&!gpu->collectAfterGpuIdle(device))return false;
        }
        return true;
    }
    // Call when Scene draws have stopped and device lifetime is retained.
    bool shutdownAfterGpuIdle(gfx::Device& device){
        for(auto& [key,gpu]:owners_){
            (void)key;
            if(gpu&&!gpu->shutdownAfterGpuIdle(device))return false;
        }
        pending_.clear();
        owners_.clear();
        return true;
    }
    struct GpuPreviewStats {
        size_t activeShards=0,pendingShards=0;
        size_t retiredScenes=0,pendingGpuOwners=0;
        uint64_t maxActiveRevision=0;
    };
    GpuPreviewStats stats()const noexcept{
        GpuPreviewStats out;
        out.pendingShards=pending_.size();
        for(const auto& [key,owner]:owners_){
            (void)key;
            if(!owner)continue;
            out.activeShards+=owner->resident();
            out.retiredScenes+=owner->retiredCount();
            out.pendingGpuOwners+=owner->pendingGpuOwners();
            out.maxActiveRevision=std::max(out.maxActiveRevision,
                                           owner->activeRevision());
        }
        return out;
    }
    // On incompatible contours the game/editor host must regenerate these
    // chunks from ONE immutable snapshot at each requested common LOD.
    // Do not silently bypass the seam gate.
    const std::map<ChunkKey3,CaveRebuildRequest,KeyLess>&
    lodRebuilds()const noexcept{return lastLodRebuilds_;}
    bool lodSourceConflict()const noexcept{return lastLodSourceConflict_;}
    size_t pendingCount()const noexcept{return pending_.size();}
    size_t ownerCount()const noexcept{return owners_.size();}
    const CaveShardCatalog& catalog()const noexcept{return catalog_;}
private:
    bool stageSnapshot(gfx::Device& device,const CaveShardSnapshot& snapshot,
                       const CaveMixedLodPlan& visual,CaveBatchSpec spec){
        if(!snapshot.revision||pending_.contains(snapshot.shard)||
           snapshot.chunks.size()>spec.maxChunks)return false;
        auto prepared=prepareFullCaveShardPresentation(snapshot,spec,&visual);
        auto& gpu=owners_[snapshot.shard];
        if(!gpu)gpu=std::make_unique<CaveGeometrySwapOwner>();
        const bool ok=prepared.models.empty()
            ? gpu->stageEmpty(snapshot.revision)
            : gpu->stage(device,std::move(prepared),snapshot.revision,
                         spec.enableStrata,spec.maxVertices);
        if(!ok)return false;
        try{pending_.emplace(snapshot.shard,snapshot);}
        catch(...){(void)gpu->cancelStage();throw;}
        return true;
    }
    std::map<ChunkKey3,CaveRebuildRequest,KeyLess> lastLodRebuilds_;
    bool lastLodSourceConflict_=false;
    CaveShardCatalog catalog_;
    std::map<CaveShardKey,std::unique_ptr<CaveGeometrySwapOwner>,CaveShardLess> owners_;
    std::map<CaveShardKey,CaveShardSnapshot,CaveShardLess> pending_;
};
} // namespace aiverse::terraweave::caverns
