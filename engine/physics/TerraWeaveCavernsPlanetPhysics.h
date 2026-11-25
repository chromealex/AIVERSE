#pragma once
#include "engine/physics/PhysisBox3D.h"
#include "engine/world/TerraWeaveCavernsPlanetMotion.h"
#include "engine/physics/Character.h"

namespace aiverse::terraweave::caverns {
// For a Scene using Surface simulation-anchor coordinates. Call once per
// physics step for each dynamic actor, instead of (not in addition to)
// its existing applyPhysisToBody call. PHYSIS remains the force authority.
inline bool applyCavePlanetPhysis(physics::Scene& scene,const World& frozen,
        physics::BodyHandle handle,const CavePlanetFrame& frame,
        const physics::PhysisBodyMaterial& material={}){
    if(!scene.valid(handle)||scene.motion(handle)!=physics::Motion::Dynamic)return false;
    Vec3 position;
    if(!physics::physisVec3(scene.position(handle),position))return false;
    try{(void)frame.fromSimulation(position);}
    catch(const std::exception&){return false;}
    return physics::applyPhysisToBody(scene,frozen,handle,material);
}
// Existing Box3D character motor, not an extra physics step. The basis
// and input yaw are expressed relative to the chart tangent reference.
inline std::optional<b3Quat> cavePlanetCharacterOrientation(const CavePlanetMotionBasis& basis){
    if(!basis.groundedCompatible)return {};
    const b3Matrix3 m{physics::physisB3(basis.right),physics::physisB3(basis.up),physics::physisB3(basis.back)};
    const auto q=b3NormalizeQuat(b3MakeQuatFromMatrix(&m));
    if(!b3IsValidQuat(q))return {};
    return q;
}
// Use instead of stepCharacter for a native physics-owned actor. Host
// performs proxy/push/Scene::step exactly once, as with the ordinary motor.
// In zero gravity the grounded motor refuses; host uses its flight motor.
inline std::optional<b3Quat> stepCavePlanetCharacter(physics::Scene& scene,const World& frozen,
        const CavePlanetFrame& frame,const physics::CharacterParams& params,
        physics::CharacterState& state,const physics::CharacterInput& input,
        uint64_t regionId,const CavePlanetMotionAdmission& admitted,
        physics::Pushes* pushes=nullptr,physics::BodyHandle self=physics::kNoBody){
    if(pushes)pushes->clear();
    const auto* entry=findPlanetCaveRegion(frozen,regionId);
    if(!entry||!sameCavePlanetAnchor(entry->region.anchor,frame.anchor())||
       !admitted||!admitted(frozen,regionId))return {};
    Vec3 point;
    if(!physics::physisVec3(state.position,point))return {};
    try{
        const auto basis=cavePlanetMotionBasis(frame,frozen,point);
        const auto q=cavePlanetCharacterOrientation(basis);
        if(!q)return {};
        auto p=params;p.gravity=float(basis.magnitude.toDouble());
        auto candidate=state;physics::Pushes localPushes;
        if(!physics::stepCharacterOriented(scene.world(),p,candidate,input,scene.dt(),*q,
            pushes?&localPushes:nullptr,self))return {};
        Vec3 next;
        if(!physics::physisVec3(candidate.position,next))return {};
        (void)frame.fromSimulation(next);
        state=candidate;
        if(pushes)*pushes=std::move(localPushes);
        return q;
    }catch(const std::exception&){return {};}
}
} // namespace aiverse::terraweave::caverns
