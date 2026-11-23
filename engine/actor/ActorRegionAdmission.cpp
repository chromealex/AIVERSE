// W1.9: pre-tick destination admission for authoritative ActorHost moves.
// Avoids running a tick with a pending teleport into unloaded collision.
#include "ActorModule.h"
#include <algorithm>
#include <set>
#include <stdexcept>
#include <utility>

namespace aiverse::actor {
namespace {
bool reject(std::string* error,const char* message) {
    if(error)*error=message;
    return false;
}
bool moveGroup(const ActorHost& host,const Simulation& sim,Entity target,
               std::vector<Entity>& out,std::string* error) {
    const World& world=sim.world();
    if(!world.alive(target))return reject(error,"region move entity is dead");
    const bool car=world.has<Vehicle>(target);
    const bool character=world.has<CharacterBody>(target);
    if(!car && !character)return reject(error,"region move requires a live character or car");
    if(world.has<world::RegionDormant>(target))
        return reject(error,"cannot move a dormant region entity");
    if(car && world.has<vehicles::VehicleDrive>(target))
        return reject(error,"typed vehicle requires authoritative controller teleport integration");
    std::vector<Entity> members{target};
    if(car) {
        const auto* vehicle=world.tryGetConst<Vehicle>(target);
        if(!vehicle || !vehicle->id)return reject(error,"invalid vehicle identity");
        if(vehicle->driver) {
            if(vehicle->driver>host.characterCount(sim))
                return reject(error,"invalid vehicle driver slot");
            members.push_back(host.characterEntity(sim,vehicle->driver));
        }
        world.eachExistingConst<CharacterBody>([&](Entity e,const CharacterBody& body){
            if(body.vehicle==vehicle->id)members.push_back(e);
        });
    }else{
        const auto* body=world.tryGetConst<CharacterBody>(target);
        if(body && body->vehicle)
            return reject(error,"occupied character must move with its vehicle");
    }
    std::sort(members.begin(),members.end(),[](Entity a,Entity b){
        return a.packed()<b.packed();
    });
    members.erase(std::unique(members.begin(),members.end()),members.end());
    for(Entity e:members) {
        if(!world.alive(e) || world.has<world::RegionDormant>(e))
            return reject(error,"group contains absent or dormant actor");
        if(!world.tryGetConst<world::RegionStableIdentity>(e))
            return reject(error,"group member missing persistent region identity");
        if(e!=target && !world.has<CharacterBody>(e))
            return reject(error,"vehicle group contains an invalid occupant");
    }
    out=std::move(members);
    return true;
}
} // namespace

bool ActorHost::requestRegionMove(Simulation& sim,Entity entity,Vec3 destination,
                                  std::string* error) {
    auto& ecs=sim.world();
    const auto* queued=ecs.resourceConst<world::RegionMoveRequests>();
    const auto* ledger=ecs.resourceConst<world::RegionEntityLedger>();
    if(!queued || !ledger)
        return reject(error,"region ownership is not enabled");
    if(fxAbs(destination.x)>config_.worldLimit ||
       fxAbs(destination.y)>config_.worldLimit ||
       fxAbs(destination.z)>config_.worldLimit)
        return reject(error,"destination exceeds world limits");
    if(!world::SpatialWorld(32).cells({
          destination-Vec3{2_fx,2_fx,2_fx},
          destination+Vec3{2_fx,2_fx,2_fx}}).size())
        return reject(error,"invalid destination region bounds");
    std::vector<Entity> group;
    if(!moveGroup(*this,sim,entity,group,error))return false;
    for(Entity member:group) {
        const auto* tag=ecs.tryGetConst<world::RegionStableIdentity>(member);
        if(!tag || !ledger->find(tag->id) || !ledger->find(tag->id)->live)
            return reject(error,"unregistered region move member");
    }
    if(entity.packed()==UINT64_MAX)return reject(error,"region move identity exhausted");
    // +1 allows the first ECS Entity (packed handle 0) to be encoded.
    const uint64_t id=entity.packed()+1;
    const auto it=queued->destinations.find(id);
    if(it==queued->destinations.end() &&
       queued->destinations.size()>=world::RegionMoveRequests::kMaxRequests)
        return reject(error,"too many pending region moves");
    if(it!=queued->destinations.end() &&
       it->second.x==destination.x && it->second.y==destination.y &&
       it->second.z==destination.z)return true;
    // Reject overlapping requests for the same occupant graph. A later
    // command may replace the same entity request (coalesced).
    for(const auto& [otherId,point]:queued->destinations) {
        (void)point;
        if(otherId==id)continue;
        std::vector<Entity> others;
        if(!moveGroup(*this,sim,Entity::fromPacked(otherId-1),others,error))
            return false;
        for(Entity e:group)
            if(std::find(others.begin(),others.end(),e)!=others.end())
                return reject(error,"overlapping pending vehicle/occupant moves");
    }
    ecs.resource<world::RegionMoveRequests>()->destinations[id]=destination;
    return true;
}

void ActorHost::applyRegionMoves(Simulation& sim) {
    auto& ecs=sim.world();
    const auto* requests=ecs.resourceConst<world::RegionMoveRequests>();
    if(!requests || requests->destinations.empty())return;
    const auto position=regionPositionReader();
    struct Commit {
        Entity entity;
        Vec3 delta;
        std::vector<Entity> members;
    };
    std::vector<Commit> changes;
    std::set<uint64_t> checked;
    for(const auto& [id,destination]:requests->destinations) {
        Entity e=Entity::fromPacked(id-1);
        std::vector<Entity> members;
        std::string error;
        if(!moveGroup(*this,sim,e,members,&error))
            throw std::logic_error("region move preflight invalidated: "+error);
        const auto old=position(ecs,e);
        if(!old)throw std::logic_error("region move missing canonical position");
        const Vec3 delta=destination-*old;
        for(Entity member:members)
            if(!checked.insert(member.packed()).second)
                throw std::logic_error("region move overlapping groups");
        if(streamManifest_.getBool("async")) {
            const world::Bounds required{
                destination-Vec3{4_fx,4_fx,4_fx},
                destination+Vec3{4_fx,4_fx,4_fx}};
            if(!streamedCollision_ || !streamedCollision_->covers(required))
                throw std::logic_error("region move destination not admitted before tick");
        }
        changes.push_back({e,delta,std::move(members)});
    }
    if(requests->applied>UINT64_MAX-changes.size())
        throw std::logic_error("region move counter exhausted");

    // All targets are validated before changing a position. No command, RNG
    // or authoritative actor component is touched by the admission gate.
    for(const auto& change:changes) {
        for(Entity member:change.members) {
            if(auto* body=ecs.tryGet<CharacterBody>(member))
                body->pos+=change.delta;
            else if(auto* vehicle=ecs.tryGet<Vehicle>(member))
                vehicle->pos+=change.delta;
        }
    }
    auto* mutableQueue=ecs.resource<world::RegionMoveRequests>();
    mutableQueue->applied+=changes.size();
    mutableQueue->destinations.clear();
}
} // namespace aiverse::actor
