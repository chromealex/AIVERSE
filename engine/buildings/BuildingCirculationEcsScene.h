#pragma once
// Integrated world-owner sequence for a procedural building with existing
// ECS World + SpatialWorld + nav::Mesh. Template Body is the game's existing
// transform/body component with a Vec3 pos; no RTS/FPS branching, no second
// entity registry. This runs only at the authoritative simulation owner.
#include "engine/buildings/BuildingCirculationScene.h"
#include "engine/buildings/BuildingLiftEcsBridge.h"
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::building {
template<BuildingPositionBody Body>
class BuildingCirculationEcsScene {
public:
    struct Tick {
        BuildingSceneUpdate world;
        BuildingLiftActorUpdate actors;
        bool complete=true;
        std::string error;
    };
    bool attach(World& ecs,world::SpatialWorld& spatial,nav::Mesh* navmesh,
                const RuntimeBuildingResult& generated,
                const std::map<uint32_t,uint32_t>& platformColliderIds,
                uint64_t sceneId,Vec3 origin,uint32_t yaw,
                std::string* error=nullptr){
        if(!sceneId){
            if(error)*error="Scene identity must be nonzero";
            return false;
        }
        if(!scene_.attach(generated,spatial,navmesh,platformColliderIds,
                          origin,yaw,error))return false;
        ecs_=&ecs;
        sceneId_=sceneId;
        origin_=origin;yaw_=yaw;
        width_=generated.building.constructionSize.width;
        depth_=generated.building.constructionSize.depth;
        return true;
    }
    bool attachLandingDoors(world::SpatialWorld& spatial,nav::Mesh* navmesh,
                            const std::map<BuildingLiftDoorKey,BuildingLiftDoorIds>& ids,
                            std::string* error=nullptr){
        return scene_.attachDoors(spatial,navmesh,ids,error);
    }
    bool board(Entity actor,uint32_t feature,std::string* error=nullptr){
        if(!ecs_||!scene_.attached()||scene_.faulted()){
            if(error)*error="Building circulation scene is unavailable";
            return false;
        }
        const auto it=scene_.host().lifts().find(feature);
        if(it==scene_.host().lifts().end()){
            if(error)*error="Unknown lift feature";
            return false;
        }
        const uint32_t floor=it->second.snapshot().currentFloor;
        if(!scene_.doorsOpenAt(feature,floor)){
            if(error)*error="Physical landing doors are closed";
            return false;
        }
        return ecsBridge_.board(*ecs_,scene_.host(),sceneId_,actor,feature,
                                 width_,depth_,origin_,yaw_,error);
    }
    bool exit(Entity actor,uint32_t feature,uint32_t floor){
        if(!ecs_||!scene_.doorsOpenAt(feature,floor))return false;
        // Actor movement/boarding volume validation should have run in
        // the game's motor/interaction owner before invoking exit().
        return ecsBridge_.exit(*ecs_,scene_.host(),sceneId_,actor,feature,floor);
    }

    Tick advance(world::SpatialWorld& spatial,nav::Mesh* navmesh,
                 uint32_t milliseconds){
        Tick out;
        if(!ecs_){
            out.complete=false;out.error="No ECS World is attached";
            return out;
        }
        out.world=scene_.update(spatial,navmesh,milliseconds);
        // Consume ONLY successfully committed platform/rider pairs.
        out.actors=ecsBridge_.applyPublished(
            *ecs_,scene_.host(),out.world,sceneId_);
        out.complete=out.world.published&&out.actors.complete;
        if(!out.complete){
            out.error=!out.world.published?out.world.error:out.actors.error;
            if(out.error.empty())
                out.error="A rider actor was destroyed or moved outside its owner";
            scene_.fault(navmesh);
        }
        return out;
    }
    bool rebuildNavigation(const nav::Mesh& mesh,std::string* error=nullptr){
        return scene_.rebuildNavigation(mesh,error);
    }
    void detach(world::SpatialWorld& spatial,nav::Mesh* navmesh){
        scene_.detach(spatial,navmesh);
        ecs_=nullptr;sceneId_=0;
    }
    BuildingCirculationScene& scene()noexcept{return scene_;}
    const BuildingCirculationScene& scene()const noexcept{return scene_;}
    uint64_t sceneId()const noexcept{return sceneId_;}
private:
    World* ecs_=nullptr;
    BuildingCirculationScene scene_;
    BuildingLiftEcsBridge<Body> ecsBridge_;
    uint64_t sceneId_=0;
    float width_=0,depth_=0;
    uint32_t yaw_=0;
    Vec3 origin_{};
};
} // namespace aiverse::building
