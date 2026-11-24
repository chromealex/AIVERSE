#pragma once
#include "engine/physics/PhysisBox3D.h"
#include "engine/world/TerraWeaveCavernsPlanetFrame.h"

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
} // namespace aiverse::terraweave::caverns
