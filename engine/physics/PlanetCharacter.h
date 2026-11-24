// Native opt-in character step. Box3D scene and capsule owner retain all
// contact solving, movement and replication. Scene coordinates are body-local.
#pragma once
#include "Character.h"
#include "PhysisBox3D.h"
#include "aiverselib/world/PlanetCharacterGravityFrame.h"

namespace aiverse::physics {
inline bool stepPlanetCharacter(Scene& scene,const World& world,const game::Surface& surface,
        std::string_view body,Vec3 referenceForward,const CharacterParams& settings,
        CharacterState& state,const CharacterInput& input,b3Quat& orientation,
        Pushes* out=nullptr,BodyHandle self=kNoBody){
    if(out)out->clear();
    const int index=surface.findBody(std::string(body));
    if(index<0||!surface.bodies[size_t(index)].hasSurface())return false;
    Vec3 feet{};
    if(!physisVec3(state.position,feet))return false;
    const auto anchor=game::Surface::anchor(index);
    const auto frame=lib::worldprocess::planetCharacterGravityFrame(world,surface,feet+anchor,body,referenceForward);
    if(!frame||frame->body!=index)return false;
    const auto& q=frame->rotation;
    b3Quat rotation{float(q.x.toDouble()),float(q.y.toDouble()),float(q.z.toDouble()),float(q.w.toDouble())};
    auto params=settings;
    params.gravity=float(frame->magnitude.toDouble());
    auto next=state;
    Pushes pushes;
    if(!stepCharacterOriented(scene.world(),params,next,input,scene.dt(),rotation,
        frame->allowGround,out?&pushes:nullptr,self))return false;
    Vec3 moved{};
    if(!physisVec3(next.position,moved))return false;
    const auto nextFrame=lib::worldprocess::surfaceFrame(&surface,moved+anchor);
    if(!nextFrame||nextFrame->body!=index)return false;
    state=next;orientation=rotation;
    if(out)*out=std::move(pushes);
    return true;
}
} // namespace aiverse::physics
