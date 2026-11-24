#pragma once
// Opt-in exterior terrain coverage on the same actual physics owners.
// This is a derived leaf residency registry, not a second terrain solver.
#include "engine/world/TerraWeaveCavernsPlanetRuntimeFrame.h"
#include <map>
#include <memory>
#include <set>

namespace aiverse::terraweave::caverns {
class CavePlanetExteriorCollision {
    using Key=CavePlanetSurfaceSlotKey;
    using Baseline=CavePlanetBaselineCollision;
    using LeafId=std::pair<std::string,Key>;
    struct Leaf {
        std::unique_ptr<Baseline> owner;
        world::Cell cell;
        bool loaned=false;
    };
public:
    using CellSource=std::function<world::Cell(const std::string&,const Key&)>;
    using OwnershipProof=std::function<bool(const std::string&,const Key&)>;
    CavePlanetExteriorCollision(game::PlanetRenderer& planet,physics::Scene& scene,
            world::SpatialWorld& spatial,CaveColliderLeasePool& pool,
            Baseline::Exclusive exclusive,CellSource cells,OwnershipProof unowned,size_t maxLeaves=4096)
        :planet_(planet),scene_(scene),spatial_(spatial),pool_(pool),exclusive_(std::move(exclusive)),
         cells_(std::move(cells)),unowned_(std::move(unowned)),maxLeaves_(maxLeaves){
        if(!exclusive_||!cells_||!unowned_||!maxLeaves_||maxLeaves_>65536)
            throw std::invalid_argument("Planet exterior collision bindings");
    }
    CavePlanetExteriorCollision(const CavePlanetExteriorCollision&)=delete;
    CavePlanetExteriorCollision& operator=(const CavePlanetExteriorCollision&)=delete;
    bool recoveryRequired()const noexcept{
        if(recovery_)return true;
        for(const auto& [id,leaf]:leaves_){(void)id;if(leaf.owner->recoveryRequired())return true;}
        return false;
    }
    size_t size()const noexcept{return leaves_.size();}
    bool solid(const std::string& body,const Key& key)const{
        const auto it=leaves_.find({body,key});
        return !recoveryRequired()&&it!=leaves_.end()&&!it->second.loaned&&
            it->second.owner->surfaceRevision()==planet_.caveSurfaceRevision()&&it->second.owner->solidReady();
    }
    bool loaned(const std::string& body,const Key& key)const{
        const auto it=leaves_.find({body,key});return it!=leaves_.end()&&it->second.loaned;
    }
    // Load one exact renderer leaf, never infer foreign ownership from a
    // spatial overlap. Global host must certify the leaf/cell namespace.
    bool load(const std::string& body,const Key& key){
        if(recoveryRequired()||!exclusive_())return false;
        if(leaves_.contains({body,key}))return solid(body,key);
        if(leaves_.size()>=maxLeaves_||!unowned_(body,key))return false;
        try{
            const int index=planet_.world().find(body);
            if(index<0)return false;
            const auto revision=planet_.caveSurfaceRevision();
            const auto source=planet_.captureCaveSurfaceSlots(index,std::span<const Key>(&key,1));
            if(source.size()!=1)return false;
            const auto cell=cells_(body,key);
            for(const auto& [id,leaf]:leaves_){(void)id;if(leaf.cell==cell)return false;}
            auto owner=std::make_unique<Baseline>(scene_,spatial_,pool_);
            auto [it,inserted]=leaves_.emplace(LeafId{body,key},Leaf{std::move(owner),cell,false});
            if(!inserted)return false;
            auto& baseline=*it->second.owner;
            try{
                if(!baseline.prepare(index,revision,source,[&](const Key&){return cell;},
                       [&](const Key& k){return unowned_(body,k);},exclusive_)||
                   planet_.caveSurfaceRevision()!=revision||!baseline.publish(exclusive_)){
                    if(baseline.recoveryRequired()||!baseline.shutdown(exclusive_)){recovery_=true;return false;}
                    leaves_.erase(it);return false;
                }
            }catch(...){
                if(baseline.recoveryRequired()||!baseline.shutdown(exclusive_))recovery_=true;
                else leaves_.erase(it);
                return false;
            }
            if(!baseline.solidReady()){recovery_=true;return false;}
            return true;
        }catch(...){return false;}
    }
    // Exact known leaf set only. Loaned coverage must be proven by the
    // committed cave runtime, not by the presence of a reservation.
    bool covers(const CavePlanetRuntimeFrame& runtime,const World& world,const std::string& body,
            std::span<const Key> keys)const{
        if(keys.empty()||recoveryRequired())return false;
        const auto* set=world.resourceConst<CavePlanetAuthoritySet>();
        for(const auto& key:keys){
            if(solid(body,key))continue;
            if(!loaned(body,key)||!set)return false;
            bool found=false;
            for(const auto& region:set->regions){
                if(region.region.anchor.bodyId!=body)continue;
                const auto cut=runtime.committedRegionCut(world,region.id);
                if(cut&&cut->slots.contains(key)){found=true;break;}
            }
            if(!found)return false;
        }
        return true;
    }
    bool borrow(const CavePlanetBodyCutPlan& plan,Baseline& destination){
        if(recoveryRequired()||!exclusive_()||destination.phase()!=Baseline::Phase::Empty||
           plan.surfaceRevision!=planet_.caveSurfaceRevision())return false;
        try{
            const auto slots=collectPlanetBodyCutSlots(plan);
            const int index=planet_.world().find(plan.bodyId);
            if(index<0)return false;
            std::set<world::Cell> cells;
            for(const auto& [key,ref]:slots){
                const auto it=leaves_.find({plan.bodyId,key});
                if(it==leaves_.end()||!solid(plan.bodyId,key)||!cells.insert(it->second.cell).second||
                   !it->second.owner->matches(index,plan.surfaceRevision,{{key,ref.slot}})||
                   !it->second.owner->canTransferSolidLeafTo(key,destination))return false;
            }
            for(const auto& [key,ref]:slots){
                (void)ref;auto& leaf=leaves_.at({plan.bodyId,key});
                if(!leaf.owner->transferSolidLeafTo(key,destination,exclusive_)){recovery_=true;return false;}
                leaf.loaned=true;
            }
            return true;
        }catch(...){if(destination.size())recovery_=true;return false;}
    }
    bool giveBack(const CavePlanetBodyCutPlan& plan,Baseline& source){
        if(recoveryRequired()||!exclusive_()||!source.solidReady())return false;
        const auto count=source.size();
        try{
            const auto slots=collectPlanetBodyCutSlots(plan);
            const int index=planet_.world().find(plan.bodyId);
            std::map<Key,const CavePlanetCutSlot*> originals;
            for(const auto& [key,ref]:slots)originals.emplace(key,ref.slot);
            if(index<0||!source.matches(index,plan.surfaceRevision,originals))return false;
            for(const auto& [key,ref]:slots){
                (void)ref;const auto it=leaves_.find({plan.bodyId,key});
                if(it==leaves_.end()||!it->second.loaned||
                   it->second.owner->phase()!=Baseline::Phase::Empty||
                   !source.canTransferSolidLeafTo(key,*it->second.owner))return false;
            }
            for(const auto& [key,ref]:slots){
                (void)ref;auto& leaf=leaves_.at({plan.bodyId,key});
                if(!source.transferSolidLeafTo(key,*leaf.owner,exclusive_)){recovery_=true;return false;}
                leaf.loaned=false;
            }
            return source.phase()==Baseline::Phase::Empty;
        }catch(...){if(source.size()!=count)recovery_=true;return false;}
    }
    // Retiring exterior coverage itself requires actual host safety proof.
    bool unload(const std::string& body,const Key& key,const OwnershipProof& safe){
        if(recoveryRequired()||!exclusive_()||!safe||!safe(body,key))return false;
        const auto it=leaves_.find({body,key});
        if(it==leaves_.end())return true;
        if(it->second.loaned||!it->second.owner->shutdown(exclusive_))return false;
        leaves_.erase(it);return true;
    }
    // The project may prefetch a batch of exact leaves at its frame barrier.
    // Partial success remains ordinary exterior coverage, not a loan.
    bool ensure(const std::string& body,std::span<const Key> keys){
        if(keys.empty()||keys.size()>maxLeaves_||!exclusive_()||recoveryRequired())return false;
        for(const auto& key:keys)if(!solid(body,key)&&!loaned(body,key)&&!load(body,key))return false;
        return true;
    }
    bool bind(CavePlanetRuntimeHooks& hooks){
        if(hooks.borrowExteriorBaseline||hooks.returnExteriorBaseline||hooks.surfaceCell||hooks.exteriorHealthy)return false;
        hooks.surfaceCell=cells_;
        hooks.exteriorHealthy=[this]{return !recoveryRequired();};
        hooks.borrowExteriorBaseline=[this](const auto& plan,auto& owner){return borrow(plan,owner);};
        hooks.returnExteriorBaseline=[this](const auto& plan,auto& owner){return giveBack(plan,owner);};
        return true;
    }
private:
    game::PlanetRenderer& planet_;physics::Scene& scene_;world::SpatialWorld& spatial_;
    CaveColliderLeasePool& pool_;Baseline::Exclusive exclusive_;
    CellSource cells_;OwnershipProof unowned_;size_t maxLeaves_;
    std::map<LeafId,Leaf> leaves_;bool recovery_=false;
};
} // namespace aiverse::terraweave::caverns
