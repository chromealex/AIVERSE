#pragma once
// Opt-in native gameplay adapter; not a second motor or lockstep physics owner.
#include "engine/physics/TerraWeaveCavernsPlanetPhysics.h"
#include "engine/world/TerraWeaveCavernsPlanetRuntimeFrame.h"
#include "engine/world/TerraWeaveCavernsPlanetEnvironmentEpoch.h"
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
} // namespace aiverse::terraweave::caverns
