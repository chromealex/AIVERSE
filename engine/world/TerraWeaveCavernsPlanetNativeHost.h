#pragma once
// Stage56b: optional native host composition over the REAL shared owners.
// No GameCore/ActorHost dependency, no second physics scene or GI runtime.
// All native methods execute at the host's existing exclusive frame barrier.
#include "engine/world/TerraWeaveCavernsPlanetGiViewFrame.h"
#include "engine/world/TerraWeaveCavernsPlanetEnvironmentEpoch.h"
#include "engine/world/TerraWeaveCavernsPlanetTravel.h"
#include "engine/render/GiMainCpuBridge.h"
#include "engine/render/GiMainGpuBridge.h"
#include "engine/render/Renderer3D.h"
#include <atomic>
#include <memory>
#include <set>
#include <span>
#include <type_traits>
#include <utility>

namespace aiverse::terraweave::caverns {
// A restore callback only signals a new generation; it must not call GPU
// APIs or retain a native pointer in Simulation's persistent state.
// A weak reference makes the installed hook safe after explicit shutdown.
class CavePlanetRestoreNotice {
public:
    void attach(Simulation& sim){
        if(attached_)throw std::logic_error("Caverns restore notice already attached");
        const std::weak_ptr<std::atomic_bool> weak=pending_;
        sim.addStateRestoredHook([weak](Simulation&){
            if(auto flag=weak.lock())flag->store(true,std::memory_order_release);
        });
        attached_=true;
    }
    bool pending()const noexcept{return pending_->load(std::memory_order_acquire);}
    bool consume()noexcept{return pending_->exchange(false,std::memory_order_acq_rel);}
    void mark()noexcept{pending_->store(true,std::memory_order_release);}
    CavePlanetRestoreNotice()=default;
    CavePlanetRestoreNotice(const CavePlanetRestoreNotice&)=delete;
    CavePlanetRestoreNotice& operator=(const CavePlanetRestoreNotice&)=delete;
private:
    std::shared_ptr<std::atomic_bool> pending_=std::make_shared<std::atomic_bool>(false);
    bool attached_=false;
};

// Canonical ECS setup remains separate from the device-dependent host.
// Install during module setup after the existing PHYSIS/NEREID/motor systems.
// An absent/empty cave map does not create an unused navigation pipeline.
inline std::shared_ptr<CavePlanetCanonicalNavigationSources> installPlanetCavesInNativeSimulation(
        Simulation& sim,const Json& authored,std::shared_ptr<const game::Surface> surface,
        std::shared_ptr<const CavePathRouter> router,CaveSimulationPosition position,
        const std::string& motor,uint64_t policyRevision,CavePlanetDemandConfig lattice={},
        CaveEnvironmentCostSettings costs={}){
    if(authored.isNull())return {};
    // Decode before installing any persistent schema. Register the same
    // authority and canonical systems used by all other native projects.
    if(decodePlanetCaveMap(authored,surface).regions.empty())return {};
    if(!router||!position||!policyRevision||motor.empty()||
       sim.world().resourceConst<CavePlanetEnvironmentEpoch>())
        throw std::invalid_argument("Caverns native simulation setup");
    size_t motorCount=0;
    for(size_t i=0;i<sim.systemCount();++i){
        const auto& name=sim.systemName(i);
        if(name==motor)++motorCount;
        if(name=="caverns.navigation.capture"||name=="caverns.navigation.repath"||
           name=="caverns.navigation.intent"||name=="caverns.navigation.follow")
            throw std::logic_error("Caverns competing navigation systems");
    }
    if(motorCount!=1)throw std::invalid_argument("Caverns requires one existing motor");
    registerPlanetCaveMap(sim,authored,surface);
    return installCanonicalCavePlanetNavigation(sim,std::move(surface),std::move(router),
        std::move(position),motor,policyRevision,lattice,costs);
}

template<class Target>
class CavePlanetNativeHost {
    static_assert(std::is_same_v<Target,render::GiMainCpuBridge>||
                  std::is_same_v<Target,render::GiMainGpuBridge>,
                  "Use an existing native GI owner");
public:
    CavePlanetNativeHost(Simulation& sim,gfx::Device& device,game::GameView& view,
            physics::Scene& box,world::SpatialWorld& spatial,CaveColliderLeasePool& colliders,
            std::shared_ptr<const game::Surface> surface,render::GiGeometryScene& scene,
            Target& target,CavePlanetGiBindings giBindings,CavePlanetRuntimeHooks hooks,
            uint64_t policyRevision,CavePlanetDemandConfig demand={},
            CaveEnvironmentCostSettings costs={},size_t workers=2)
        :target_(target),scene_(scene),surface_(surface),exclusive_(hooks.exclusive),
         giReady_(giBindings.ready),publication_(scene,target,std::move(giBindings)){
        const auto* epoch=sim.world().resourceConst<CavePlanetEnvironmentEpoch>();
        if(!exclusive_||!giReady_||!surface||!epoch||epoch->policyRevision!=policyRevision||
           !sim.world().resourceConst<CavePlanetAuthoritySet>()||
           hooks.navigationInputs||view.hasPlanetCaveDraw())
            throw std::invalid_argument("Caverns native host requires installed canonical ECS and unique owner");
        hooks.navigationInputs=canonicalCavePlanetNavigationInputs(surface,policyRevision,costs);
        if(publication_.providesBaseline()){
            if(hooks.ensureBaselineGi)throw std::invalid_argument("Caverns competing GI baseline source");
            hooks.ensureBaselineGi=[this,&view](const CavePlanetBodyCutPlan& plan){
                if(!exclusive_())return false;
                const auto revision=scene_.revision();
                if(!publication_.ensureBaseline(view.planets(),plan))return false;
                // A new baseline may submit GPU work. Resume native staging
                // only on a subsequent owner frame after the readiness gate.
                return scene_.revision()==revision&&exclusive_()&&giReady_();
            };
        }
        if(!publication_.bind(hooks)||!hooks.valid())
            throw std::invalid_argument("Caverns native host binding");
        notice_.attach(sim);
        runtime_=std::make_unique<CavePlanetRuntimeFrame>(device,view,box,spatial,colliders,
            std::move(surface),std::move(hooks),demand,workers);
    }
    CavePlanetNativeHost(const CavePlanetNativeHost&)=delete;
    CavePlanetNativeHost& operator=(const CavePlanetNativeHost&)=delete;

