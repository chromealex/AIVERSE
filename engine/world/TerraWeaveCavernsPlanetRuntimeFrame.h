#pragma once
// Genre-independent native host frame; all mutable gameplay density stays
// in ECS. This owner coordinates derived resources on the host's existing
// graphics/physics thread, never inside a draw pass or simulation worker.
#include "engine/world/TerraWeaveCavernsPlanetRuntimeBuild.h"
#include "engine/world/TerraWeaveCavernsPlanetCollisionBinding.h"
#include "engine/world/TerraWeaveCavernsPlanetResidency.h"

namespace aiverse::terraweave::caverns {
struct CavePlanetRuntimeHooks {
    CavePlanetNativeOwner::Exclusive exclusive;
    std::function<world::Cell(const std::string&,const CavePlanetSurfaceSlotKey&)> surfaceCell;
    std::function<world::Cell(uint64_t,ChunkKey3)> volumeCell;
    std::function<bool(const std::string&,const CavePlanetSurfaceSlotKey&)> unownedLeaf;
    // Actual host proof: no occupant/readers depend on old cave volume;
    // the host can remove these baseline leaves without losing required ground.
    // A true answer cannot simply mean the camera moved away.
    std::function<bool(const CavePlanetBodyCutPlan&)> mayReleaseCoverage;
    // Stage existing navigation/GI and validate actor safety before native
    // publication. A false/throw may have staged resources: abort is mandatory.
    std::function<bool(const CavePlanetRuntimeCandidate&)> prepareNavigationGi;
    std::function<bool(const std::string&,uint64_t)> commitNavigationGi;
    std::function<bool(const std::string&,uint64_t)> abortNavigationGi;
    std::function<bool(const std::string&,uint64_t)> retireNavigationGi;
    bool valid()const noexcept{
        return exclusive&&surfaceCell&&volumeCell&&unownedLeaf&&mayReleaseCoverage&&
            prepareNavigationGi&&commitNavigationGi&&abortNavigationGi&&retireNavigationGi;
    }
};
enum class CavePlanetRuntimePhase {Idle,Building,WaitingCoverage,Ready,Refused,Recovery};
struct CavePlanetRuntimeProgress {
    CavePlanetRuntimePhase phase=CavePlanetRuntimePhase::Idle;
    size_t residentBodies=0,residentRegions=0,workers=0;
    bool pinsReady=false,allDemandReady=false;
    std::vector<uint64_t> refused;
    std::string diagnostic;
};
class CavePlanetRuntimeFrame {
    struct Cohort {
        std::unique_ptr<CavePlanetCollisionBinding> binding;
        std::shared_ptr<const CavePlanetBodyCutPlan> coverage;
        std::optional<CavePlanetRuntimeCandidate> active;
        std::vector<uint64_t> ids;
    };
public:
    CavePlanetRuntimeFrame(gfx::Device& device,game::GameView& view,
            physics::Scene& box,world::SpatialWorld& spatial,CaveColliderLeasePool& pool,
            std::shared_ptr<const game::Surface> surface,CavePlanetRuntimeHooks hooks,
            CavePlanetDemandConfig config={},size_t workers=2)
        :device_(device),view_(view),planet_(view.planets()),box_(box),spatial_(spatial),pool_(pool),
         surface_(std::move(surface)),hooks_(std::move(hooks)),config_(config),builder_(workers){
        if(!surface_||!surface_->tab||!hooks_.valid())throw std::invalid_argument("Caverns runtime host hooks");
        // Validate configuration before attaching the native draw hook.
        (void)selectPlanetCaveDemand(CavePlanetAuthoritySet{},surface_,{},{},{},config_);
        residency_.attach(view_);
    }
    CavePlanetRuntimeFrame(const CavePlanetRuntimeFrame&)=delete;
    CavePlanetRuntimeFrame& operator=(const CavePlanetRuntimeFrame&)=delete;
    bool recoveryRequired()const noexcept{
        if(recovery_||residency_.recoveryRequired())return true;
        for(const auto& [body,c]:cohorts_){(void)body;if(!c.binding||c.binding->recoveryRequired())return true;}
        return false;
    }
    // Called by the real host on state restoration, even if restored
    // numeric revisions equal the old ones. The next frame drains stale jobs.
    void invalidateAfterRestore(){
        if(generation_==UINT64_MAX){recovery_=true;return;}
        ++generation_;builder_.cancel();candidate_.reset();buildingBody_.clear();buildingIds_.clear();
        failed_.clear();
    }
    void retryRefused(){failed_.clear();}
    bool regionReady(const World& ecs,uint64_t id)const{
        if(closed_||recoveryRequired())return false;
        const auto* entry=findPlanetCaveRegion(ecs,id);
        if(!entry)return false;
        const auto found=cohorts_.find(entry->region.anchor.bodyId);
        if(found==cohorts_.end()||!found->second.active)return false;
        const auto* owner=residency_.find(found->first);
        const auto& active=*found->second.active;
        return owner&&owner->ready()&&found->second.binding->baseline().cutReady()&&
            active.sources.contains(id)&&active.current(ecs,generation_)&&
            active.cut->surfaceRevision==planet_.caveSurfaceRevision();
    }
    // The host MUST use this readiness in its actual movement/admission gate;
    // render visibility is not permission to move through a cave.
    bool requiredRegionsReady(const World& ecs,const std::set<uint64_t>& required)const{
        if(closed_||recoveryRequired())return false;
        for(auto id:required)if(!regionReady(ecs,id))return false;
        return true;
    }
    CavePlanetRuntimeProgress pump(const World& ecs,std::span<const CavePlanetObserver> observers,
            const std::set<uint64_t>& pins,size_t maxDispatch=2){
        CavePlanetRuntimeProgress out;
        const auto report=[&]{
            out.residentBodies=cohorts_.size();out.workers=builder_.inFlight();
            for(const auto& [body,c]:cohorts_){(void)body;if(c.active)out.residentRegions+=c.ids.size();}
            out.pinsReady=!lastDemand_.pinnedOverflow&&requiredRegionsReady(ecs,pins);
            out.allDemandReady=out.pinsReady&&lastDemand_.refused.empty();
            for(const auto& [body,ids]:lastDemand_.bodies){
                (void)body;for(auto id:ids)out.allDemandReady&=regionReady(ecs,id);
            }
            if(recoveryRequired()){out.phase=CavePlanetRuntimePhase::Recovery;out.pinsReady=out.allDemandReady=false;}
            return out;
        };
        if(closed_||recoveryRequired()||!hooks_.exclusive()||!maxDispatch||maxDispatch>32){
            out.phase=recoveryRequired()?CavePlanetRuntimePhase::Recovery:CavePlanetRuntimePhase::Refused;
            out.diagnostic="Caverns owner closed, recovering or readers not excluded";
            out.pinsReady=out.allDemandReady=false;return out;
        }
        try{
        const auto* set=ecs.resourceConst<CavePlanetAuthoritySet>();
        const CavePlanetAuthoritySet empty;
        std::set<uint64_t> resident;
        for(const auto& [body,c]:cohorts_){(void)body;if(c.active)resident.insert(c.ids.begin(),c.ids.end());}
        lastDemand_=selectPlanetCaveDemand(set?*set:empty,surface_,observers,resident,pins,config_);
        out.refused=lastDemand_.refused;
        if(lastDemand_.pinnedOverflow){
            out.phase=CavePlanetRuntimePhase::Refused;out.diagnostic="Pinned cave demand exceeds budget";return report();
        }
        const auto wanted=[&](const std::string& body,const std::vector<uint64_t>& ids){
            const auto it=lastDemand_.bodies.find(body);
            return it!=lastDemand_.bodies.end()&&it->second==ids;
        };
        if(builder_.busy()&&(!wanted(buildingBody_,buildingIds_)||!builder_.current(ecs,generation_))){
            builder_.cancel();buildingBody_.clear();buildingIds_.clear();
        }
        if(candidate_&&(!wanted(buildingBody_,buildingIds_)||
                !candidate_->current(ecs,generation_)||candidate_->cut->surfaceRevision!=planet_.caveSurfaceRevision())){
            candidate_.reset();buildingBody_.clear();buildingIds_.clear();
        }
        // Retire obsolete bodies before consuming their resident budget.
        // In particular, a failed coverage proof is not permission to evict.
        for(auto it=cohorts_.begin();it!=cohorts_.end();++it){
            if(lastDemand_.bodies.contains(it->first))continue;
            if(!retire(it->first,pins)){
                out.phase=CavePlanetRuntimePhase::WaitingCoverage;out.diagnostic="Old body coverage still required";return report();
            }
            out.phase=CavePlanetRuntimePhase::Idle;return report();
        }
        (void)residency_.collect(device_);
        if(!builder_.busy()&&!builder_.pollCancelled()){
            out.phase=CavePlanetRuntimePhase::Building;return report();
        }
            if(candidate_){
                if(!publishCandidate(ecs,pins)){
                    out.phase=CavePlanetRuntimePhase::WaitingCoverage;out.diagnostic="Candidate publication/coverage refused";
                    return report();
                }
                out.phase=CavePlanetRuntimePhase::Ready;return report();
            }
            if(builder_.busy()){
                if(builder_.pump(maxDispatch))candidate_=builder_.finish(hooks_.volumeCell);
                out.phase=CavePlanetRuntimePhase::Building;return report();
            }
            for(const auto& [body,ids]:lastDemand_.bodies){
                const auto found=cohorts_.find(body);
                if(found!=cohorts_.end()&&found->second.active&&found->second.ids==ids&&
                   std::all_of(ids.begin(),ids.end(),[&](uint64_t id){return regionReady(ecs,id);}))continue;
                // Do not exceed global resident budget while old cohorts linger.
                size_t otherSlots=0,otherRegions=0;
                for(const auto& [name,c]:cohorts_)if(name!=body){
                    otherRegions+=c.ids.size();
                    for(auto id:c.ids){
                        const auto* e=findPlanetCaveRegion(ecs,id);
                        if(!e)throw std::logic_error("Caverns resident recipe removed");
                        otherSlots+=planetInteriorKeys(e->region.anchor,config_).size();
                    }
                }
                if(otherSlots>=config_.maxVolumeSlots||otherRegions>=config_.maxRegions){
                    out.phase=CavePlanetRuntimePhase::WaitingCoverage;out.diagnostic="Old cohorts consume native budget";return report();
                }
                auto budget=config_;budget.maxRegions-=otherRegions;budget.maxVolumeSlots-=otherSlots;
                const auto signature=buildSignature(ecs,body,ids);
                if(failed_.contains(signature)){out.phase=CavePlanetRuntimePhase::Refused;out.diagnostic=failed_.at(signature);continue;}
                buildingBody_=body;buildingIds_=ids;
                if(nextRevision_==UINT64_MAX)throw std::overflow_error("Caverns native revision exhausted");
                const auto watermark=planet_.caveSurfaceCutWatermark(surface_->findBody(body));
                if(watermark==UINT64_MAX)throw std::overflow_error("Caverns surface epoch exhausted");
                builder_.begin(ecs,surface_,planet_,ids,budget,generation_,++nextRevision_,watermark+1);
                out.phase=CavePlanetRuntimePhase::Building;return report();
            }
            if(out.phase!=CavePlanetRuntimePhase::Refused)out.phase=CavePlanetRuntimePhase::Ready;
        }catch(const std::exception& e){
            if(!buildingBody_.empty()){
                if(failed_.size()>=64)failed_.clear();
                failed_.insert_or_assign(buildSignature(ecs,buildingBody_,buildingIds_),e.what());
            }
            builder_.cancel();candidate_.reset();buildingBody_.clear();buildingIds_.clear();
            out.phase=CavePlanetRuntimePhase::Refused;out.diagnostic=e.what();
        }
        return report();
    }
    // Actual world teardown only. Caller has stopped movement, native
    // readers and submissions; still requires the same coverage proof.
    bool shutdown(){
        if(closed_)return true;
        if(recoveryRequired()||!hooks_.exclusive())return false;
        builder_.drain();candidate_.reset();buildingBody_.clear();buildingIds_.clear();
        while(!cohorts_.empty())if(!retire(cohorts_.begin()->first,{}))return false;
        CavePlanetResidency::detach(view_);closed_=true;return true;
    }
private:
    std::string buildSignature(const World& ecs,const std::string& body,const std::vector<uint64_t>& ids)const{
        std::string key=body+"/"+std::to_string(generation_)+"/"+std::to_string(planet_.caveSurfaceRevision());
        for(auto id:ids){const auto* e=findPlanetCaveRegion(ecs,id);key+="/"+std::to_string(id)+":"+
            std::to_string(e?e->authority.volume.sourceRevision:0);}
        return key;
    }
    bool retire(std::string body,const std::set<uint64_t>& pins){
        const auto it=cohorts_.find(body);if(it==cohorts_.end())return true;
        auto& c=it->second;
        for(auto id:c.ids)if(pins.contains(id))return false;
        if(!hooks_.exclusive()||!c.coverage||!hooks_.mayReleaseCoverage(*c.coverage))return false;
        auto* owner=residency_.find(body);
        if(!owner){recovery_=true;return false;}
        const auto revision=owner->activeRevision();
        if(!owner->closed()||c.binding->baseline().solidReady()){
            if(!c.binding->closeCut(device_,hooks_.exclusive,[&]{
                    return hooks_.retireNavigationGi(body,revision);
                }))return false;
        }
        // If coverage release fails after close, the restored solid baseline
        // remains owned. Never advertise the retired cut as active.
        c.active.reset();
        if(!hooks_.exclusive()||!hooks_.mayReleaseCoverage(*c.coverage)||
           !c.binding->shutdownBaseline(hooks_.exclusive))return false;
        if(!residency_.eraseClosed(body)){recovery_=true;return false;}
        cohorts_.erase(it);return true;
    }
    bool publishCandidate(const World& ecs,const std::set<uint64_t>& pins){
        auto& candidate=*candidate_;
        const auto body=buildingBody_;
        if(!hooks_.exclusive()||!candidate.current(ecs,generation_))return false;
        auto it=cohorts_.find(body);
        if(it!=cohorts_.end()&&(it->second.ids!=buildingIds_||
                !it->second.binding->matchesBaselinePlan(*candidate.cut))){
            if(!retire(body,pins))return false;
            it=cohorts_.end();
        }
        if(it==cohorts_.end()){
            const auto& anchor=candidate.cut->regions.begin()->second->anchor;
            auto& owner=residency_.open(CavePlanetFrame::make(surface_,anchor));
            Cohort c;c.ids=buildingIds_;c.coverage=candidate.cut;
            try{
                c.binding=std::make_unique<CavePlanetCollisionBinding>(owner,planet_,box_,spatial_,pool_);
                it=cohorts_.emplace(body,std::move(c)).first;
            }catch(...){(void)residency_.eraseClosed(body);throw;}
        }
        auto& c=it->second;
        if(c.binding->baseline().phase()==CavePlanetBaselineCollision::Phase::Empty){
            if(!c.binding->initializeBaseline(*candidate.cut,
                [&](const auto& key){return hooks_.surfaceCell(body,key);},
                [&](const auto& key){return hooks_.unownedLeaf(body,key);},hooks_.exclusive))
                throw std::runtime_error("Caverns baseline preparation refused");
        }
        const auto rev=candidate.nativeRevision;
        bool navPrepared=false;
        try{
            navPrepared=true;
            if(!hooks_.prepareNavigationGi(candidate))throw std::runtime_error("Caverns navigation/GI preflight refused");
            if(!candidate.current(ecs,generation_)||!hooks_.exclusive()||
               !c.binding->stage(device_,ecs,candidate.cut,std::move(candidate.volume),rev,
                   [&](uint64_t,const auto& key){return hooks_.surfaceCell(body,key);},hooks_.exclusive))
                throw std::runtime_error("Caverns native stage refused");
            if(!candidate.current(ecs,generation_)||!hooks_.exclusive()||
               !c.binding->commit(ecs,hooks_.exclusive,[&](uint64_t r){
                   return r==rev&&hooks_.commitNavigationGi(body,r);
               }))throw std::runtime_error("Caverns native commit refused");
        }catch(...){
            if(!recoveryRequired()){
                try{
                    if(!c.binding->cancel(hooks_.exclusive)||
                       (navPrepared&&!hooks_.abortNavigationGi(body,rev)))recovery_=true;
                }catch(...){recovery_=true;}
            }
            throw;
        }
        c.coverage=candidate.cut;c.active=std::move(candidate);candidate_.reset();
        buildingBody_.clear();buildingIds_.clear();return true;
    }
    gfx::Device& device_;game::GameView& view_;game::PlanetRenderer& planet_;
    physics::Scene& box_;world::SpatialWorld& spatial_;CaveColliderLeasePool& pool_;
    std::shared_ptr<const game::Surface> surface_;
    CavePlanetRuntimeHooks hooks_;CavePlanetDemandConfig config_;
    CavePlanetRuntimeBuilder builder_;CavePlanetResidency residency_;
    std::map<std::string,Cohort> cohorts_;
    std::optional<CavePlanetRuntimeCandidate> candidate_;
    std::string buildingBody_;std::vector<uint64_t> buildingIds_;
    std::map<std::string,std::string> failed_;
    CavePlanetDemand lastDemand_;
    uint64_t generation_=1,nextRevision_=0;
    bool recovery_=false,closed_=false;
};
} // namespace aiverse::terraweave::caverns
