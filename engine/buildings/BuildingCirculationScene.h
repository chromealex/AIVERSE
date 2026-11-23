#pragma once
// Optional native world-owner adapter. Wires the already-existing
// SpatialWorld, nav::Mesh and BuildingCirculationHost without creating a
// second entity store or a new navigation system. The authoritative game
// calls update() during its world-owner phase. Rendering reads the returned
// motion commands and moves a dedicated platform instance (no CPU remesh).
#include "engine/buildings/BuildingCirculationHost.h"
#include "engine/buildings/BuildingLiftColliderOwner.h"
#include "engine/buildings/BuildingLiftDoorOwner.h"
#include <aiverselib/systems/RuntimeBuildingGenerator.h>
#include <map>
#include <string>
#include <vector>

namespace aiverse::building {

struct BuildingSceneUpdate {
    BuildingCirculationTick tick; // raw authoritative events and attempted moves
    // Only these motions have successfully committed physical collision.
    // Apply them to existing ECS/render owners in their world-owner phase.
    std::vector<BuildingLiftPlatformMotion> publishedPlatforms;
    std::vector<BuildingRiderDisplacement> publishedRiders;
    bool published=true;
    bool navDirty=false;
    std::string error;
};
class BuildingCirculationScene {
public:
    BuildingCirculationHost& host()noexcept{return host_;}
    const BuildingCirculationHost& host()const noexcept{return host_;}
    const BuildingLiftColliderOwner& colliders()const noexcept{return colliders_;}
    const BuildingLiftDoorOwner& doors()const noexcept{return doors_;}

    // All collider IDs come from the calling game's normal ECS/physics owner.
    // Invalid/duplicate IDs are rejected by SpatialWorld::add. Do not
    // silently borrow colliders from another building or owner.
    bool attach(const RuntimeBuildingResult& generated,
                world::SpatialWorld& spatial,nav::Mesh* navmesh,
                const std::map<uint32_t,uint32_t>& colliderIds,
                Vec3 origin,uint32_t yaw,std::string* error=nullptr){
        if(attached_){if(error)*error="Scene already attached";return false;}
        BuildingCirculationHost staged;
        std::string problem;
        if(!staged.initialize(generated,origin,yaw,&problem)){
            if(error)*error=problem;return false;
        }
        const auto& lifts=staged.lifts();
        if(colliderIds.size()!=lifts.size()){
            if(error)*error="One unique collider id is required per elevator";
            return false;
        }
        // Verify ids and complete registration before exposing the scene.
        std::vector<uint32_t> registered;
        const float W=generated.building.constructionSize.width;
        const float D=generated.building.constructionSize.depth;
        for(const auto& [feature,lift]:lifts){
            const auto it=colliderIds.find(feature);
            if(it==colliderIds.end()||it->second==0||
               !colliders_.registerPlatform(spatial,navmesh,feature,it->second,
                   lift.platformPose(),W,D,origin,yaw)){
                for(const auto rollback:registered)
                    (void)colliders_.remove(spatial,navmesh,rollback);
                if(error)*error="Could not register dynamic elevator collider";
                return false;
            }
            registered.push_back(feature);
        }
        host_=std::move(staged);
        width_=W;depth_=D;origin_=origin;yaw_=yaw;
        attached_=true;faulted_=false;
        if(error)error->clear();
        return true;
    }

    // Optional physical landing doors: game supplies collision IDs from its
    // own allocation pool. Open landings contain no door leaf AABBs.
    // The scene remains usable without doors for headless/server prototypes,
    // but runtime boarding must also use physical actor volume validation.
    bool attachDoors(world::SpatialWorld& spatial,nav::Mesh* navmesh,
                     const std::map<BuildingLiftDoorKey,BuildingLiftDoorIds>& ids,
                     std::string* error=nullptr){
        if(!attached_||faulted_||doors_.enabled()){
            if(error)*error="Scene unavailable or doors already attached";
            return false;
        }
        return doors_.attach(spatial,navmesh,host_.graph(),host_.lifts(),
                             ids,width_,depth_,origin_,yaw_,error);
    }
    bool doorsOpenAt(uint32_t feature,uint32_t floor)const {
        if(!attached_||faulted_)return false;
        const auto it=host_.lifts().find(feature);
        return it!=host_.lifts().end()&&it->second.readyAt(floor)&&
               (!doors_.enabled()||doors_.openAt(feature,floor));
    }