    // Never step Simulation or Box3D here: the existing host owns both.
    // Pins include actors and requested destination charts, not just camera.
    CavePlanetRuntimeProgress beforeFrame(const World& world,render::Renderer3D& renderer,
            std::span<const CavePlanetObserver> observers,const std::set<uint64_t>& pins,
            size_t jobs=2){
        const bool wasCoherent=frameCoherent_;
        frameCoherent_=false;
        CavePlanetRuntimeProgress result;
        if(closed_||!exclusive_()){
            result.phase=CavePlanetRuntimePhase::Refused;
            result.diagnostic="Caverns native host requires exclusive readers";
            return result;
        }
        const auto required=cavePlanetTravelPins(world,pins);
        renderer.clearDynamicGiOverlay();
        const bool restored=notice_.consume();
        if(restored)runtime_->invalidateAfterRestore();
        if(runtime_->recoveryRequired()){
            result.phase=CavePlanetRuntimePhase::Recovery;
            result.diagnostic="Caverns native cohort quarantined";
            return result;
        }
        // Fence pressure is transient, not a bad authored recipe. Refuse
        // before entering the runtime's irreversible publication barrier.
        if(!target_.ready()||!giReady_()||(!scene_.current()&&!publication_.providesBaseline())||scene_.hasPendingChanges()){
            result.phase=CavePlanetRuntimePhase::WaitingCoverage;
            result.pinsReady=runtime_->requiredRegionsReady(world,required);
            // A busy fence alone does not invalidate an unchanged published
            // cohort. The actual bridge still decides if its atlas can bind.
            frameCoherent_=wasCoherent&&!restored&&!notice_.pending()&&
                result.pinsReady&&runtime_->committedCohortsCurrent(world)&&
                publication_.current()&&target_.ready();
            result.diagnostic="Caverns waiting for shared GI owner/fence";
            return result;
        }
        result=runtime_->pump(world,observers,required,jobs);
        frameCoherent_=result.allDemandReady&&!runtime_->recoveryRequired()&&!notice_.pending()&&publication_.current();
        return result;
    }

