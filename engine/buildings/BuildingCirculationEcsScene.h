#pragma once
// Integrated world-owner sequence for a procedural building with existing
// ECS World + SpatialWorld + nav::Mesh. Template Body is the game's existing
// transform/body component with a Vec3 pos; no RTS/FPS branching, no second
// entity registry. This runs only at the authoritative simulation owner.
#include "engine/buildings/BuildingCirculationScene.h"
#include "engine/buildings/BuildingLiftEcsBridge.h"
#include "engine/buildings/BuildingLiftReplication.h"
#include "engine/buildings/BuildingLiftCommandWire.h"
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::building {
template<BuildingPositionBody Body>
class BuildingCirculationEcsScene {
public:
    BuildingCirculationEcsScene()=default;
    // The door obstruction callback captures this world-owner object.
    BuildingCirculationEcsScene(const BuildingCirculationEcsScene&)=delete;
    BuildingCirculationEcsScene& operator=(const BuildingCirculationEcsScene&)=delete;
    BuildingCirculationEcsScene(BuildingCirculationEcsScene&&)=delete;
    BuildingCirculationEcsScene& operator=(BuildingCirculationEcsScene&&)=delete;
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
                std::string* error=nullptr,
                const BuildingCirculationCheckpoint* restored=nullptr){
        if(!sceneId){
            if(error)*error="Scene identity must be nonzero";
            return false;
        }
        if(restored&&!validatePassengers(ecs,sceneId,*restored)){
            if(error)*error="Lift checkpoint disagrees with ECS passengers";
            return false;
        }
        if(!scene_.attach(generated,spatial,navmesh,platformColliderIds,
                          origin,yaw,error,restored))return false;
        ecs_=&ecs;
        sceneId_=sceneId;
        origin_=origin;yaw_=yaw;
        width_=generated.building.constructionSize.width;
        depth_=generated.building.constructionSize.depth;
        lastPublishComplete_=true;
        commandAuthority_.reset();
        scene_.setDoorObstructionProbe([this](uint32_t feature,uint32_t floor){
            return this->doorwayOccupied(feature,floor);
        });
        return true;
    }
    bool attachLandingDoors(world::SpatialWorld& spatial,nav::Mesh* navmesh,
                            const std::map<BuildingLiftDoorKey,BuildingLiftDoorIds>& ids,
                            std::string* error=nullptr){
        return scene_.attachDoors(spatial,navmesh,ids,error);
    }
    void setDoorObstructionProbe(BuildingCirculationScene::ObstructionProbe probe){
        scene_.setDoorObstructionProbe(std::move(probe));
    }
    // Native/authoritative owner-only action. Network clients MUST use
    // handleAuthenticatedCommand with a configured ownership policy.
    bool board(Entity actor,uint32_t feature,std::string* error=nullptr){
        if(!ecs_||!scene_.attached()||scene_.faulted()||
           !scene_.doors().enabled()){
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
        if(!ecs_||!scene_.doors().enabled()||
           !scene_.doorsOpenAt(feature,floor))return false;
        // Actor movement/boarding volume validation should have run in
        // the game's motor/interaction owner before invoking exit().
        return ecsBridge_.exit(*ecs_,scene_.host(),sceneId_,actor,feature,floor,
                               width_,depth_,origin_,yaw_);
    }

    // Install only on the authoritative ECS/world owner thread. The
    // callback maps an authenticated existing connection to an ECS Entity;
    // never accept client-supplied Entity ids without the mapping check.
    using OwnsActor=typename BuildingLiftAuthority<Body>::OwnsActor;
    using AtLanding=typename BuildingLiftAuthority<Body>::AtLanding;
    bool configureAuthenticatedCommands(OwnsActor owns,AtLanding landing,
                                        std::string* error=nullptr){
        if(!ecs_||!scene_.attached()||scene_.faulted()||
           !owns||!landing){
            if(error)*error="ECS lift command policy requires a live scene and both guards";
            return false;
        }
        commandAuthority_=std::make_unique<BuildingLiftAuthority<Body>>(
            *ecs_,scene_,ecsBridge_,sceneId_,width_,depth_,origin_,yaw_,
            std::move(owns),std::move(landing));
        if(error)error->clear();
        return true;
    }
    // The transport must have authenticated the client before invoking.
    // The server-authority handler checks actor ownership, replay nonce,
    // physical landing/boarding and scene ID; no packet drives state
    // directly and client-authored server snapshots are never accepted.
    BuildingLiftCommandResult handleAuthenticatedCommand(
        uint64_t clientId,const BuildingLiftCommand& cmd){
        if(!commandAuthority_)
            return {false,cmd.sequence,"Lift command authority is not configured"};
        return commandAuthority_->execute(clientId,cmd);
    }
    BuildingLiftCommandResult handleAuthenticatedPacket(
        uint64_t clientId,std::span<const uint8_t> packet){
        BuildingLiftCommand command;
        std::string error;
        if(!decodeBuildingLiftCommand(packet,command,&error))
            return {false,0,std::move(error)};
        return handleAuthenticatedCommand(clientId,command);
    }
    // Administrative owner-only reset after an actor's authenticated
    // lifecycle ends. A recycled Entity generation must never inherit a
    // prior actor's nonce counter or building passenger marker.
    void clearActorCommandSequence(Entity actor){
        if(commandAuthority_&&actor.valid())
            commandAuthority_->clearActor(actor.packed());
    }

    // Authoritative entity-lifecycle gate. Call BEFORE destroying or
    // disconnecting an ECS actor. A rider in transit must remain owned by
    // the simulation until a real landing is open; do not silently subtract
    // its cabin occupancy or leave an invalid packed Entity in host.riders_.
    // Returns false to defer actor retirement (the game may mark it pending).
    bool prepareActorRetirement(Entity actor,std::string* error=nullptr){
        auto fail=[&](const char* why){
            if(error)*error=why;
            return false;
        };
        if(!ecs_||!scene_.attached()||scene_.faulted()||
           !actor.valid()||!ecs_->alive(actor))
            return fail("Actor retirement requires a live authoritative ECS scene");
        const uint64_t key=actor.packed();
        const auto binding=scene_.host().riderBindings().find(key);
        if(binding!=scene_.host().riderBindings().end()){
            const uint32_t feature=binding->second;
            const auto it=scene_.host().lifts().find(feature);
            if(it==scene_.host().lifts().end()||!scene_.doors().enabled()||
               !scene_.doorsOpenAt(feature,it->second.snapshot().currentFloor))
                return fail("Rider is in transit: defer actor retirement until an open landing");
            const auto* marker=ecs_->tryGet<BuildingLiftPassenger>(actor);
            if(!marker||!marker->attached||marker->sceneId!=sceneId_||
               marker->liftFeature!=feature)
                return fail("ECS passenger marker and lift owner are inconsistent");
            if(!ecsBridge_.releaseAtLanding(*ecs_,scene_.host(),actor,feature))
                return fail("Authoritative lift declined rider release");
        }
        // A recycled entity generation must never retain an authenticated
        // command sequence. The existing transport still owns client logout.
        clearActorCommandSequence(actor);
        if(error)error->clear();
        return true;
    }
    // Ordinary streaming/unload must not invalidate live passenger markers.
    // Forced detach() below is reserved for authoritative world destruction
    // after the game has explicitly reconciled every passenger.
    bool canUnload()const noexcept{
        return ecs_&&scene_.attached()&&!scene_.faulted()&&
               scene_.host().riderBindings().empty();
    }
    bool detachIfVacant(world::SpatialWorld& spatial,nav::Mesh* navmesh,
                        std::string* error=nullptr){
        if(!canUnload()){
            if(error)*error="Cannot unload a faulted or occupied building scene";
            return false;
        }
        detach(spatial,navmesh);
        if(error)error->clear();
        return true;
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
        lastPublishComplete_=out.complete;
        if(!out.complete){
            out.error=!out.world.published?out.world.error:out.actors.error;
            if(out.error.empty())
                out.error="A rider actor was destroyed or moved outside its owner";
            scene_.fault(navmesh);
        }
        return out;
    }
    // Server only: capture after the ECS and collision owner have committed
    // the tick. Transport the encoded bytes through the EXISTING authorized
    // network snapshot channel; never accept this as a client command.
    bool captureServerSnapshot(uint64_t serverTick,
                               std::vector<uint8_t>& out,
                               std::string* error=nullptr)const{
        if(!ecs_||!scene_.attached()||scene_.faulted()||
           !lastPublishComplete_||sceneId_==0){
            if(error)*error="Cannot publish lift frame before owner commit";
            return false;
        }
        const auto frame=captureBuildingLiftServerFrame(
            scene_.host(),sceneId_,serverTick);
        return encodeBuildingLiftStateFrame(frame,out,error);
    }

    bool captureCheckpoint(BuildingCirculationCheckpoint& out)const{
        if(!ecs_||!scene_.attached()||scene_.faulted()||!lastPublishComplete_)
            return false;
        BuildingCirculationCheckpoint staged;
        if(!scene_.host().captureCheckpoint(staged)||
           !validatePassengers(*ecs_,sceneId_,staged))return false;
        out=std::move(staged);return true;
    }
    bool rebuildNavigation(const nav::Mesh& mesh,std::string* error=nullptr){
        return scene_.rebuildNavigation(mesh,error);
    }
    void detach(world::SpatialWorld& spatial,nav::Mesh* navmesh,
                bool clearPassengers=true){
        commandAuthority_.reset(); // drops authenticated callbacks before detaching their world
        // Clear only the caller's old ECS attachment markers. Game owner
        // remains responsible for safe despawn/relocation at unload.
        if(ecs_&&clearPassengers){
            const uint64_t oldScene=sceneId_;
            ecs_->eachExisting<BuildingLiftPassenger>(
                [oldScene](Entity,BuildingLiftPassenger& p){
                    if(p.sceneId==oldScene)p.attached=false;
                });
        }
        scene_.detach(spatial,navmesh);
        ecs_=nullptr;sceneId_=0;lastPublishComplete_=false;
    }
    BuildingCirculationScene& scene()noexcept{return scene_;}
    const BuildingCirculationScene& scene()const noexcept{return scene_;}
    uint64_t sceneId()const noexcept{return sceneId_;}
private:
    static bool validatePassengers(World& ecs,uint64_t sceneId,
                                   const BuildingCirculationCheckpoint& saved){
        std::map<uint64_t,uint32_t> found;
        bool valid=true;
        ecs.eachExisting<BuildingLiftPassenger>([&](Entity e,BuildingLiftPassenger& p){
            if(!p.attached||p.sceneId!=sceneId)return;
            const auto lift=saved.lifts.find(p.liftFeature);
            if(!ecs.alive(e)||!ecs.tryGet<Body>(e)||lift==saved.lifts.end()||
               !p.lastAppliedRevision||p.lastAppliedRevision>lift->second.state.revision||
               !found.emplace(e.packed(),p.liftFeature).second)valid=false;
        });
        return valid&&found==saved.riders;
    }
    bool doorwayOccupied(uint32_t feature,uint32_t floor){
        if(!ecs_||!scene_.attached())return true; // fail closed
        for(const auto& pose:scene_.doors().poses()){
            if(pose.key.feature!=feature||pose.key.floor!=floor)continue;
            const Vec3 lo=buildingLocalToWorld(pose.x0,0,pose.z,
                width_,depth_,origin_,yaw_);
            const Vec3 hi=buildingLocalToWorld(pose.x1,0,pose.z,
                width_,depth_,origin_,yaw_);
            const Fixed r=buildingFixedMm(420);
            const Fixed at=origin_.y+buildingFixedMm(pose.yMm);
            bool blocked=false;
            ecs_->eachExisting<Body>([&](Entity,Body& body){
                const auto& pos=body.pos;
                if(pos.x>=fxMin(lo.x,hi.x)-r&&pos.x<=fxMax(lo.x,hi.x)+r&&
                   pos.z>=fxMin(lo.z,hi.z)-r&&pos.z<=fxMax(lo.z,hi.z)+r&&
                   pos.y>=at-buildingFixedMm(250)&&
                   pos.y<=at+buildingFixedMm(2300))
                    blocked=true;
            });
            return blocked;
        }
        return true; // missing door metadata cannot be considered clear
    }
    World* ecs_=nullptr;
    BuildingCirculationScene scene_;
    BuildingLiftEcsBridge<Body> ecsBridge_;
    std::unique_ptr<BuildingLiftAuthority<Body>> commandAuthority_;
    uint64_t sceneId_=0;
    bool lastPublishComplete_=false;
    float width_=0,depth_=0;
    uint32_t yaw_=0;
    Vec3 origin_{};
};
} // namespace aiverse::building
