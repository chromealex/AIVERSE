#pragma once
// Authoritative game-owner integration: existing ECS World, SpatialWorld,
// nav::Mesh, building circulation and the existing network snapshot transport.
// There is no alternate entity, physics or networking backend.
#include "engine/buildings/BuildingLiftEcsBridge.h"
#include "engine/buildings/BuildingLiftReplication.h"
#include "engine/buildings/BuildingLiftPresentation.h"
#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace aiverse::building {

struct BuildingLiftGameTick {
    BuildingSceneUpdate scene;
    BuildingLiftActorUpdate actors;
    std::optional<BuildingLiftStateFrame> frame;
    bool complete=false;
    std::string error;
};

template<BuildingPositionBody Body>
class BuildingLiftGameSession {
public:
    using Authorize=std::function<bool(Entity)>;
    bool attach(const RuntimeBuildingResult& generated,
                world::SpatialWorld& spatial,nav::Mesh* nav,
                const std::map<uint32_t,uint32_t>& colliderIds,
                uint64_t sceneId,Vec3 origin,uint32_t yaw,
                Authorize authorize={},std::string* error=nullptr){
        if(attached_||sceneId==0||yaw>3||!authorize){
            if(error)*error="Invalid scene identity or session already attached";
            return false;
        }
        std::string problem;
        if(!scene_.attach(generated,spatial,nav,colliderIds,origin,yaw,&problem)){
            if(error)*error=problem;
            return false;
        }
        origin_=origin;yaw_=yaw;sceneId_=sceneId;
        width_=generated.building.constructionSize.width;
        depth_=generated.building.constructionSize.depth;
        authorize_=std::move(authorize);
        attached_=true;
        if(error)error->clear();
        return true;
    }
    bool attachDoors(world::SpatialWorld& spatial,nav::Mesh* nav,
                     const std::map<BuildingLiftDoorKey,BuildingLiftDoorIds>& ids,
                     BuildingCirculationScene::ObstructionProbe sensor,
                     std::string* error=nullptr){
        // The game session has no ECS World pointer at attach() time. A
        // live authoritative sensor must therefore be explicitly supplied
        // by the caller (characters, vehicles and cargo), never inferred
        // from client network messages or omitted silently.
        if(!attached_||!sensor){
            if(error)*error="Physical lift doors require an authoritative obstruction sensor";
            return false;
        }
        scene_.setDoorObstructionProbe(std::move(sensor));
        if(!scene_.attachDoors(spatial,nav,ids,error)){
            scene_.setDoorObstructionProbe({});
            return false;
        }
        if(error)error->clear();
        return true;
    }
    // Legacy call sites without a sensor reject early instead of attaching
    // doors and failing a later world tick without explanation.
    bool attachDoors(world::SpatialWorld&,nav::Mesh*,
                     const std::map<BuildingLiftDoorKey,BuildingLiftDoorIds>&,
                     std::string* error=nullptr){
        if(error)*error="Supply the authoritative door obstruction callback";
        return false;
    }
    bool board(World& ecs,Entity actor,uint32_t feature,
               std::string* error=nullptr){
        if(!allowed(ecs,actor)||scene_.faulted()||!scene_.doors().enabled()){
            if(error)*error="Actor unauthorized or scene faulted";
            return false;
        }
        auto it=scene_.host().lifts().find(feature);
        if(it==scene_.host().lifts().end()||
           !scene_.doorsOpenAt(feature,it->second.snapshot().currentFloor)){
            if(error)*error="Lift doors not physically open";
            return false;
        }
        // ECS bridge verifies actual feet within the platform volume.
        return actors_.board(ecs,scene_.host(),sceneId_,actor,feature,
                            width_,depth_,origin_,yaw_,error);
    }
    bool call(World& ecs,Entity actor,uint32_t feature,uint32_t floor){
        return allowed(ecs,actor)&&!scene_.faulted()&&
               scene_.host().callLift(feature,floor);
    }
    bool requestDestination(World& ecs,Entity actor,uint32_t feature,
                            uint32_t floor){
        return allowed(ecs,actor)&&!scene_.faulted()&&
               scene_.host().isRider(actor.packed(),feature)&&
               scene_.host().requestLiftDestination(feature,floor);
    }
    bool exit(World& ecs,Entity actor,uint32_t feature,uint32_t floor){
        if(!allowed(ecs,actor)||scene_.faulted()||
           !scene_.doors().enabled()||!scene_.doorsOpenAt(feature,floor))return false;
        const auto lift=scene_.host().lifts().find(feature);
        if(lift==scene_.host().lifts().end())return false;
        // Disembark is acknowledged only AFTER the actor's motor has
        // reached the landing outside the cabin; being inside is NOT an
        // exit. The ECS bridge checks the real landing geometry.
        return actors_.exit(ecs,scene_.host(),sceneId_,actor,feature,floor,
                            width_,depth_,origin_,yaw_);
    }
    // Actor teardown/logout can safely release a rider only at an actually
    // open landing. During travel the game must retain the actor until
    // arrival, or fault this scene and reconcile actor lifecycle outside
    // the component store (no silent passenger-count decrement in motion).
    bool releaseAtLanding(World& ecs,Entity actor,uint32_t feature){
        if(!attached_||scene_.faulted()||!ecs.alive(actor)||
           !scene_.doors().enabled())return false;
        const auto lift=scene_.host().lifts().find(feature);
        if(lift==scene_.host().lifts().end()||
           !scene_.doorsOpenAt(feature,lift->second.snapshot().currentFloor))
            return false;
        const auto* marker=ecs.tryGet<BuildingLiftPassenger>(actor);
        if(!marker||!marker->attached||marker->sceneId!=sceneId_)
            return false;
        return actors_.releaseAtLanding(ecs,scene_.host(),actor,feature);
    }
    // Safe ordinary scene unload rejects active riders. Forced detach()
    // remains for authoritative game/world destruction only.
    bool detachIfVacant(world::SpatialWorld& spatial,nav::Mesh* nav){
        if(!attached_||!scene_.host().riderBindings().empty())return false;
        detach(spatial,nav);
        return true;
    }
    bool hasPassengers()const noexcept{
        return attached_&&!scene_.host().riderBindings().empty();
    }
    // Call on the authoritative world-owner phase only. Returns an existing
    // transport-ready snapshot ONLY after collision and ECS rider movement
    // have been published successfully, never after an attempted delta.
    BuildingLiftGameTick advance(World& ecs,world::SpatialWorld& spatial,
                                 nav::Mesh* nav,uint32_t elapsedMs,
                                 uint64_t serverTick){
        BuildingLiftGameTick result;
        if(!attached_||scene_.faulted()||serverTick<=lastPublishedTick_){
            result.error="Scene inactive or snapshot tick stale";
            return result;
        }
        result.scene=scene_.update(spatial,nav,elapsedMs);
        if(!result.scene.published){
            result.error=result.scene.error;scene_.fault(nav);return result;
        }
        result.actors=actors_.applyPublished(ecs,scene_.host(),
                                            result.scene,sceneId_);
        if(!result.actors.complete){
            result.error=result.actors.error.empty()?
                "ECS rider publication failed":result.actors.error;
            scene_.fault(nav);return result;
        }
        auto frame=captureBuildingLiftServerFrame(scene_.host(),
                                                  sceneId_,serverTick);
        if(!validateBuildingLiftStateFrame(frame,&result.error)){
            scene_.fault(nav);return result;
        }
        result.frame=std::move(frame);
        result.complete=true;
        lastPublishedTick_=serverTick;
        return result;
    }
    bool rebuildNavigation(const nav::Mesh& mesh,std::string* error=nullptr){
        return attached_&&!scene_.faulted()&&
               scene_.rebuildNavigation(mesh,error);
    }
    // Rendering owner creates the reusable CPU assets once at spawn, then
    // uploads/leases them through Renderer3D. This method does not touch GPU.
    BuildingLiftVisualAssets makeVisualAssets(const BuildingStyle& style)const{
        if(!attached_||scene_.faulted())return {};
        return makeBuildingLiftVisualAssets(scene_.host(),style);
    }
    // Only committed authoritative cabin state can drive presentation.
    // If collision/actor publication faults this session, suppress the
    // frame until the world owner reconciles and reattaches it.
    BuildingLiftPresentationFrame visualFrame()const{
        if(!attached_||scene_.faulted()){
            BuildingLiftPresentationFrame failed;
            failed.error="Lift scene is not attached or needs reconciliation";
            return failed;
        }
        return buildingLiftVisualFrame(scene_.host(),scene_.doors(),
                                       width_,depth_,origin_,yaw_);
    }
    const BuildingCirculationScene& scene()const noexcept{return scene_;}
    BuildingCirculationScene& scene()noexcept{return scene_;}
    bool attached()const noexcept{return attached_;}
    uint64_t sceneId()const noexcept{return sceneId_;}
    void detach(world::SpatialWorld& spatial,nav::Mesh* nav){
        if(!attached_)return;
        scene_.detach(spatial,nav);
        attached_=false;sceneId_=0;lastPublishedTick_=0;authorize_={};
        // Game ECS owner must retire attached passenger components at scene
        // teardown; this facade never destroys actors from another system.
    }
private:
    bool allowed(World& ecs,Entity actor)const {
        return attached_&&actor.valid()&&ecs.alive(actor)&&
               authorize_&&authorize_(actor);
    }
    bool insidePlatform(Vec3 feet,const BuildingLiftPlatformPose& pose)const {
        const auto a=buildingLocalToWorld(pose.x0,0,pose.z0,
                                         width_,depth_,origin_,yaw_);
        const auto c=buildingLocalToWorld(pose.x1,0,pose.z1,
                                         width_,depth_,origin_,yaw_);
        const Fixed rim=buildingFixedMm(120);
        const Fixed y=origin_.y+buildingFixedMm(pose.yMm)+
            Fixed::fromRatio(int64_t(std::llround(
                double(pose.thickness)*1000.)),1000);
        return feet.x>=fxMin(a.x,c.x)+rim&&feet.x<=fxMax(a.x,c.x)-rim&&
               feet.z>=fxMin(a.z,c.z)+rim&&feet.z<=fxMax(a.z,c.z)-rim&&
               fxAbs(feet.y-y)<=buildingFixedMm(300);
    }
    BuildingCirculationScene scene_;
    BuildingLiftEcsBridge<Body> actors_;
    Authorize authorize_;
    Vec3 origin_{};
    float width_=0,depth_=0;
    uint32_t yaw_=0;
    uint64_t sceneId_=0,lastPublishedTick_=0;
    bool attached_=false;
};
} // namespace aiverse::building