    // Authoritative world update: after advancing one bounded tick, publish
    // all dynamic platform collision positions. If any update fails, freeze
    // lift commands (fail closed) rather than claiming a valid navmesh route.
    // The caller must reconcile the source collider and reattach the scene.
    BuildingSceneUpdate update(world::SpatialWorld& spatial,
                               nav::Mesh* navmesh,uint32_t elapsedMs){
        BuildingSceneUpdate result;
        if(!attached_||faulted_||elapsedMs==0||elapsedMs>1000){
            result.published=false;
            result.error=(!attached_?"Circulation scene is not attached":
                faulted_?"Circulation scene needs collision reconciliation":
                "Tick delta must be 1..1000ms");
            return result;
        }
        result.tick=host_.advance(elapsedMs);
        // Before publishing new cabin/rider poses, commit landing-door
        // transitions to the same SpatialWorld. A door collision failure
        // invalidates navigation and suppresses this tick's actor poses.
        if(doors_.enabled()&&
           !doors_.synchronize(spatial,navmesh,host_.lifts())){
            result.published=false;
            result.error="Could not synchronize lift landing door collision";
            faulted_=true;
            if(navmesh)navmesh->markAllDirty();
            return result;
        }
        for(const auto& motion:result.tick.platformMovements){
            if(!colliders_.applyMovement(spatial,navmesh,motion)){
                result.published=false;
                result.error="Dynamic platform collider update rejected; navigation is invalid";
                faulted_=true;
                host_.takeLiftOffline(motion.featureIndex,true);
                if(navmesh)navmesh->markAllDirty();
                break;
            }
            result.publishedPlatforms.push_back(motion);
            for(const auto& rider:result.tick.riderMovements)
                if(rider.liftFeature==motion.featureIndex &&
                   rider.revision==motion.revision)
                    result.publishedRiders.push_back(rider);
            result.navDirty=navmesh&&navmesh->dirty();
        }
        // On a partial failure, already-published platform/rider pairs remain
        // authoritative. Unpublished movements MUST NOT be applied to actor
        // transforms or rendering. The host stays faulted until reattached.
        return result;
    }

    // Call after the caller has refreshed navmesh from its own authoritative
    // collision-backed nav::Source. No implicit rebuilding on the render
    // thread. A stale nav revision will otherwise reject route queries.
    bool rebuildNavigation(const nav::Mesh& mesh,std::string* error=nullptr){
        if(!attached_||faulted_||mesh.dirty()){
            if(error)*error="Circulation scene unavailable or navmesh dirty";
            return false;
        }
        return host_.rebuildNavigation(mesh,error);
    }

    // Removes exactly the registered dynamic collider IDs. Static generated
    // building collision, assets and ECS entities remain owned by the game.
    void detach(world::SpatialWorld& spatial,nav::Mesh* navmesh){
        if(!attached_)return;
        doors_.detach(spatial,navmesh);
        std::vector<uint32_t> ids;
        for(const auto& [feature,lift]:host_.lifts()){
            (void)lift;ids.push_back(feature);
        }
        for(const auto feature:ids)
            (void)colliders_.remove(spatial,navmesh,feature);
        host_.clear();
        attached_=false;faulted_=false;
    }
    bool attached()const noexcept{return attached_;}
    bool faulted()const noexcept{return faulted_;}

private:
    BuildingCirculationHost host_;
    BuildingLiftColliderOwner colliders_;
    BuildingLiftDoorOwner doors_;
    Vec3 origin_{};
    float width_=0,depth_=0;
    uint32_t yaw_=0;
    bool attached_=false,faulted_=false;
};
} // namespace aiverse::building
