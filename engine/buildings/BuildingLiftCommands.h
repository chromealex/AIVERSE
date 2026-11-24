#pragma once
// Authoritative lift interaction endpoint. This is a handler for commands
// ALREADY authenticated/decrypted by the game's existing network transport,
// not a new network receiver. Never accept a client-authored state snapshot.
// Every command validates owner identity, real ECS actor and fresh nonce.
// Spatial boarding is checked by BuildingLiftEcsBridge, while landing
// approach/exit are confirmed by authoritative game movement callbacks.
#include "engine/buildings/BuildingLiftEcsBridge.h"
#include <cstdint>
#include <functional>
#include <map>
#include <string>

namespace aiverse::building {
enum class BuildingLiftCommandKind : uint8_t {
    Call, Board, SelectFloor, Exit
};
struct BuildingLiftCommand {
    uint64_t sceneId=0;
    uint64_t actorId=0; // existing ECS Entity::packed()
    uint64_t sequence=0; // strictly increasing per-actor request nonce
    uint32_t liftFeature=0;
    uint32_t floor=0;
    BuildingLiftCommandKind kind=BuildingLiftCommandKind::Call;
};
struct BuildingLiftCommandResult {
    bool accepted=false;
    uint64_t sequence=0;
    std::string reason;
};

template<BuildingPositionBody Body>
class BuildingLiftAuthority {
public:
    using OwnsActor=std::function<bool(uint64_t,Entity)>;
    using AtLanding=std::function<bool(Entity,uint32_t,uint32_t)>;
    BuildingLiftAuthority(World& ecs,BuildingCirculationScene& scene,
                          BuildingLiftEcsBridge<Body>& actorBridge,
                          uint64_t sceneId,float width,float depth,
                          Vec3 origin,uint32_t yaw,
                          OwnsActor ownsActor,AtLanding atLanding)
        :ecs_(ecs),scene_(scene),actorBridge_(actorBridge),
         sceneId_(sceneId),width_(width),depth_(depth),
         origin_(origin),yaw_(yaw),
         ownsActor_(std::move(ownsActor)),
         atLanding_(std::move(atLanding)){}

    BuildingLiftCommandResult execute(uint64_t authenticatedClient,
                                      const BuildingLiftCommand& cmd){
        auto fail=[&](const char* reason){
            return BuildingLiftCommandResult{false,cmd.sequence,reason};
        };
        if(!sceneId_||!authenticatedClient||!cmd.actorId||!cmd.sequence||
           !ownsActor_||!atLanding_||!scene_.attached()||scene_.faulted()||
           !scene_.doors().enabled()||
           cmd.sceneId!=sceneId_||yaw_>3||width_<=0||depth_<=0)
            return fail("Unavailable scene or unauthenticated actor command");
        const Entity actor=Entity::fromPacked(cmd.actorId);
        if(!ecs_.alive(actor)||!ecs_.has<Body>(actor)||
           !ownsActor_(authenticatedClient,actor))
            return fail("Actor is not owned by authenticated client");
        const auto previous=acceptedSequence_.find(cmd.actorId);
        if(previous!=acceptedSequence_.end()&&cmd.sequence<=previous->second)
            return fail("Duplicate or reordered actor command");
        const auto it=scene_.host().lifts().find(cmd.liftFeature);
        if(it==scene_.host().lifts().end()||
           cmd.floor>=it->second.floorCount())
            return fail("Unknown lift feature or floor");
        if(!it->second.online())return fail("Lift unavailable");
        bool accepted=false;
        switch(cmd.kind){
          case BuildingLiftCommandKind::Call:
            if(!atLanding_(actor,cmd.liftFeature,cmd.floor))
                return fail("Actor has not reached the landing call zone");
            accepted=scene_.host().callLift(cmd.liftFeature,cmd.floor);
            break;
          case BuildingLiftCommandKind::Board:
            if(!scene_.doorsOpenAt(cmd.liftFeature,cmd.floor))
                return fail("Landing doors not safely open");
            accepted=actorBridge_.board(ecs_,scene_.host(),sceneId_,
                actor,cmd.liftFeature,width_,depth_,origin_,yaw_);
            break;
          case BuildingLiftCommandKind::SelectFloor:
            if(!scene_.host().isRider(cmd.actorId,cmd.liftFeature))
                return fail("Actor is not in selected cabin");
            accepted=scene_.host().requestLiftDestination(cmd.liftFeature,
                                                           cmd.floor);
            break;
          case BuildingLiftCommandKind::Exit:
            if(!scene_.doorsOpenAt(cmd.liftFeature,cmd.floor)||
               !atLanding_(actor,cmd.liftFeature,cmd.floor))
                return fail("Actor has not physically reached the exit zone");
            accepted=actorBridge_.exit(ecs_,scene_.host(),sceneId_,
                actor,cmd.liftFeature,cmd.floor,
                width_,depth_,origin_,yaw_);
            break;
        }
        if(!accepted)return fail("Lift controller rejected action");
        acceptedSequence_[cmd.actorId]=cmd.sequence;
        return {true,cmd.sequence,{}};
    }
    const std::map<uint64_t,uint64_t>& acceptedSequences()const noexcept{return acceptedSequence_;}
    bool restoreSequences(const std::map<uint64_t,uint64_t>& saved){
        if(saved.size()>65536)return false;
        for(const auto& [id,seq]:saved)if(!seq||!Entity::fromPacked(id).valid())return false;
        acceptedSequence_=saved;return true;
    }
    void clearActor(uint64_t actorId){
        acceptedSequence_.erase(actorId);
    }
    void clear(){acceptedSequence_.clear();}
private:
    World& ecs_;
    BuildingCirculationScene& scene_;
    BuildingLiftEcsBridge<Body>& actorBridge_;
    uint64_t sceneId_=0;
    float width_=0,depth_=0;
    Vec3 origin_{};
    uint32_t yaw_=0;
    OwnsActor ownsActor_;
    AtLanding atLanding_;
    std::map<uint64_t,uint64_t> acceptedSequence_;
};
} // namespace aiverse::building
