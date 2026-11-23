#pragma once
// TerraWeave Caverns Stage34: genre-independent live edit preparation host.
// A brush edit -> versioned SDF -> worker meshes -> native collision/nav
// shadow sources -> GPU shard staging. A separate EXCLUSIVE owner barrier
// remains mandatory for gameplay publication; Box3D Scene::create is live.
// Never report committed gameplay just because CPU/GPU preparation completed.
#include "engine/world/TerraWeaveCavernsEditRuntime.h"
#include "engine/world/TerraWeaveCavernsVolumePhysicsStage.h"
#include "engine/world/TerraWeaveCavernsAuthoredNavStage.h"
#include "engine/render/TerraWeaveCavernsShardGpuHost.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave::caverns {

struct CaveGameplayEditStats {
    size_t dirty=0,workers=0,cpuReady=0,gpuPending=0,gpuRetired=0;
    uint64_t requestedRevision=0,stagedRevision=0,committedRevision=0;
    bool prepared=false;
};

// Owner callback runs between physics ticks and outside render passes.
// Must quiesce ALL readers and implement real all-or-none publication of
// SpatialWorld, Box3D and Navigation before reporting success.
// NOTE: existing Stage26 shadows are NOT globally rollback-safe; a host
// without a genuine transaction must return false and keep old gameplay.
struct CaveGameplayCommitView {
    const std::vector<CavePreparedEdit>& meshes;
    const std::vector<CaveShardSnapshot>& visualShards;
    std::map<ChunkKey3,std::unique_ptr<CaveVolumePhysicsStage>,KeyLess>& physics;
    CaveAuthoredNavStage& navigation;
    const CaveVolumeSnapshot& snapshot;
    uint64_t visualRevision;
};

class CaveGameplayEditHost {
public:
    using ColliderIdAllocator=std::function<uint32_t(ChunkKey3,size_t)>;
    using TerrainHeight=CaveNavSource::Height;
    using CommitOwners=std::function<bool(const CaveGameplayCommitView&)>;

    explicit CaveGameplayEditHost(uint32_t resolution=16,
                                  int32_t spacingCm=100,
                                  CaveShardConfig shardConfig={},
                                  size_t dirtyBudget=1024,
                                  size_t workers=2)
        :worker_(resolution,spacingCm,dirtyBudget,workers),
         geometry_(shardConfig) {}

    CaveGameplayEditHost(const CaveGameplayEditHost&)=delete;
    CaveGameplayEditHost& operator=(const CaveGameplayEditHost&)=delete;

    // World authoring event. Caller supplies the exact resulting snapshot
    // and affected SDF bounds (for Undo, union of old/new influences).
    // A new version invalidates all not-yet-committed resource preparation.
    void edited(CaveDirtyBounds bounds,
                std::shared_ptr<const CaveVolumeSnapshot> source) {
        if(!source||!source->sourceRevision)
            throw std::invalid_argument("Caverns missing game edit snapshot");
        // Saved MapDoc Undo may restore an older sourceRevision. Native jobs
        // need a strictly increasing runtime epoch independent of saved JSON.
        if(runtimeEpoch_==UINT64_MAX)
            throw std::overflow_error("Caverns game edit epoch overflow");
        auto copy=std::make_shared<CaveVolumeSnapshot>(*source);
        canonicalizeCaveSnapshot(*copy);
        const uint64_t next=std::max(runtimeEpoch_,copy->sourceRevision);
        if(next==UINT64_MAX)
            throw std::overflow_error("Caverns game edit revision overflow");
        copy->sourceRevision=next+1;
        cancelPending();
        worker_.invalidate(bounds,copy);
        snapshot_=std::move(copy);
        runtimeEpoch_=snapshot_->sourceRevision;
    }

    bool consumeAuthoringOperation(const Json& result,
                                   std::shared_ptr<const CaveVolumeSnapshot> source) {
        if(!source || !result.isObject()||
           !result.getBool("ok",false)||
           !result["dirtyBounds"].isObject())return false;
        if(result.getInt("revision",0)!=int64_t(source->sourceRevision))
            throw std::invalid_argument("Caverns authoring operation revision mismatch");
        const Json& b=result["dirtyBounds"];
        edited({b.getInt("minXcm"),b.getInt("minYcm"),b.getInt("minZcm"),
                b.getInt("maxXcm"),b.getInt("maxYcm"),b.getInt("maxZcm")},
               std::move(source));
        return true;
    }

