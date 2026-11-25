#pragma once
// Adapter to the EXISTING common ECS locomotion input policy. Horizontal
// wishes are consumed by its normal locomotion/physics systems. Stairs
// and threshold movement must use the host's real swept character motor.
#include "engine/buildings/BuildingAgentNavigation.h"
#include <aiverselib/mechanics/EcsLocomotionSystems.h>
namespace aiverse::building {
inline BuildingAgentMotor buildingAgentLocomotionMotor(
    std::function<BuildingAgentMoveResult(Simulation&,Entity,Vec3,BuildingAgentMoveKind)> physicalStep){
    using lib::mechanics::LocomotionInputComponent;
    BuildingAgentMotor motor;
    motor.stop=[](Simulation& sim,Entity actor){
        if(auto* input=sim.world().tryGet<LocomotionInputComponent>(actor))
            input->value.wish={};
    };
    motor.move=[physicalStep=std::move(physicalStep)](Simulation& sim,Entity actor,Vec3 target,BuildingAgentMoveKind kind){
        if(kind!=BuildingAgentMoveKind::Ground){
            if(auto* input=sim.world().tryGet<LocomotionInputComponent>(actor))input->value.wish={};
            return physicalStep?physicalStep(sim,actor,target,kind):BuildingAgentMoveResult::Blocked;
        }
        auto* input=sim.world().tryGet<LocomotionInputComponent>(actor);
        const auto* t=sim.world().tryGetConst<Transform>(actor);
        if(!input||!t)return BuildingAgentMoveResult::Blocked;
        if(buildingAgentAt(sim.world(),actor,target)){
            input->value.wish={};return BuildingAgentMoveResult::Reached;
        }
        const Vec3 diff{target.x-t->pos.x,{},target.z-t->pos.z};
        // Slow down near waypoints; existing locomotion owner still
        // handles acceleration, grounded/ceiling and collision checks.
        const Fixed length=diff.length();
        if(length<=Fixed{}){input->value.wish={};return BuildingAgentMoveResult::Moving;}
        const auto wish=fxNormalize(diff)*fxMin(1_fx,length*2);
        input->value.wish={wish.x,wish.z};
        return BuildingAgentMoveResult::Moving;
    };
    return motor;
}
}
