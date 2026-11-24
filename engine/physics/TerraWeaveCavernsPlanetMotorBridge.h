#pragma once
// Opt-in native gameplay adapter; not a second motor or lockstep physics owner.
#include "engine/physics/TerraWeaveCavernsPlanetPhysics.h"
#include "engine/world/TerraWeaveCavernsPlanetRuntimeFrame.h"
#include "engine/world/TerraWeaveCavernsPlanetEnvironmentEpoch.h"
#include "engine/mechanics/LocomotionModes.h"
#include <algorithm>
#include <cmath>

namespace aiverse::terraweave::caverns {
struct CavePlanetCharacterDrive {
    physics::CharacterParams params;
    physics::CharacterInput input;
};
// Translate shared steering into the existing Box3D character input.
// Speed is a desired speed cap, not a hard displacement clamp: inertia,
// step-up, gravity and moving platforms remain the motor's responsibility.
inline std::optional<CavePlanetCharacterDrive> cavePlanetCharacterDrive(
        const CavePlanetMotorIntent& intent,Vec3 position,
        const physics::CharacterParams& params,Fixed tickSeconds){
    if(intent.traversal!=CaveTraversal::Ground||!intent.basis.groundedCompatible||
       !intent.speedCmPerTick||tickSeconds<=Fixed{}||!physics::validParams(params))return {};
    auto delta=intent.simulationDestination-position;
    delta=delta-intent.basis.up*delta.dot(intent.basis.up);
    const auto distance=delta.length();
    const auto requested=Fixed::fromRatio(intent.speedCmPerTick,100)/tickSeconds;
    const auto approach=distance/tickSeconds;
    CavePlanetCharacterDrive out;out.params=params;
    out.params.walkSpeed=std::min({params.walkSpeed,float(requested.toDouble()),float(approach.toDouble())});
    out.input.moveX=float(intent.simulationDirection.dot(intent.basis.right).toDouble());
    out.input.moveZ=-float(intent.simulationDirection.dot(intent.basis.back).toDouble());
    // Yaw zero is expressed in the same local frame as the motor basis.
    out.input.yaw=0;
    return out;
}
// Call ONCE in the host's existing native character update, with native
// readers excluded and after canonical capture/repath/steering and managed
// environment lease refresh. The caller still owns Scene::step() and
// publication of the character's authoritative state. Never use this
// floating-point path as a deterministic lockstep position writer.
inline bool stepAdmittedCavePlanetCharacter(CavePlanetRuntimeFrame& runtime,
        physics::Scene& scene,const World& world,Entity entity,
        const CavePlanetFrame& frame,const physics::CharacterParams& params,
        physics::CharacterState& state,physics::BodyHandle proxy,
        uint32_t tick,Fixed tickSeconds){
    if(!runtime.ownsPhysicsScene(scene)||!scene.valid(proxy)||
       scene.motion(proxy)!=physics::Motion::Kinematic)return false;
    const auto* epoch=world.resourceConst<CavePlanetEnvironmentEpoch>();
    const auto* route=world.tryGetConst<nav::CaveRouteFollower>(entity);
    const auto region=nav::caveRouteScope(world,entity);
    const auto* entry=findPlanetCaveRegion(world,region);
    if(!epoch||!route||!entry||!sameCavePlanetAnchor(entry->region.anchor,frame.anchor())||
       !physics::validParams(params)||params.radius*100>route->agent.radiusCm||
       params.height*100>route->agent.heightCm)return false;
    Vec3 start;
    if(!physics::physisVec3(state.position,start))return false;
    try{
        const auto lease=runtime.committedNavigationLease(world,region,epoch->revision);
        if(!lease)return false;
        const auto intent=runtime.motorIntent(world,entity,start,tick,epoch->revision);
        if(!intent)return false;
        const auto drive=cavePlanetCharacterDrive(*intent,start,params,tickSeconds);
        if(!drive)return false;
        auto candidate=state;physics::Pushes pushes;
        const auto q=stepCavePlanetCharacter(scene,world,frame,drive->params,candidate,drive->input,
            region,[&](const World& ecs,uint64_t id){
                return id==region&&runtime.navigationLeaseCurrent(ecs,*lease,epoch->revision);
            },&pushes,proxy);
        Vec3 end;
        if(!q||!physics::physisVec3(candidate.position,end))return false;
        const auto from=CavePlanetFrame::centimetres(frame.fromSimulation(start));
        const auto to=CavePlanetFrame::centimetres(frame.fromSimulation(end));
        if(!lease->provider->residentSegment(from,to,route->agent)||
           !runtime.navigationLeaseCurrent(world,*lease,epoch->revision))return false;
        if(!scene.moveCharacterOriented(proxy,candidate,*q))return false;
        state=candidate;scene.applyPushes(pushes);
        return true;
    }catch(const std::exception&){return false;}
}
// A route cap is shared by both volumetric modes. The existing host
// supplies the flight policy; Caverns never integrates thrust or gravity.
inline std::optional<Vec3> cavePlanetVolumeVelocity(const CavePlanetMotorIntent& intent,
        Vec3 position,Vec3 velocity,Fixed dt,Fixed maxSpeed){
    if((intent.traversal!=CaveTraversal::Flying&&intent.traversal!=CaveTraversal::Swimming)||
       !intent.speedCmPerTick||dt<=Fixed{}||dt>1_fx||maxSpeed<=Fixed{}||maxSpeed>1000_fx)return {};
    for(auto v:{velocity.x,velocity.y,velocity.z})if(v< -1000_fx||v>1000_fx)return {};
    const auto limit=fxMin(maxSpeed,fxMin(Fixed::fromRatio(intent.speedCmPerTick,100)/dt,
        (intent.simulationDestination-position).length()/dt));
    const auto speed=velocity.length();
    if(speed>limit)velocity=fxNormalize(velocity)*limit;
    return velocity;
}
inline std::optional<b3Quat> cavePlanetVolumeOrientation(const CavePlanetFrame& frame,
        const CavePlanetMotorIntent& intent){
    if(intent.basis.groundedCompatible)return cavePlanetCharacterOrientation(intent.basis);
    if(intent.traversal!=CaveTraversal::Flying||intent.basis.magnitude>Fixed::fromRatio(1,100))return {};
    // Zero gravity has no support normal; keep the chart orientation for
    // the capsule, not a fictitious gravity force.
    const b3Matrix3 matrix{physics::physisB3(frame.east()),physics::physisB3(frame.up()),physics::physisB3(frame.south())};
    const auto q=b3NormalizeQuat(b3MakeQuatFromMatrix(&matrix));
    if(!b3IsValidQuat(q))return {};
    return q;
}
// Read the same authoritative water as the navigation provider. Immersion
// is relative to the PHYSIS support axis; refuse incompatible water planes
// instead of using their global Y/radial depths as chart-local values.
inline std::optional<Fixed> cavePlanetSwimDepth(const World& world,Vec3 p,
        const CavePlanetMotionBasis& basis,Fixed seconds){
    if(!basis.groundedCompatible)return {};
    const auto sphere=lib::nereid::sampleSphereWater(world,p,seconds);
    const auto flat=lib::nereid::sampleWater(world,p);
    std::optional<Fixed> depth;
    if(sphere.submerged&&sphere.up.dot(basis.up)>=Fixed::fromRatio(99,100))depth=sphere.depth;
    if(flat.submerged&&basis.up.y>=Fixed::fromRatio(99,100))
        depth=depth?fxMax(*depth,flat.depth):flat.depth;
    return depth;
}
// The policy uses the existing locomotion state (simulation-space velocity)
// supplied by the host. Only the simple Ground/Air/Swim modes enter this
// adapter; other abilities remain with the host's existing owner.
inline std::optional<lib::mechanics::LocomotionResult> prepareCavePlanetSwim(
        const World& world,const CavePlanetMotorIntent& intent,Vec3 position,
        const lib::mechanics::LocomotionModeConfig& config,
        lib::mechanics::LocomotionState& state,uint32_t tick,Fixed dt,Fixed seconds){
    using namespace lib::mechanics;
    if(intent.traversal!=CaveTraversal::Swimming||!intent.basis.groundedCompatible)return {};
    if(state.mode!=LocomotionMode::Ground&&state.mode!=LocomotionMode::Air&&state.mode!=LocomotionMode::Swim)return {};
    const auto depth=cavePlanetSwimDepth(world,position,intent.basis,seconds);
    if(!depth)return {};
    auto next=state;next.motion.velocity=intent.basis.toLocal(state.motion.velocity);
    auto cfg=config;cfg.motion.gravity=intent.basis.magnitude;
    LocomotionInput input;
    const auto local=intent.basis.toLocal(intent.simulationDirection);
    input.wish={fxClamp(local.x,-1_fx,1_fx),fxClamp(local.z,-1_fx,1_fx)};
    input.vertical=fxClamp(local.y,-1_fx,1_fx);input.env.waterDepth=*depth;
    auto result=stepLocomotion(cfg,next,input,tick,dt);
    if(!result||result->mode!=LocomotionMode::Swim)return {};
    result->velocity=intent.basis.toSimulation(result->velocity);
    next.motion.velocity=result->velocity;state=next;return result;
}
// Flight callback is the existing host's read-only velocity proposal. A
// stateful policy must stage its state externally and commit only on success.
using CavePlanetFlightVelocity=std::function<std::optional<Vec3>(const CavePlanetMotorIntent&,Vec3,Fixed)>;
struct CavePlanetVolumeStep {
    CaveTraversal traversal=CaveTraversal::Flying;
    uint64_t events=0;
};
// One native proxy, one sweep, no additional Scene::step. Swim state and
// character state are published only after actual collision and the current
// navigation lease both accept the result.
inline std::optional<CavePlanetVolumeStep> stepAdmittedCavePlanetVolume(
        CavePlanetRuntimeFrame& runtime,physics::Scene& scene,const World& world,Entity entity,
        const CavePlanetFrame& frame,const physics::CharacterParams& params,
        physics::CharacterState& state,physics::BodyHandle proxy,uint32_t tick,Fixed dt,
        const CavePlanetFlightVelocity& flight={},
        const lib::mechanics::LocomotionModeConfig* swimConfig=nullptr,
        lib::mechanics::LocomotionState* swimState=nullptr){
    if(!runtime.ownsPhysicsScene(scene)||!scene.valid(proxy)||
       scene.motion(proxy)!=physics::Motion::Kinematic||!physics::validParams(params)||
       dt<=Fixed{}||std::abs(float(dt.toDouble())-scene.dt())>0.00002f)return {};
    const auto* epoch=world.resourceConst<CavePlanetEnvironmentEpoch>();
    const auto* route=world.tryGetConst<nav::CaveRouteFollower>(entity);
    const auto region=nav::caveRouteScope(world,entity);
    const auto* entry=findPlanetCaveRegion(world,region);
    if(!epoch||!route||!entry||!sameCavePlanetAnchor(entry->region.anchor,frame.anchor())||
       params.radius*100>route->agent.radiusCm||params.height*100>route->agent.heightCm)return {};
    Vec3 start,previousVelocity;
    if(!physics::physisVec3(state.position,start)||!physics::physisVec3(state.velocity,previousVelocity))return {};
    try{
        const auto lease=runtime.committedNavigationLease(world,region,epoch->revision);
        const auto intent=runtime.motorIntent(world,entity,start,tick,epoch->revision);
        if(!lease||!intent)return {};
        const auto q=cavePlanetVolumeOrientation(frame,*intent);
        if(!q)return {};
        std::optional<Vec3> desired;
        std::optional<lib::mechanics::LocomotionState> nextSwim;
        CavePlanetVolumeStep result{intent->traversal,0};
        if(intent->traversal==CaveTraversal::Flying){
            if(!flight)return {};
            desired=flight(*intent,previousVelocity,dt);
        }else if(intent->traversal==CaveTraversal::Swimming){
            if(!swimConfig||!swimState)return {};
            nextSwim=*swimState;nextSwim->motion.velocity=previousVelocity;
            const auto swim=prepareCavePlanetSwim(world,*intent,start,*swimConfig,*nextSwim,
                tick,dt,epoch->sphereSeconds);
            if(!swim)return {};
            desired=swim->velocity;result.events=swim->events;
        }else return {};
        if(!desired)return {};
        const auto velocity=cavePlanetVolumeVelocity(*intent,start,*desired,dt,
            Fixed::fromRatio(int64_t(params.maxSpeed*1000.f),1000));
        if(!velocity)return {};
        auto candidate=state;
        if(!physics::sweepCharacterVelocityOriented(scene.world(),params,candidate,
                physics::physisB3(*velocity),scene.dt(),*q,proxy))return {};
        Vec3 finish,actualVelocity;
        if(!physics::physisVec3(candidate.position,finish)||!physics::physisVec3(candidate.velocity,actualVelocity))return {};
        const auto from=CavePlanetFrame::centimetres(frame.fromSimulation(start));
        const auto to=CavePlanetFrame::centimetres(frame.fromSimulation(finish));
        if(!lease->provider->canTraverse(from,to,route->agent)||
           !runtime.navigationLeaseCurrent(world,*lease,epoch->revision))return {};
        if(nextSwim&&!cavePlanetSwimDepth(world,finish,intent->basis,epoch->sphereSeconds))return {};
        if(!scene.moveCharacterOriented(proxy,candidate,*q))return {};
        state=candidate;
        if(nextSwim){nextSwim->motion.velocity=actualVelocity;*swimState=*nextSwim;}
        return result;
    }catch(const std::exception&){return {};}
}
} // namespace aiverse::terraweave::caverns
