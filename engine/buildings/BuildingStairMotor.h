#pragma once
// Genre-neutral ECS stair route motor. All displacement is explicitly
// authorized by the EXISTING game physics/motor collision sweep callback.
// No invented physics world, NavMesh portal, or automatic teleport.
#include "engine/buildings/BuildingNavMeshBridge.h"
#include "engine/core/ECS.h"
#include <aiverselib/systems/BuildingRouteExecutor.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::building {

// The caller's PHYSIS/character-controller owner must sweep the complete
// actor shape along [from,to], accounting for risers, ceilings, railings,
// dynamic obstacles and step/ground support. Returning true means it has
// verified the full motion; this adapter only commits the returned target.
using BuildingStairSweep=std::function<bool(Entity,const Vec3&,const Vec3&)>;

struct BuildingStairMotorReport {
    bool complete=false;
    bool moved=false;
    bool reachedLanding=false;
    std::string error;
};

template<BuildingPositionBody Body>
class BuildingStairMotor {
public:
    // Bind exactly one stair segment of an existing navigation route.
    // Endpoints must originate in the current non-dirty native NavMesh.
    bool begin(Entity actor,const BuildingNavRouteStep& step,
               const nav::Mesh& mesh,BuildingStairSweep sweep,
               std::string* error=nullptr){
        auto fail=[&](const char* why){
            if(error)*error=why;
            return false;
        };
        if(active_)return fail("Stair motor already active");
        if(!actor.valid()||step.action!=BuildingRouteAction::ClimbStairs||
           !step.hasNavAnchors||!step.from||!step.to||
           step.corridor.size()<2||!mesh.built()||mesh.dirty()||!sweep)
            return fail("Stairs require real NavMesh anchors, tread path and physics sweep");
        // No untrusted off-mesh teleport: require exact generated endpoints.
        if(step.corridor.front().x!=step.from->x||
           step.corridor.front().y!=step.from->y||
           step.corridor.front().z!=step.from->z||
           step.corridor.back().x!=step.to->x||
           step.corridor.back().y!=step.to->y||
           step.corridor.back().z!=step.to->z)
            return fail("Stair path endpoints do not match resolved landings");
        actor_=actor;step_=step;waypoint_=1;navRevision_=mesh.revision();
        sweep_=std::move(sweep);active_=true;
        if(error)error->clear();
        return true;
    }

    // At the existing ECS world-owner phase only. A false sweep rejects
    // displacement; it does not advance the route or snap the actor forward.
    // Physics owner must verify that new pose is supported before returning
    // true. The waypoint position is an exact approved tread point.
    BuildingStairMotorReport advance(World& ecs,const nav::Mesh& mesh){
        BuildingStairMotorReport result;
        if(!active_){result.error="No active stair traversal";return result;}
        if(!ecs.alive(actor_)){
            result.error="Stair actor was destroyed outside owner lifecycle";
            return result;
        }
        if(mesh.dirty()||!mesh.built()||mesh.revision()!=navRevision_){
            result.error="Stair NavMesh revision changed; replan required";
            return result;
        }
        auto* body=ecs.tryGet<Body>(actor_);
        if(!body){result.error="Stair actor lacks ECS position Body";return result;}
        if(waypoint_>=step_.corridor.size()){
            result.complete=true;result.reachedLanding=true;return result;
        }
        const Vec3 target=step_.corridor[waypoint_];
        const Vec3 current=body->pos;
        if(!sweep_(actor_,current,target)){
            result.error="Physical stair motor rejected movement";
            return result;
        }
        // The physics sweep is authoritative; this assignment is a
        // collision-approved waypoint, not an unchecked room teleport.
        body->pos=target;
        result.moved=true;
        ++waypoint_;
        result.complete=true;
        result.reachedLanding=waypoint_==step_.corridor.size();
        return result;
    }

    // Only the last collision-approved tread/landing may acknowledge the
    // graph route. If the executor changed phase/edge, fail closed.
    bool acknowledgeLanding(BuildingRouteExecutor& executor){
        if(!active_||waypoint_!=step_.corridor.size()||
           executor.phase()!=BuildingRoutePhase::TraverseStairs)
            return false;
        const auto& route=executor.route();
        if(executor.stepIndex()>=route.steps.size()||
           route.steps[executor.stepIndex()].edgeId!=step_.connectionId)
            return false;
        if(!executor.acknowledgeMovement(step_.toRoom))return false;
        cancel();
        return true;
    }
    void cancel() {
        actor_={};step_={};waypoint_=0;navRevision_=0;
        sweep_={};active_=false;
    }
    bool active()const noexcept{return active_;}
    size_t nextWaypoint()const noexcept{return waypoint_;}
    Entity actor()const noexcept{return actor_;}
private:
    Entity actor_{};
    BuildingNavRouteStep step_{};
    BuildingStairSweep sweep_{};
    size_t waypoint_=0;
    uint64_t navRevision_=0;
    bool active_=false;
};

} // namespace aiverse::building
