#pragma once
// Host-owned native integration. No new world, scheduler or physics scene.
#include "engine/world/TerraWeaveCavernsPlanetBaselineCollision.h"
#include "engine/world/TerraWeaveCavernsPlanetNativeOwner.h"

namespace aiverse::terraweave::caverns {
class CavePlanetCollisionBinding {
    using Owner=CavePlanetNativeOwner;
public:
    using Exclusive=Owner::Exclusive;
    CavePlanetCollisionBinding(Owner& owner,game::PlanetRenderer& planet,
            physics::Scene& box,world::SpatialWorld& spatial,CaveColliderLeasePool& pool)
        :owner_(owner),planet_(planet),spatial_(spatial),box_(box),pool_(pool),
         baseline_(box,spatial,pool){}
    CavePlanetCollisionBinding(const CavePlanetCollisionBinding&)=delete;
    CavePlanetCollisionBinding& operator=(const CavePlanetCollisionBinding&)=delete;
    bool recoveryRequired()const noexcept{
        return recovery_||owner_.recoveryRequired()||baseline_.recoveryRequired();
    }
    const CavePlanetBaselineCollision& baseline()const noexcept{return baseline_;}
    bool matchesBaselinePlan(const CavePlanetBodyCutPlan& plan)const{return matchesBaseline(plan);}
    // Capture the actual uncut renderer leaves. The caller owns the global
    // coverage registry and must prove no independent baseline source
    // overlaps these leaves. Other terrain remains owned by that host.
    bool initializeBaseline(const CavePlanetBodyCutPlan& plan,
            const CavePlanetBaselineCollision::Cells& cells,
            const CavePlanetBaselineCollision::Unowned& unowned,const Exclusive& exclusive){
        if(recoveryRequired()||owner_.staged()||owner_.activeRevision()||
           !exclusive||!exclusive()||planet_.caveSurfaceCutEpoch(owner_.frame().body())||
           planet_.caveSurfaceRevision()!=plan.surfaceRevision)return false;
        if(plan.bodyId!=owner_.frame().anchor().bodyId)return false;
        const auto slots=collectPlanetBodyCutSlots(plan);
        std::vector<CavePlanetSurfaceSlotKey> keys;
        for(const auto& [key,ref]:slots){(void)ref;keys.push_back(key);}
        const auto source=planet_.captureCaveSurfaceSlots(owner_.frame().body(),keys);
        if(!baseline_.prepare(owner_.frame().body(),plan.surfaceRevision,source,cells,unowned,exclusive))return false;
        if(!matchesBaseline(plan)){
            (void)baseline_.shutdown(exclusive);return false;
        }
        if(baseline_.publish(exclusive))return true;
        if(!baseline_.recoveryRequired())(void)baseline_.shutdown(exclusive);
        return false;
    }
    // The host supplies current ECS state; no stale region revision is
    // allowed to stage or commit. Rollback must cancel pending work and
    // rebuild plans from the restored snapshot, even at equal revisions.
    bool stage(gfx::Device& device,const World& ecs,
            std::shared_ptr<const CavePlanetBodyCutPlan> plan,
            std::vector<CavePlanetNativeSlot> volume,uint64_t nativeRevision,
            const Owner::BodySurfaceCellAllocator& cells,const Exclusive& exclusive){
        if(recoveryRequired()||!exclusive||!exclusive()||!plan||owner_.staged()||
           !pendingLeases_.empty()||!matchesBaseline(*plan)||!matchesAuthority(ecs,*plan)||
           (!baseline_.solidReady()&&!baseline_.cutReady())||
           planet_.caveSurfaceRevision()!=plan->surfaceRevision)return false;
        pendingLeases_.reserve(128); // no allocation after a lease is acquired
        try{
            const bool ok=owner_.stageBodyCut(device,spatial_,plan,std::move(volume),nativeRevision,cells,
                [&](const CavePlanetSlotId&,size_t count){
                    const auto lease=pool_.reserve(uint32_t(count));
                    if(!lease)throw std::length_error("Caverns native collider namespace exhausted");
                    pendingLeases_.push_back(*lease);return lease->first;
                });
            if(!ok){if(!owner_.recoveryRequired())release(pendingLeases_);return false;}
            pending_=std::move(plan);return true;
        }catch(...){if(!owner_.recoveryRequired())release(pendingLeases_);throw;}
    }
    bool commit(const World& ecs,const Exclusive& exclusive,
            const std::function<bool(uint64_t)>& commitNavigationAndGi){
        if(recoveryRequired()||!pending_||!exclusive||!exclusive()||
           !matchesAuthority(ecs,*pending_)||
           (!baseline_.solidReady()&&!baseline_.cutReady())||!commitNavigationAndGi)return false;
        if(!owner_.commit(planet_,box_,spatial_,exclusive,
                [&]{return baseline_.retireForCut(exclusive);},commitNavigationAndGi))return false;
        if(!release(activeLeases_))return false;
        activeLeases_.swap(pendingLeases_);pending_.reset();return true;
    }
    bool cancel(const Exclusive& exclusive){
        if(recoveryRequired()||!exclusive||!exclusive())return false;
        if(owner_.staged()&&!owner_.cancelStaged())return false;
        pending_.reset();return release(pendingLeases_);
    }
    // Pre-stage real baseline collision before the native owner retires
    // cut geometry. Restore the exact originals inside the same barrier.
    // The host must commit actual nav/GI retirement as part of this call.
    bool closeCut(gfx::Device& device,const Exclusive& exclusive,
            const std::function<bool()>& retireNavigationAndGi){
        if(recoveryRequired()||!exclusive||!exclusive()||!retireNavigationAndGi||
           !cancel(exclusive))return false;
        const bool restoring=owner_.activeRevision()!=0;
        if(restoring&&!baseline_.prepareRestore(exclusive))return false;
        if(!restoring&&!baseline_.solidReady())return false;
        try{
            const bool ok=owner_.close(device,planet_,box_,spatial_,exclusive,[&]{
                return (!restoring||baseline_.publish(exclusive))&&retireNavigationAndGi();
            });
            if(!ok){
                if(!owner_.recoveryRequired()&&restoring&&
                   baseline_.phase()==CavePlanetBaselineCollision::Phase::RestorePrepared)
                    (void)baseline_.cancelRestore();
                return false;
            }
        }catch(...){recovery_=true;throw;}
        return release(activeLeases_)&&baseline_.solidReady();
    }
    bool shutdownBaseline(const Exclusive& exclusive){
        return !recoveryRequired()&&!owner_.staged()&&!owner_.activeRevision()&&
            activeLeases_.empty()&&pendingLeases_.empty()&&baseline_.shutdown(exclusive);
    }
private:
    bool matchesBaseline(const CavePlanetBodyCutPlan& plan)const{
        std::map<CavePlanetSurfaceSlotKey,const CavePlanetCutSlot*> slots;
        for(const auto& [key,ref]:collectPlanetBodyCutSlots(plan))slots.emplace(key,ref.slot);
        return baseline_.matches(owner_.frame().body(),plan.surfaceRevision,slots);
    }
    static bool matchesAuthority(const World& ecs,const CavePlanetBodyCutPlan& plan){
        for(const auto& [id,p]:plan.regions){
            const auto* entry=findPlanetCaveRegion(ecs,id);
            if(!entry||!p||!sameCavePlanetAnchor(entry->region.anchor,p->anchor)||
               entry->authority.volume.sourceRevision!=p->authorityRevision)return false;
        }
        return true;
    }
    bool release(std::vector<ColliderLease>& leases){
        for(auto& lease:leases){
            if(!lease.token)continue;
            for(uint64_t n=lease.first;n<uint64_t(lease.first)+lease.count;++n)
                if(spatial_.colliders().contains(uint32_t(n))){recovery_=true;return false;}
            if(!pool_.release(lease)){recovery_=true;return false;}
            lease={};
        }
        leases.clear();return true;
    }
    Owner& owner_;game::PlanetRenderer& planet_;world::SpatialWorld& spatial_;
    physics::Scene& box_;CaveColliderLeasePool& pool_;
    CavePlanetBaselineCollision baseline_;
    std::vector<ColliderLease> activeLeases_,pendingLeases_;
    std::shared_ptr<const CavePlanetBodyCutPlan> pending_;
    bool recovery_=false;
};
} // namespace aiverse::terraweave::caverns
