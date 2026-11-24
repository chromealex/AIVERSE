#pragma once
// Genre-independent native host frame; all mutable gameplay density stays
// in ECS. This owner coordinates derived resources on the host's existing
// graphics/physics thread, never inside a draw pass or simulation worker.
#include "engine/world/TerraWeaveCavernsPlanetRuntimeBuild.h"
#include "engine/world/TerraWeaveCavernsPlanetCollisionBinding.h"
#include "engine/world/TerraWeaveCavernsPlanetResidency.h"
#include "engine/world/TerraWeaveCavernsPlanetMotion.h"

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
    // Optional managed provider publication. Existing GI/solver hooks are
    // still mandatory; prepared leases are available on the candidate.
    CavePlanetNavigationInputSource navigationInputs;
    // Optional fallible retirement staging before the native owner removes
    // any physics/render source. Legacy hosts may retain their own preflight.
    std::function<bool(const std::string&,uint64_t)> prepareRetireNavigationGi;
    // Optional baseline GI producer; refusal is transient and must occur
    // before irreversible native staging (candidate retained for retry).
    std::function<bool(const CavePlanetBodyCutPlan&)> ensureBaselineGi;
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
        if(view_.hasPlanetCaveDraw())throw std::logic_error("Caverns native draw owner already installed");
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
        for(auto& [body,c]:cohorts_){(void)body;if(c.active)c.active->navigationLeases.clear();}
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
    // Host-thread capture only. No live runtime pointer is captured by the
    // provider; route workers get immutable data. Supply a fresh environment
    // revision whenever PHYSIS/NEREID inputs or mapping change.
    std::optional<CavePlanetNavigationLease> navigationLease(const World& ecs,
            uint64_t id,std::shared_ptr<const World> frozen,uint64_t environmentRevision,
            CavePhysisCellMapper mapper,CaveEnvironmentCostSettings settings={},
            CaveNavigationCallbacks policies={})const{
        if(!regionReady(ecs,id))return {};
        const auto* entry=findPlanetCaveRegion(ecs,id);
        const auto& candidate=*cohorts_.at(entry->region.anchor.bodyId).active;
        if(hooks_.navigationInputs)return committedNavigationLease(ecs,id,environmentRevision);
        return candidate.navigationLease(surface_,id,std::move(frozen),environmentRevision,
            std::move(mapper),settings,std::move(policies));
    }
    // Call under the host's normal reader exclusion at dispatch/acceptance.
    bool navigationLeaseCurrent(const World& ecs,const CavePlanetNavigationLease& lease,
            uint64_t environmentRevision)const{
        if(!regionReady(ecs,lease.regionId))return false;
        const auto* entry=findPlanetCaveRegion(ecs,lease.regionId);
        const auto& candidate=*cohorts_.at(entry->region.anchor.bodyId).active;
        if(!lease.matches(lease.regionId,generation_,candidate.nativeRevision,
            entry->authority.volume.sourceRevision,environmentRevision))return false;
        if(!hooks_.navigationInputs)return true;
        const auto stored=candidate.navigationLeases.find(lease.regionId);
        if(stored==candidate.navigationLeases.end()||stored->second.provider!=lease.provider)return false;
        try{
            const auto input=hooks_.navigationInputs(ecs,lease.regionId);
            return input&&input->environmentRevision==environmentRevision&&bool(input->physisCell);
        }catch(const std::exception&){return false;}
    }
    std::optional<CavePlanetNavigationLease> committedNavigationLease(
            const World& ecs,uint64_t id,uint64_t environmentRevision)const{
        if(!hooks_.navigationInputs||!regionReady(ecs,id))return {};
        const auto* entry=findPlanetCaveRegion(ecs,id);
        const auto& leases=cohorts_.at(entry->region.anchor.bodyId).active->navigationLeases;
        const auto it=leases.find(id);
        if(it==leases.end()||!navigationLeaseCurrent(ecs,it->second,environmentRevision))return {};
        return it->second;
    }
    // Environment-only changes replace all admitted providers atomically
    // under reader exclusion, not the already committed SDF/physics/GI.
    // Old workers retain immutable providers but cannot pass the identity gate.
    bool ownsPhysicsScene(const physics::Scene& scene)const noexcept{return &scene==&box_;}
    bool refreshNavigationEnvironment(const World& ecs){
        if(!hooks_.navigationInputs||closed_||recoveryRequired()||!hooks_.exclusive())return false;
        try{
            const auto frozen=std::make_shared<const World>(ecs);
            std::map<std::string,CavePlanetNavigationLeaseSet> next;
            for(const auto& [body,c]:cohorts_){
                if(!c.active)continue;
                for(auto id:c.ids)if(!regionReady(ecs,id))return false;
                auto leases=c.active->prepareNavigationLeases(surface_,frozen,hooks_.navigationInputs);
                if(!leases)return false;
                next.emplace(body,std::move(*leases));
            }
            if(!hooks_.exclusive())return false;
            for(const auto& [body,leases]:next){
                (void)body;
                for(const auto& [id,lease]:leases){
                    const auto input=hooks_.navigationInputs(ecs,id);
                    if(!regionReady(ecs,id)||!input||!input->physisCell||
                       input->environmentRevision!=lease.provider->environmentRevision())return false;
                }
            }
            if(!hooks_.exclusive())return false;
            for(auto& [body,leases]:next)cohorts_.at(body).active->navigationLeases.swap(leases);
            return true;
        }catch(const std::exception&){return false;}
    }
    // Consume canonical ECS steering only against the actually committed
    // native provider. The existing motor is still the sole position writer.
    std::optional<CavePlanetMotorIntent> motorIntent(const World& ecs,Entity entity,
            Vec3 simulationPosition,uint32_t tick,uint64_t environmentRevision)const{
        const auto id=nav::caveRouteScope(ecs,entity);
        const auto lease=committedNavigationLease(ecs,id,environmentRevision);
        if(!lease)return {};
        const auto* entry=findPlanetCaveRegion(ecs,id);
        try{
            const auto frame=CavePlanetFrame::make(surface_,entry->region.anchor);
            return cavePlanetMotorIntent(ecs,entity,id,frame,*lease->provider,simulationPosition,tick,
                [this](const World& world,uint64_t region){return regionReady(world,region);});
        }catch(const std::exception&){return {};}

    }
    // The host MUST use this readiness in its actual movement/admission gate;
    // render visibility is not permission to move through a cave.
    // Existing committed cohorts may remain presentable while a GPU
    // fence delays a new publication. Restore or changed SDF invalidates
    // this proof even if the old native geometry still exists.
    bool committedCohortsCurrent(const World& ecs)const{
        if(closed_||recoveryRequired())return false;
        for(const auto& [body,cohort]:cohorts_){
            (void)body;
            if(!cohort.active)continue;
            for(auto id:cohort.ids)if(!regionReady(ecs,id))return false;
        }
        return true;
    }
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
        if(!owner->closed()){
            const bool preflight=bool(hooks_.prepareRetireNavigationGi);
            if(preflight&&!hooks_.prepareRetireNavigationGi(body,revision))return false;
            if(!c.binding->closeCut(device_,hooks_.exclusive,[&]{
                    return hooks_.retireNavigationGi(body,revision);
                })){
                if(preflight&&!recoveryRequired()&&!hooks_.abortNavigationGi(body,revision))recovery_=true;
                return false;
            }
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
        if(hooks_.ensureBaselineGi&&!hooks_.ensureBaselineGi(*candidate.cut))return false;
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
            if(hooks_.navigationInputs){
                auto leases=candidate.prepareNavigationLeases(surface_,std::make_shared<const World>(ecs),
                    hooks_.navigationInputs);
                if(!leases)throw std::runtime_error("Caverns navigation cohort preparation refused");
                candidate.navigationLeases=std::move(*leases);
            }
            navPrepared=true;
            if(!hooks_.prepareNavigationGi(candidate))throw std::runtime_error("Caverns navigation/GI preflight refused");
            if(!candidate.current(ecs,generation_)||!hooks_.exclusive()||
               !c.binding->stage(device_,ecs,candidate.cut,std::move(candidate.volume),rev,
                   [&](uint64_t,const auto& key){return hooks_.surfaceCell(body,key);},hooks_.exclusive))
                throw std::runtime_error("Caverns native stage refused");
            if(!candidate.current(ecs,generation_)||!hooks_.exclusive()||
               !c.binding->commit(ecs,hooks_.exclusive,[&](uint64_t r){
                   if(r!=rev)return false;
                   if(hooks_.navigationInputs){
                       for(const auto& [id,lease]:candidate.navigationLeases){
                           const auto input=hooks_.navigationInputs(ecs,id);
                           if(!input||!input->physisCell||
                              input->environmentRevision!=lease.provider->environmentRevision())return false;
                       }
                   }
                   return hooks_.commitNavigationGi(body,r);
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