    // Admission before the existing authoritative tick. Never use camera
    // visibility, GPU readiness or a local lease as a lockstep checksum.
    bool admitNativeTick(const World& world,const std::set<uint64_t>& pins)const{
        return !closed_&&!notice_.pending()&&!runtime_->recoveryRequired()&&
            runtime_->requiredRegionsReady(world,cavePlanetTravelPins(world,pins));
    }
    // Call after canonical capture and immediately before the existing
    // native motor consumes steering. Refresh only when the admitted
    // environment epoch changed; do not rebuild native collision or GI.
    bool beforeMotor(const World& world,const std::set<uint64_t>& required){
        if(!exclusive_()||!admitNativeTick(world,required))return false;
        const auto* epoch=world.resourceConst<CavePlanetEnvironmentEpoch>();
        if(!epoch||!epoch->revision)return false;
        const auto all=cavePlanetTravelPins(world,required);
        const auto current=[&](){
            for(auto id:all)
                if(!runtime_->committedNavigationLease(world,id,epoch->revision))return false;
            return true;
        };
        return current()||(runtime_->refreshNavigationEnvironment(world)&&current());
    }
    // Before the existing crossing motor, under the same owner exclusion.
    bool admitCrossing(const World& world,Entity entity)const{
        return !closed_&&exclusive_()&&!notice_.pending()&&!runtime_->recoveryRequired()&&
            cavePlanetCrossingReady(*runtime_,world,entity);
    }
    std::optional<CavePlanetCrossingGuidance> crossingGuidance(const World& world,Entity entity,Fixed extent)const{
        if(!admitCrossing(world,entity))return {};
        return cavePlanetCrossingGuidance(*runtime_,world,entity,surface_,extent);
    }
    CavePlanetCrossingResult acceptCrossingMovement(World& world,Entity entity,Vec3 previous,
            const CaveSimulationPosition& position,const CavePlanetCrossingProof& swept,
            const CavePlanetSideProof& side,Fixed margin,uint32_t tick,
            const std::optional<CavePlanetCrossingGoal>& goal={}){
        if(!admitCrossing(world,entity))return CavePlanetCrossingResult::Refused;
        return advanceCavePlanetCrossing(*runtime_,world,entity,surface_,previous,position,swept,side,margin,tick,goal);
    }
    // Expose the existing native admission adapter only, not a second motor.
    CavePlanetRuntimeFrame& runtime(){return *runtime_;}
    const CavePlanetRuntimeFrame& runtime()const{return *runtime_;}
    bool recoveryRequired()const noexcept{return runtime_->recoveryRequired();}

    // The caller holds the existing frame barrier and coordinates lights,
    // environment, probe focus and other presentation transforms in the same
    // new GI space. Collision/navigation remain in simulation-anchor space.
    // A refused target leaves the old geometry and projector intact.
    bool reprojectGi(render::Renderer3D& renderer,CavePlanetGiProject next,
            const CavePlanetGiOtherProject& other){
        if(closed_||!exclusive_()||notice_.pending()||recoveryRequired())return false;
        renderer.clearDynamicGiOverlay();frameCoherent_=false;
        if(!target_.ready()||!giReady_())return false;
        return publication_.reproject(std::move(next),other);
    }

    // Called by the existing renderer after its GI update, before the pass.
    // The real CPU/GPU bridge also checks its own atlas presentation safety.
    bool bindIndirect(render::Renderer3D& renderer)const{
        renderer.clearDynamicGiOverlay();
        if(closed_||!frameCoherent_||notice_.pending()||recoveryRequired()||
           publication_.staged()||!publication_.current())return false;
        return renderer.setDynamicGiOverlay(target_);
    }

    // The host stops readers and submissions first. Failed shutdown retains
    // the owner, its leases and the recovery state; never drop live handles.
    bool shutdown(render::Renderer3D& renderer){
        if(closed_)return true;
        if(!exclusive_())return false;
        renderer.clearDynamicGiOverlay();frameCoherent_=false;
        if(!runtime_->shutdown())return false;
        if(publication_.activeBodies()||publication_.staged())return false;
        closed_=true;return true;
    }
private:
    Target& target_;
    render::GiGeometryScene& scene_;
    std::shared_ptr<const game::Surface> surface_;
    CavePlanetNativeOwner::Exclusive exclusive_;
    std::function<bool()> giReady_;
    CavePlanetRestoreNotice notice_;
    CavePlanetGiPublication<Target> publication_; // outlives runtime callbacks
    std::unique_ptr<CavePlanetRuntimeFrame> runtime_;
    bool frameCoherent_=false,closed_=false;
};
using CavePlanetCpuNativeHost=CavePlanetNativeHost<render::GiMainCpuBridge>;
using CavePlanetGpuNativeHost=CavePlanetNativeHost<render::GiMainGpuBridge>;
} // namespace aiverse::terraweave::caverns
