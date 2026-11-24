#pragma once
// Stage51 native projection with explicit timeline restore fencing.
// The runtime epoch fences asynchronous jobs; it is NOT a saved SDF revision.
// No RHI/physics work occurs from inside an ECS command or fixed system.
#include "engine/world/TerraWeaveCavernsEcsAuthority.h"
#include "engine/world/TerraWeaveCavernsRuntimeFrame.h"

namespace aiverse::terraweave::caverns {
class CaveEcsFrameBridge {
public:
    explicit CaveEcsFrameBridge(CaveAuthorityConfig cfg):cfg_(std::move(cfg)){
        validateCaveAuthorityConfig(cfg_);
    }
    CaveEcsFrameBridge(const CaveEcsFrameBridge&)=delete;
    CaveEcsFrameBridge& operator=(const CaveEcsFrameBridge&)=delete;
    // Register during setup. The weak marker avoids dangling frame captures;
    // the hook only invalidates readiness, never mutates ECS or native owners.
    // Simulation and native frame synchronization are owner-thread operations.
    void installRestoreHook(Simulation& sim){
        if(hookInstalled_)throw std::logic_error("Caverns restore hook already installed");
        std::weak_ptr<RestoreMarker> weak=marker_;
        sim.addStateRestoredHook([weak](Simulation&){
            if(auto marker=weak.lock()){
                marker->dirty=true;
            }
        });
        hookInstalled_=true;
    }
    bool needsRestore()const noexcept{return marker_->dirty;}
    bool restoreAfterStateLoad(CaveGameplayRuntimeFrame& frame,const World& frozen,
                               CaveDirtyBounds fallback,
                               const CaveGameplayPhysicsBarrier::ExclusiveProof& exclusive){
        if(frame_!=&frame||!needsRestore()||!exclusive||!exclusive())return false;
        auto snapshot=freezeCaveAuthority(frozen,cfg_);
        const auto revision=snapshot->sourceRevision;
        frame.restoreAuthoredWorld(fallback,std::move(snapshot),cfg_.maxDirtyChunks,exclusive);
        requestedAuthority_=revision;
        requestedNative_=frame.stats().requestedRevision;
        marker_->dirty=false;
        return true;
    }
    // The world must be immutable for the duration of capture. The owner
    // controls the initial loaded extent, and the configured chunk budget.
    void open(CaveGameplayRuntimeFrame& frame,const World& frozen,
              CaveDirtyBounds loadedBounds){
        if(requestedAuthority_||frame.stats().requestedRevision||frame.closed())
            throw std::logic_error("Caverns authority bridge already opened");
        (void)dirtyCaveChunks(loadedBounds,cfg_.resolution,cfg_.spacingCm,cfg_.maxDirtyChunks);
        auto snapshot=freezeCaveAuthority(frozen,cfg_);
        frame.setNavigationChunkDimensions(cfg_.resolution,cfg_.spacingCm);
        const auto revision=snapshot->sourceRevision;
        frame.openWorld(loadedBounds,std::move(snapshot));
        requestedAuthority_=revision;
        requestedNative_=frame.stats().requestedRevision;
        frame_=&frame;
    }
    // Supply all accepted changes since last capture, in command order.
    // A missed revision is NOT silently treated as a local dirty edit.
    bool syncAfterTick(CaveGameplayRuntimeFrame& frame,const World& frozen,
                       const std::vector<CaveAuthorityChange>& changes){
        if(needsRestore()||frame.restoringAuthoredWorld()||frame_!=&frame||!requestedAuthority_||frame.closed()||
           frame.stats().requestedRevision!=requestedNative_||
           frame.physicsStatus().recoveryRequired||changes.size()>1024)return false;
        auto snapshot=freezeCaveAuthority(frozen,cfg_);
        uint64_t cursor=requestedAuthority_;
        CaveDirtyBounds bounds{};
        bool changed=false;
        for(const auto& delta:changes){
            if(delta.oldRevision!=cursor)return false;
            if(!delta.changed){
                if(delta.newRevision!=cursor)return false;
                continue;
            }
            if(cursor==UINT64_MAX||delta.newRevision!=cursor+1||
               delta.bounds.maxX<=delta.bounds.minX||
               delta.bounds.maxY<=delta.bounds.minY||
               delta.bounds.maxZ<=delta.bounds.minZ)return false;
            if(!changed)bounds=delta.bounds;
            else authority_detail::mergeBounds(bounds,delta.bounds);
            changed=true;cursor=delta.newRevision;
        }
        if(snapshot->sourceRevision!=cursor)return false;
        if(!changed)return true;
        (void)dirtyCaveChunks(bounds,cfg_.resolution,cfg_.spacingCm,cfg_.maxDirtyChunks);
        frame.onEditedVolume(bounds,std::move(snapshot));
        requestedAuthority_=cursor;
        requestedNative_=frame.stats().requestedRevision;
        return true;
    }
    // Gate native readers on matching epochs and completed invalidations.
    // Not a lockstep tick driver: caller controls deterministic readiness.
    bool readyFor(const CaveGameplayRuntimeFrame& frame,const World& frozen)const {
        const auto* state=frozen.resourceConst<CaveAuthorityResource>();
        const auto stats=frame.stats();
        return !needsRestore()&&!frame.restoringAuthoredWorld()&&frame_==&frame&&state&&state->recipeId==cfg_.recipeId&&
            state->volume.generatorVersion==cfg_.generatorVersion&&
            requestedAuthority_!=0&&state->volume.sourceRevision==requestedAuthority_&&
            requestedNative_!=0&&stats.requestedRevision==requestedNative_&&
            stats.committedRevision==requestedNative_&&!frame.closed()&&
            !frame.physicsStatus().recoveryRequired&&!frame.navigationInvalidationsPending()&&
            bool(frame.gameplayNavigationSource());
    }
    uint64_t requestedAuthorityRevision()const noexcept{return requestedAuthority_;}
    uint64_t requestedNativeRevision()const noexcept{return requestedNative_;}
private:
    struct RestoreMarker {bool dirty=false;};
    std::shared_ptr<RestoreMarker> marker_=std::make_shared<RestoreMarker>();
    bool hookInstalled_=false;
    CaveAuthorityConfig cfg_;
    const CaveGameplayRuntimeFrame* frame_=nullptr;
    uint64_t requestedAuthority_=0,requestedNative_=0;
};
} // namespace aiverse::terraweave::caverns