    CaveEditTick pumpCpu(size_t budget=8) {
        return worker_.tick(budget);
    }

    // Owner pre-render / between-steps operation. Stages ALL affected cave
    // collision meshes, one authored NavMesh and all modified GPU shards.
    // Must not call while the graphics device has an open render pass.
    // Global collider ID allocator must reserve disjoint ranges across
    // ALL resident engine publishers before staging.
    bool stageReady(gfx::Device& device,
                    const world::SpatialWorld& spatial,
                    CaveNavConfig navConfig,TerrainHeight terrainHeight,
                    const ColliderIdAllocator& colliderIds,
                    uint64_t visualRevision,
                    CaveBatchSpec geometryOptions={}) {
        if(!snapshot_ || !colliderIds || !terrainHeight ||
           !visualRevision || stagedRevision_)return false;
        const auto candidates=worker_.certifiedCandidates();
        if(candidates.empty())return false;
        const uint64_t sourceRevision=snapshot_->sourceRevision;
        for(const auto& c:candidates)
            if(c.candidate.sourceRevision!=sourceRevision ||
               c.dirty.sourceRevision!=sourceRevision)return false;
        // Prepare-only native physics. No Box3D body is created here.
        try {
            for(const auto& c:candidates) {
                auto& owner=physics_[c.dirty.key];
                if(!owner)owner=std::make_unique<CaveVolumePhysicsStage>();
                const auto collision=prepareCaveVolumeCollision(
                    c.candidate.mesh,c.candidate.resolution,
                    c.candidate.spacingCm,sourceRevision);
                const uint32_t firstId=colliderIds(c.dirty.key,
                                                    collision.triangles.size());
                if(!owner->prepare(c.candidate.mesh,c.candidate.resolution,
                                   c.candidate.spacingCm,sourceRevision,
                                   firstId,spatial))
                    throw std::runtime_error("Caverns collision stage refused");
                preparedPhysics_.push_back(c.dirty.key);
            }
            if(!navigation_.prepare(snapshot_,navConfig,
                                    std::move(terrainHeight)))
                throw std::runtime_error("Caverns authored navigation stage refused");
            if(!geometry_.stagePreparedEdits(device,candidates,
                                             snapshot_->generatorVersion,
                                             visualRevision,stagedShards_,
                                             geometryOptions))
                throw std::runtime_error("Caverns GPU shard stage refused");
        } catch(...) {
            cancelPending();
            throw;
        }
        stagedMeshes_=candidates;
        stagedRevision_=sourceRevision;
        stagedVisualRevision_=visualRevision;
        return true;
    }

    // Typical game-owner frame hook, after an authored event, outside an
    // active render pass: advance CPU jobs and stage the full matching
    // geometry/collision/nav resources automatically once ready. It does not
    // run or fake the later exclusive game-world commit.
    CaveEditTick advance(gfx::Device& device,
                         const world::SpatialWorld& spatial,
                         CaveNavConfig navConfig,TerrainHeight terrainHeight,
                         const ColliderIdAllocator& colliderIds,
                         uint64_t nextVisualRevision,
                         size_t cpuJobBudget=2,
                         CaveBatchSpec graphicsOptions={}) {
        auto tick=pumpCpu(cpuJobBudget);
        if(!stagedRevision_ && !tick.inFlight && tick.pending &&
           tick.ready==tick.pending) {
            // stageReady is deliberately fail-closed; callers can inspect
            // stats and retry if a GPU budget or a source validation fails.
            (void)stageReady(device,spatial,navConfig,std::move(terrainHeight),
                             colliderIds,nextVisualRevision,graphicsOptions);
        }
        return tick;
    }

