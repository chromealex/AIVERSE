#pragma once
// Shared deterministic motion intent. PHYSIS is the only gravity authority;
// existing locomotion policy/motor remains the sole velocity/position owner.
#include "engine/world/TerraWeaveCavernsPlanetQueries.h"
#include "engine/world/TerraWeaveCavernsPlanetRegionSet.h"
#include "engine/mechanics/Locomotion.h"
#include "engine/nav/CaveEcsSteering.h"

namespace aiverse::terraweave::caverns {
struct CavePlanetMotionBasis {
    Vec3 right{},up{},back{},acceleration{};
    Fixed magnitude{};
    bool groundedCompatible=false;
    Vec3 toLocal(Vec3 v)const{return {v.dot(right),v.dot(up),v.dot(back)};}
    Vec3 toSimulation(Vec3 v)const{return right*v.x+up*v.y+back*v.z;}
};
// Sample at the real position, without centimetre snapping. Chart east
// provides a stable heading reference; chart south handles its pole.
inline CavePlanetMotionBasis cavePlanetMotionBasis(const CavePlanetFrame& frame,
        const World& ecs,Vec3 simulationPosition){
    (void)frame.fromSimulation(simulationPosition);
    CavePlanetMotionBasis out;
    out.acceleration=physis::gravityAt(ecs,simulationPosition);
    out.magnitude=out.acceleration.length();
    if(out.magnitude<=Fixed::fromRatio(1,100))return out;
    out.up=fxNormalize(out.acceleration*Fixed::fromInt(-1));
    auto tangent=frame.east()-out.up*frame.east().dot(out.up);
    if(tangent.lengthSq()<Fixed::fromRatio(1,64))
        tangent=frame.south()-out.up*frame.south().dot(out.up);
    out.right=fxNormalize(tangent);
    out.back=fxNormalize(fxCross(out.right,out.up));
    out.right=fxNormalize(fxCross(out.up,out.back));
    const auto tol=Fixed::fromRatio(1,128);
    for(const auto& v:{out.right,out.up,out.back})
        if(fxAbs(v.lengthSq()-1_fx)>tol)return out;
    if(fxAbs(out.right.dot(out.up))>tol||fxAbs(out.back.dot(out.up))>tol||
       fxAbs(out.right.dot(out.back))>tol)return out;
    out.groundedCompatible=true;return out;
}
// Existing Fixed locomotion policy in an arbitrary support frame. State
// velocity remains in simulation coordinates between ticks; contacts and
// position integration remain the existing motor's responsibility.
inline std::optional<lib::mechanics::MotionResult> stepCavePlanetMotion(
        const CavePlanetMotionBasis& basis,const lib::mechanics::MotionConfig& config,
        lib::mechanics::MotionState& state,lib::mechanics::MotionInput input,
        uint32_t tick,Fixed dt){
    if(!basis.groundedCompatible)return {};
    auto local=state;local.velocity=basis.toLocal(state.velocity);
    auto cfg=config;cfg.gravity=basis.magnitude;
    auto result=lib::mechanics::stepMotion(cfg,local,input,tick,dt);
    if(!result)return {};
    local.velocity=basis.toSimulation(local.velocity);result->velocity=local.velocity;
    state=local;return result;
}
struct CavePlanetMotorIntent {
    uint64_t regionId=0,sourceRevision=0,environmentRevision=0;
    uint32_t tick=0;
    Vec3 simulationDirection{},simulationDestination{};
    uint32_t speedCmPerTick=0;
    CaveTraversal traversal=CaveTraversal::Ground;
    CavePlanetMotionBasis basis;
};
using CavePlanetMotionAdmission=std::function<bool(const World&,uint64_t)>;
// Converts the EXISTING ECS steering request; does not register another
// route follower or write Transform. Ground routing still obeys the current
// chart-Y navigation support certificate until Stage54v2.
inline std::optional<CavePlanetMotorIntent> cavePlanetMotorIntent(
        const World& ecs,Entity entity,uint64_t regionId,const CavePlanetFrame& frame,
        const CaveNavigationDataProvider& provider,Vec3 simulationPosition,uint32_t tick,
        const CavePlanetMotionAdmission& admitted){
    const auto* entry=findPlanetCaveRegion(ecs,regionId);
    if(!entry||!admitted||!sameCavePlanetAnchor(entry->region.anchor,frame.anchor())||
       entry->authority.volume.sourceRevision!=provider.revision()||!admitted(ecs,regionId))return {};
    const auto* request=nav::caveMotorRequest(ecs,entity,provider);
    if(!request)return {};
    try{
        CavePlanetMotorIntent out;
        out.regionId=regionId;out.sourceRevision=request->sourceRevision;
        out.environmentRevision=request->environmentRevision;out.tick=tick;
        out.basis=cavePlanetMotionBasis(frame,ecs,simulationPosition);
        out.traversal=request->traversal;out.speedCmPerTick=request->maxSpeedCmPerTick;
        out.simulationDestination=frame.toSimulation(CavePlanetFrame::metres(request->destination));
        auto delta=out.simulationDestination-simulationPosition;
        if(out.traversal==CaveTraversal::Ground){
            if(!out.basis.groundedCompatible)return {};
            delta=delta-out.basis.up*delta.dot(out.basis.up);
        }
        out.simulationDirection=fxNormalize(delta);return out;
    }catch(const std::exception&){return {};}
}
} // namespace aiverse::terraweave::caverns
