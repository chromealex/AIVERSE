#pragma once
// ECS integration for AIVERSE building circulation. No FPS/RTS split:
// callers choose their existing position/body component type (it needs a
// mutable Vec3 .pos). This bridge never creates a second actor store.
// Requires world-owner exclusive access; do not invoke from render threads.
#include "engine/buildings/BuildingCirculationScene.h"
#include "engine/core/ECS.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

namespace aiverse::building {
// Simple generic link component attached to an existing ECS Entity.
// Component remains authoritative with the ECS world, not a copied actor.
struct BuildingLiftPassenger {
    uint64_t sceneId=0;
    uint32_t liftFeature=0;
    uint64_t lastAppliedRevision=0;
    bool attached=false;
};
struct BuildingLiftActorBinding {
    uint64_t sceneId=0;
    Entity actor{};
    uint32_t feature=0;
    int64_t previousYmm=0;
};
// No raw pointers or references to ECS pools survive an owner phase.
struct BuildingLiftActorUpdate {
    std::vector<Entity> moved;
    std::vector<Entity> missing;
    bool complete=true;
    std::string error;
};

template<class Body>
concept BuildingPositionBody = requires(Body b) {
    { b.pos.x } -> std::same_as<Fixed&>;
    { b.pos.y } -> std::same_as<Fixed&>;
    { b.pos.z } -> std::same_as<Fixed&>;
};
inline Fixed buildingFixedMm(int64_t mm) {
    return Fixed::fromRatio(mm,1000);
}
template<BuildingPositionBody Body>
class BuildingLiftEcsBridge {
public:
    // The actor feet must lie over the cabin interior and at its platform
    // elevation. No implicit teleport/attachment or remote client boarding.
    bool board(World& ecs,BuildingCirculationHost& host,
               uint64_t sceneId,Entity entity,uint32_t liftFeature,
               float width,float depth,Vec3 origin,uint32_t yaw,
               std::string* error=nullptr){
        auto fail=[&](const std::string& why) {
            if(error)*error=why;
            return false;
        };
        if(sceneId==0||!entity.valid()||!ecs.alive(entity)||!host.lifts().contains(liftFeature)||
           (ecs.has<BuildingLiftPassenger>(entity)&&
            ecs.get<BuildingLiftPassenger>(entity).attached))
            return fail("ECS actor does not exist or is already on a lift");
        auto* body=ecs.tryGet<Body>(entity);
        if(!body)return fail("ECS actor has no writable position/body component");
        const auto& lift=host.lifts().at(liftFeature);
        const auto pose=lift.platformPose();
        const auto& snapshot=lift.snapshot();
        if(!lift.readyAt(snapshot.currentFloor))
            return fail("Lift doors are not open at an available floor");
        const auto min=buildingLocalToWorld(pose.x0,0,pose.z0,width,depth,origin,yaw);
        const auto max=buildingLocalToWorld(pose.x1,0,pose.z1,width,depth,origin,yaw);
        const Fixed border=buildingFixedMm(120);
        const Fixed floor=origin.y+buildingFixedMm(pose.yMm)+buildingFixedMm(
            int64_t(std::llround(double(pose.thickness)*1000.)));
        if(body->pos.x<fxMin(min.x,max.x)+border||
           body->pos.x>fxMax(min.x,max.x)-border||
           body->pos.z<fxMin(min.z,max.z)+border||
           body->pos.z>fxMax(min.z,max.z)-border||
           fxAbs(body->pos.y-floor)>buildingFixedMm(300))
            return fail("Actor feet are not physically inside the boarding volume");
        if(!host.boardRider(entity.packed(),liftFeature,snapshot.currentFloor))
            return fail("Lift denied actor boarding");
        if(ecs.has<BuildingLiftPassenger>(entity))
            ecs.get<BuildingLiftPassenger>(entity)={
                sceneId,liftFeature,snapshot.revision,true};
        else
            ecs.add<BuildingLiftPassenger>(entity,BuildingLiftPassenger{
                sceneId,liftFeature,snapshot.revision,true});
        if(error)error->clear();
        return true;
    }

    // Published ONLY after successful cabin-collider publication by
    // BuildingCirculationScene. Does not move missing/destroyed entities.
    BuildingLiftActorUpdate applyPublished(
        World& ecs,const BuildingCirculationHost& host,
        const BuildingSceneUpdate& publication,uint64_t sceneId){
        BuildingLiftActorUpdate report;
        if(sceneId==0){
            report.complete=false;report.error="Invalid scene id";return report;
        }
        for(const auto& move:publication.publishedRiders){
            Entity entity=Entity::fromPacked(move.actorId);
            if(!ecs.alive(entity)||!host.isRider(move.actorId,move.liftFeature)){
                report.complete=false;report.missing.push_back(entity);continue;
            }
            auto* marker=ecs.tryGet<BuildingLiftPassenger>(entity);
            auto* body=ecs.tryGet<Body>(entity);
            if(!marker||!body||!marker->attached||marker->sceneId!=sceneId||
               marker->liftFeature!=move.liftFeature){
                report.complete=false;report.missing.push_back(entity);continue;
            }
            // A duplicate publish or out-of-order replication cannot apply
            // the same elevator displacement twice to one actor.
            if(move.revision<=marker->lastAppliedRevision)continue;
            if(move.newYmm<move.oldYmm-1200||move.newYmm>move.oldYmm+1200){
                report.complete=false;
                report.error="Lift motion exceeds max one-tick travel";break;
            }
            body->pos.y+=buildingFixedMm(move.newYmm-move.oldYmm);
            marker->lastAppliedRevision=move.revision;
            report.moved.push_back(entity);
        }
        return report;
    }

    // The game motor confirms actual feet at destination landing before
    // this transition. No actor transform teleport here.
    bool exit(World& ecs,BuildingCirculationHost& host,
              uint64_t sceneId,Entity entity,uint32_t liftFeature,
              uint32_t floor){
        if(!ecs.alive(entity))return false;
        auto* marker=ecs.tryGet<BuildingLiftPassenger>(entity);
        if(!marker||!marker->attached||marker->sceneId!=sceneId||
           marker->liftFeature!=liftFeature)return false;
        const auto it=host.lifts().find(liftFeature);
        if(it==host.lifts().end()||!it->second.readyAt(floor))return false;
        if(!host.exitRider(entity.packed(),liftFeature,floor))return false;
        marker->attached=false;
        return true;
    }

    // A disconnected actor can be released only when the real lift doors
    // are already open at a landing. This API must not eject moving riders.
    bool releaseAtLanding(World& ecs,BuildingCirculationHost& host,
                          Entity entity,uint32_t liftFeature){
        if(!ecs.alive(entity))return false;
        const auto it=host.lifts().find(liftFeature);
        if(it==host.lifts().end()||!it->second.snapshot().doorsOpen())return false;
        auto* marker=ecs.tryGet<BuildingLiftPassenger>(entity);
        if(!marker||!marker->attached||marker->liftFeature!=liftFeature)return false;
        if(!host.exitRider(entity.packed(),liftFeature,
                           it->second.snapshot().currentFloor))return false;
        marker->attached=false;
        return true;
    }
};
} // namespace aiverse::building