    // No implicit success callback: the host must actually own a quiescent,
    // rollback-safe physical/nav commit. In particular, the Stage26 Box3D
    // create+destroy procedure is not by itself a global atomic transaction.
    // The callback is invoked before visual activation, with old GPU scene
    // still visible. If false, gameplay dirty queue remains outstanding.
    bool commitAtOwnerBarrier(const CommitOwners& commitOwners) {
        if(!stagedRevision_||!snapshot_||!commitOwners ||
           snapshot_->sourceRevision!=stagedRevision_ ||
           !navigation_.staged(stagedRevision_) ||
           preparedPhysics_.size()!=stagedMeshes_.size())return false;
        for(const auto& key:preparedPhysics_){
            const auto it=physics_.find(key);
            if(it==physics_.end()||!it->second->prepared(stagedRevision_))
                return false;
        }
        const bool accepted=worker_.publishReady([&](const auto& current){
            if(current.size()!=stagedMeshes_.size())return false;
            for(size_t i=0;i<current.size();++i)
                if(current[i].dirty.key!=stagedMeshes_[i].dirty.key ||
                   current[i].dirty.serial!=stagedMeshes_[i].dirty.serial ||
                   current[i].candidate.renderRevision!=
                       stagedMeshes_[i].candidate.renderRevision)
                    return false;
            return geometry_.activateGroup(stagedShards_,
                [&](const std::vector<CaveShardSnapshot>& shards) {
                    return commitOwners(CaveGameplayCommitView{
                        current,shards,physics_,navigation_,*snapshot_,
                        stagedVisualRevision_});
                });
        });
        if(!accepted)return false;
        committedRevision_=stagedRevision_;
        stagedRevision_=stagedVisualRevision_=0;
        stagedMeshes_.clear();
        stagedShards_.clear();
        preparedPhysics_.clear();
        return true;
    }

    void cancelPending() {
        for(const auto& key:stagedShards_)(void)geometry_.cancel(key);
        stagedShards_.clear();
        for(const auto& key:preparedPhysics_) {
            const auto it=physics_.find(key);
            if(it!=physics_.end())(void)it->second->abort(stagedRevision_
                ?stagedRevision_:(snapshot_?snapshot_->sourceRevision:0));
        }
        preparedPhysics_.clear();
        if(snapshot_&&navigation_.staged(snapshot_->sourceRevision))
            (void)navigation_.abort(snapshot_->sourceRevision);
        stagedMeshes_.clear();
        stagedRevision_=stagedVisualRevision_=0;
    }

    void registerCommittedMesh(CaveCertifiedChunk mesh,
                               uint64_t generatorVersion) {
        CaveBatchChunk source;
        source.mesh=mesh.mesh;
        source.generatorVersion=generatorVersion;
        source.revision=mesh.renderRevision;
        source.resolution=mesh.resolution;
        source.spacingCm=mesh.spacingCm;
        if(!geometry_.registerCommittedSource(source))
            throw std::invalid_argument("Caverns committed mesh registration refused");
        worker_.registerResident(std::move(mesh));
    }

    void draw(render::Renderer3D& renderer){geometry_.draw(renderer);}
    bool collectAfterGpuIdle(gfx::Device& device){
        return geometry_.collectAfterGpuIdle(device);
    }
    bool shutdownAfterGpuIdle(gfx::Device& device){
        cancelPending();
        worker_.drain();
        return geometry_.shutdownAfterGpuIdle(device);
    }
    CaveGameplayEditStats stats()const {
        const auto gpu=geometry_.stats();
        return {worker_.dirtyCount(),worker_.inFlight(),worker_.preparedCount(),
                gpu.pendingShards,gpu.retiredScenes,
                snapshot_?snapshot_->sourceRevision:0,
                stagedRevision_,committedRevision_,stagedRevision_!=0};
    }
    std::vector<ChunkKey3> pendingEditKeys()const{return worker_.dirtyKeys();}
    std::shared_ptr<const CaveVolumeSnapshot> sourceSnapshot()const {
        return snapshot_;
    }
    const CaveAuthoredNavStage& navigation()const noexcept{return navigation_;}
    const CaveShardGpuHost& geometry()const noexcept{return geometry_;}
    const std::map<ChunkKey3,std::unique_ptr<CaveVolumePhysicsStage>,KeyLess>&
    physicsStages()const noexcept{return physics_;}
private:
    CaveEditRuntimeBridge worker_;
    CaveShardGpuHost geometry_;
    CaveAuthoredNavStage navigation_;
    std::map<ChunkKey3,std::unique_ptr<CaveVolumePhysicsStage>,KeyLess> physics_;
    std::shared_ptr<const CaveVolumeSnapshot> snapshot_;
    std::vector<CavePreparedEdit> stagedMeshes_;
    std::vector<CaveShardKey> stagedShards_;
    std::vector<ChunkKey3> preparedPhysics_;
    uint64_t stagedRevision_=0,stagedVisualRevision_=0,committedRevision_=0;
    uint64_t runtimeEpoch_=0;
};
} // namespace aiverse::terraweave::caverns
