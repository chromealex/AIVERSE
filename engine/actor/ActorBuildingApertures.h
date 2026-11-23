#pragma once
// FPS project host integration using the EXISTING authoritative ActorHost
// ECS characters, vehicles and SpatialWorld. This is optional per scene;
// no independent physics world or network authority is introduced.
#include "engine/actor/ActorModule.h"
#include "engine/buildings/BuildingDualLeafGameSession.h"
#include <algorithm>
#include <memory>
#include <string>
#include <utility>

namespace aiverse::actor {

class ActorBuildingApertures {
public:
    bool attach(Simulation& sim,ActorHost& host,
                uint64_t sceneId,
                const std::vector<building::BuildingAperturePose>& poses,
                const std::map<std::string,building::BuildingDualLeafIds>& ids,
                const lib::systems::BuildingStyle& style,
                std::string* error=nullptr){
        if(sceneId_||!sceneId||poses.empty()){
            if(error)*error="FPS building doors require one nonempty scene";
            return false;
        }
        sim_=&sim;host_=&host;
        const auto occupied=[this](const world::Bounds& first,
                                   const world::Bounds& second,
                                   uint32_t ownFirst,uint32_t ownSecond){
            // The moving leaf colliders are excluded by ID. Physical
            // occupancy comes from the same authoritative FPS ECS and
            // typed vehicle/loot records that the Actor motor updates.
            // The game PHYSIS/Box3D owner may add further probes separately.
            (void)ownFirst;(void)ownSecond;
            for(uint8_t slot=1;slot<=host_->characterCount(*sim_);++slot){
                auto actor=host_->character(*sim_,slot);
                if(actor.health<=0)continue;
                const auto b=actorBounds(actor,host_->config().motor);
                if(world::overlaps(first,b)||world::overlaps(second,b))
                    return true;
            }
            for(const auto& vehicle:host_->vehicles(*sim_)){
                const Vec3 half{2_fx,2_fx,3_fx};
                world::Bounds b{vehicle.pos-half,vehicle.pos+half};
                if(world::overlaps(first,b)||world::overlaps(second,b))
                    return true;
            }
            for(const auto& cargo:host_->loot(*sim_)){
                const Vec3 half{Fixed::fromRatio(3,10),1_fx,
                                Fixed::fromRatio(3,10)};
                world::Bounds b{cargo.pos-half,cargo.pos+half};
                if(world::overlaps(first,b)||world::overlaps(second,b))
                    return true;
            }
            return false;
        };
        if(!physics_.attach(poses,ids,host.buildingCollision(),sceneId,
                            occupied,error)){
            sim_=nullptr;host_=nullptr;
            return false;
        }
        // FPS Spatial access owner caches the last dirty region/revision.
        // Navigation/other graph domains must register real owners separately.
        router_.registerConsumer(building::BuildingGraphDomain::Spatial,
            [this](const building::BuildingGraphChange& change){
                lastChanged_=change.bounds;spatialRevision_=change.revision;
                return true;
            });
        auto authorize=[this](Entity actor,const std::string&,bool){
            if(!sim_||!host_||!sim_->world().alive(actor))return false;
            const auto* body=sim_->world().tryGetConst<CharacterBody>(actor);
            const auto* id=sim_->world().tryGetConst<CharacterId>(actor);
            const auto* hp=sim_->world().tryGetConst<CharacterHealth>(actor);
            return body&&id&&hp&&hp->health>0&&!hp->downed&&
                   !body->vehicle;
        };
        auto reachable=[this](Entity actor,const world::Bounds& bounds){
            const auto* body=sim_->world().tryGetConst<CharacterBody>(actor);
            if(!body)return false;
            const Vec3 margin{2_fx,2_fx,2_fx};
            return world::overlaps({body->pos-margin,body->pos+margin},
                                    bounds);
        };
        if(!session_.attach(sceneId,physics_,style,authorize,reachable,error)){
            physics_.detach(host.buildingCollision());
            router_.forgetScene(sceneId);
            sim_=nullptr;host_=nullptr;return false;
        }
        sceneId_=sceneId;
        if(error)error->clear();
        return true;
    }

