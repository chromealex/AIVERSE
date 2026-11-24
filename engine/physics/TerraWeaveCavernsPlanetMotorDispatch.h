#pragma once
// Stage58a: one dispatch inside the existing native motor system.
// Owns only ephemeral admission receipts, never actors or physics state.
#include "engine/physics/TerraWeaveCavernsPlanetMotorBridge.h"
#include <map>
#include <set>

namespace aiverse::terraweave::caverns {
enum class CavePlanetDispatchResult { Refused,Held,Repath,Exterior,Crossing,Interior,Recovery };
class CavePlanetMotorTickGuard {
public:
    // A physics step is the consumption boundary. A different simulation
    // tick without that step must not schedule the same proxy twice.
    bool claim(uint64_t physicsTick,uint32_t simulationTick,Entity entity,physics::BodyHandle proxy){
        if(proxy==physics::kNoBody)return false;
        if(tick_&&(physicsTick<*tick_||(physicsTick>*tick_&&simulationTick<=simulationTick_)))return false;
        if(!tick_||physicsTick!=*tick_){
            entities_.clear();proxies_.clear();tick_=physicsTick;simulationTick_=simulationTick;
        }
        if(simulationTick_!=simulationTick||entities_.contains(entity.packed())||proxies_.contains(proxy))return false;
        entities_.insert(entity.packed());proxies_.insert(proxy);return true;
    }
    // Only at the real host's synchronized restore boundary.
    void reset(){tick_.reset();entities_.clear();proxies_.clear();}
private:
    std::optional<uint64_t> tick_;
    uint32_t simulationTick_=0;
    std::set<uint64_t> entities_;
    std::set<physics::BodyHandle> proxies_;
};
struct CavePlanetMotorDispatchInput {
    CavePlanetTraversalCapabilities capabilities;
    bool automaticModeSelection=false;
    // Called after the route profile changes to synchronize the existing
    // controller state, not to run movement. Failure requires recovery.
    std::function<bool(World&,Entity,CaveTraversal,CaveTraversal)> modeChanged;
    CavePlanetCrossingMotion crossing;
    CavePlanetCrossingCoverage crossingCoverage;
    std::optional<CavePlanetCrossingGoal> crossingGoal;
    CavePlanetFlightVelocity flight;
    const lib::mechanics::LocomotionModeConfig* swimConfig=nullptr;
    lib::mechanics::LocomotionState* swimState=nullptr;
    // Existing exterior motor, not a new motor. It stages/commits its own
    // native proxy once, and returns true only when its state is accepted.
    std::function<bool(physics::CharacterState&)> exterior;
    // Existing single ECS position writer and canonical readback.
    std::function<bool(World&,Entity,const physics::CharacterState&)> publishPosition;
    CaveSimulationPosition position;
};
// Route policy is evaluated from current authoritative coordinates. No
// extra repath system; changes invalidate steering for the next tick.
inline std::optional<CavePlanetCrossingGoal> currentCavePlanetRouteGoal(
        const World& world,Entity entity,const CavePlanetFrame& frame){
    const auto* intent=world.tryGetConst<nav::CaveRouteIntent>(entity);
    const auto* route=world.tryGetConst<nav::CaveRouteFollower>(entity);
    if(!intent||!intent->active||!route)return {};
    try{
        return CavePlanetCrossingGoal{frame.toSimulation(CavePlanetFrame::metres(intent->goal)),
            route->agent,intent->algorithm,route->speedCmPerTick,intent->maxVisits};
    }catch(const std::exception&){return {};}
}
inline bool cavePlanetDispatchPositionMatches(const CaveSimulationPosition& read,const World& world,
        Entity entity,const physics::CharacterState& state){
    if(!read)return false;
    const auto actual=read(world,entity);Vec3 native;
    return actual&&physics::physisVec3(state.position,native)&&
        (*actual-native).length()<=Fixed::fromRatio(1,100);
}
inline CavePlanetDispatchResult dispatchCavePlanetMotor(
        CavePlanetRuntimeFrame& runtime,physics::Scene& scene,World& world,Entity entity,
        const std::shared_ptr<const game::Surface>& surface,const physics::CharacterParams& params,
        physics::CharacterState& state,physics::BodyHandle proxy,uint32_t tick,Fixed dt,
        const CavePlanetMotorDispatchInput& input){
    using R=CavePlanetDispatchResult;
    if(!surface||!world.alive(entity)||!runtime.ownsPhysicsScene(scene)||runtime.recoveryRequired()||
       !scene.valid(proxy)||scene.motion(proxy)!=physics::Motion::Kinematic||!physics::validParams(params)||
       !input.publishPosition||!input.position||dt<=Fixed{}||
       std::abs(float(dt.toDouble())-scene.dt())>.00002f)return R::Refused;
    bool published=false;
    const auto recover=[&](){runtime.quarantineNativeMotion();return R::Recovery;};
    try{
        if(!cavePlanetDispatchPositionMatches(input.position,world,entity,state))return R::Refused;
        Vec3 actualProxy,nativePosition;
        if(!physics::physisVec3(scene.position(proxy),actualProxy)||!physics::physisVec3(state.position,nativePosition)||
           (actualProxy-nativePosition).length()>Fixed::fromRatio(1,100))return R::Refused;
        const auto* travel=world.tryGetConst<CavePlanetTravel>(entity);
        if(!travel||!validCavePlanetTravel(*travel))return R::Refused;
        const auto motor=cavePlanetTravelMotor(world,entity);
        R result=R::Refused;
        if(motor==CavePlanetTravelMotor::Exterior){
            if(nav::caveRouteScope(world,entity)||!input.exterior)return R::Refused;
            // The exterior owner contract is irreversible once invoked.
            published=true;
            if(!input.exterior(state))return recover();
            result=R::Exterior;
        }else{
            const auto region=travel->pendingRegion?travel->pendingRegion:travel->occupiedRegion;
            const auto* entry=findPlanetCaveRegion(world,region);
            const auto* epoch=world.resourceConst<CavePlanetEnvironmentEpoch>();
            if(!entry||!epoch)return R::Refused;
            const auto lease=runtime.committedNavigationLease(world,region,epoch->revision);
            if(!lease)return R::Refused;
            const auto frame=CavePlanetFrame::make(surface,entry->region.anchor);
            if(motor==CavePlanetTravelMotor::Crossing){
                const auto step=stepAdmittedCavePlanetCrossing(runtime,scene,world,entity,frame,params,state,proxy,
                    input.crossing,input.crossingCoverage,tick,dt,input.crossingGoal);
                if(step==CavePlanetCrossingResult::Recovery)return recover();
                if(step==CavePlanetCrossingResult::Refused)return R::Refused;
                result=R::Crossing;published=true;
            }else{
                if(nav::caveRouteScope(world,entity)!=region)return R::Held;
                auto goal=currentCavePlanetRouteGoal(world,entity,frame);
                if(!goal)return R::Held;
                const auto current=goal->agent.traversal;
                if(params.radius*100>goal->agent.radiusCm||params.height*100>goal->agent.heightCm)return R::Refused;
                if(input.automaticModeSelection){
                    if(!input.swimConfig)return R::Refused;
                    Vec3 point;if(!physics::physisVec3(state.position,point))return R::Refused;
                    const auto selected=chooseCavePlanetTraversalGoal(*lease->provider,world,frame,point,*goal,
                        input.capabilities,current,*input.swimConfig,epoch->sphereSeconds);
                    if(!selected)return R::Held;
                    if(selected->agent.traversal!=current){
                        if(!input.modeChanged)return R::Held;
                        published=true; // route replacement may partially mutate on allocation failure
                        if(!changeCavePlanetTraversal(runtime,world,entity,surface,input.position,*selected))return recover();
                        if(!input.modeChanged(world,entity,current,selected->agent.traversal))return recover();
                        return R::Repath;
                    }
                }
                const auto* route=world.tryGetConst<nav::CaveRouteFollower>(entity);
                if(!route||route->arrived||route->needsRepath||!nav::caveMotorRequest(world,entity,*lease->provider))return R::Held;
                if(current==CaveTraversal::Ground){
                    if(!input.capabilities.ground)return R::Refused;
                    if(!stepAdmittedCavePlanetCharacter(runtime,scene,world,entity,frame,params,state,proxy,tick,dt))return R::Refused;
                }else if(current==CaveTraversal::Swimming||current==CaveTraversal::Flying){
                    if(current==CaveTraversal::Swimming&&!input.capabilities.swim)return R::Refused;
                    if(current==CaveTraversal::Flying){
                        if(!input.capabilities.flight)return R::Refused;
                        Vec3 p;if(!physics::physisVec3(state.position,p))return R::Refused;
                        if(!input.capabilities.flightUnderwater&&
                           (lib::nereid::sampleWater(world,p).submerged||
                            lib::nereid::sampleSphereWater(world,p,epoch->sphereSeconds).submerged))return R::Refused;
                    }
                    if(!stepAdmittedCavePlanetVolume(runtime,scene,world,entity,frame,params,state,proxy,tick,dt,
                        input.flight,input.swimConfig,input.swimState))return R::Refused;
                }else return R::Refused; // custom/climb remain the host's own policies
                result=R::Interior;published=true;
            }
        }
        if(!input.publishPosition(world,entity,state)||
           !cavePlanetDispatchPositionMatches(input.position,world,entity,state))return recover();
        return result;
    }catch(...){return published?recover():R::Refused;}
}
} // namespace aiverse::terraweave::caverns