    // Called as a genuine additional Simulation stage AFTER ActorHost::tick.
    // A dedicated network packet handler must authenticate the peer before
    // queuing BuildingApertureCommand on this same world-owner thread.
    building::BuildingDualLeafTickFrame tick(Simulation& sim){
        building::BuildingDualLeafTickFrame out;
        if(!sceneId_||&sim!=sim_){
            out.error="FPS dual leaf runtime is attached to another simulation";
            return out;
        }
        // The FPS transport already authenticated each actor input and
        // stored it in the ECS CharacterControl. The Interact action is
        // mapped to the closest reachable generated socket, never to a
        // client-supplied socket ID. Do not consume input twice.
        for(size_t slot=1;slot<=host_->characterCount(sim);++slot){
            const Entity entity=host_->characterEntity(sim,uint8_t(slot));
            if(!sim.world().alive(entity))continue;
            const auto* control=sim.world().tryGetConst<CharacterControl>(entity);
            const auto* body=sim.world().tryGetConst<CharacterBody>(entity);
            const auto* health=sim.world().tryGetConst<CharacterHealth>(entity);
            if(!control||!body||!health||health->health<=0||
               control->input.action!=Action::Interact||
               !control->input.sequence)continue;
            const uint64_t sequence=control->input.sequence;
            if(interactions_.contains(entity.packed())&&
               sequence<=interactions_.at(entity.packed()))continue;
            interactions_[entity.packed()]=sequence;
            std::string nearest;
            Fixed best=Fixed::fromInt(1000000);
            bool wasOpen=false;
            for(const auto& pose:physics_.poses()){
                if(pose.moving)continue;
                const Vec3 mid{(pose.bounds.min.x+pose.bounds.max.x)/2,
                    (pose.bounds.min.y+pose.bounds.max.y)/2,
                    (pose.bounds.min.z+pose.bounds.max.z)/2};
                const Fixed distance=(body->pos-mid).lengthSq();
                if(distance<best&&distance<=Fixed::fromInt(16)){
                    best=distance;nearest=pose.socketId;
                    wasOpen=pose.open;
                }
            }
            if(nearest.empty())continue;
            building::BuildingApertureCommand command;
            command.sceneId=sceneId_;
            command.actor=entity;
            command.sequence=sequence;
            command.socketId=std::move(nearest);
            command.open=!wasOpen;
            command.graphDomains=building::buildingGraphBit(
                building::BuildingGraphDomain::Spatial);
            (void)session_.enqueue(sim.world(),command);
        }
        out=session_.fixedTick(sim.world(),physics_,
             host_->buildingCollision(),router_,sim.tick(),100);
        return out;
    }
    bool enqueueAuthenticated(uint64_t authenticatedClient,
                              Entity ownedActor,
                              const building::BuildingApertureCommand& cmd,
                              std::string* error=nullptr){
        // The FPS transport maps the authenticated client to a slot/Entity.
        // No client-supplied actor ID is accepted without this equality.
        if(!authenticatedClient||!ownedActor.valid()||
           cmd.actor!=ownedActor||!sim_||!sim_->world().alive(ownedActor)){
            if(error)*error="FPS authenticated actor mismatch";
            return false;
        }
        return session_.enqueue(sim_->world(),cmd,error);
    }
    bool registerGraphConsumer(building::BuildingGraphDomain domain,
                               building::BuildingGraphChangeRouter::Consumer c){
        return router_.registerConsumer(domain,std::move(c));
    }
    bool bindMaterial(const std::string& socket,
                      const lib::systems::StyleMaterial& material,
                      const building::BuildingImageByGuid& resolve,
                      std::string* error=nullptr){
        return session_.bindMaterial(socket,material,resolve,error);
    }
    template<class Callback>
    bool forEachCpuModel(Callback&& callback){
        return session_.forEachCpuModel(std::forward<Callback>(callback));
    }
    bool submit(uint32_t ms,const building::BuildingDualLeafGameSession::SubmitLeaf& submit,
                std::string* error=nullptr){
        return session_.submit(ms,submit,error);
    }
    void retireActor(Entity actor){session_.retireActor(actor);interactions_.erase(actor.packed());}
    uint64_t sceneId()const{return sceneId_;}
    uint64_t revision()const{return physics_.revision();}
    uint64_t spatialRevision()const{return spatialRevision_;}
    world::Bounds lastChangedBounds()const{return lastChanged_;}
    const building::BuildingDualLeafOwner& physics()const{return physics_;}
    void detach(){
        if(!sceneId_)return;
        // Caller must ensure GPU resources have retired first.
        session_.detach();physics_.detach(host_->buildingCollision());
        router_.forgetScene(sceneId_);
        sceneId_=0;spatialRevision_=0;sim_=nullptr;host_=nullptr;
        interactions_.clear();
    }
private:
    Simulation* sim_=nullptr;
    ActorHost* host_=nullptr;
    building::BuildingDualLeafOwner physics_;
    building::BuildingDualLeafGameSession session_;
    building::BuildingGraphChangeRouter router_;
    uint64_t sceneId_=0,spatialRevision_=0;
    std::map<uint64_t,uint64_t> interactions_;
    world::Bounds lastChanged_{};
};

} // namespace aiverse::actor
