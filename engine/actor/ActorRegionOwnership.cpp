// Opt-in authoritative region ownership for world-space actor ECS.
// Runs after gameplay movement; never reads render-camera observers.
#include "ActorModule.h"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace aiverse::actor {

void ActorHost::synchronizeRegionOwnership(Simulation& sim) {
    auto& ecs=sim.world();
    const World& view=ecs;
    const auto* ledger=view.resourceConst<world::RegionEntityLedger>();
    if(!ledger)return;
    const auto position=regionPositionReader();
    const world::SpatialWorld grid(32);

    // Snapshot before adding ECS tags. Stable order ignores sparse pool order.
    std::vector<Entity> tracked;
    view.eachExistingConst<CharacterBody>([&](Entity e,const CharacterBody&) {
        if(view.alive(e))tracked.push_back(e);
    });
    view.eachExistingConst<Vehicle>([&](Entity e,const Vehicle&) {
        if(view.alive(e))tracked.push_back(e);
    });
    view.eachExistingConst<Loot>([&](Entity e,const Loot&) {
        if(view.alive(e))tracked.push_back(e);
    });
    std::sort(tracked.begin(),tracked.end(),[](Entity a,Entity b) {
        return a.packed()<b.packed();
    });
    tracked.erase(std::unique(tracked.begin(),tracked.end()),tracked.end());

    std::set<world::RegionEntityId> present;
    for(Entity entity:tracked) {
        if(entity.packed()==UINT64_MAX)
            throw std::logic_error("regionOwnership: stable ID exhausted");
        const auto stable=entity.packed()+1;
        const auto pos=position(view,entity);
        if(!pos)throw std::logic_error("regionOwnership: missing actor position");
        const auto* tag=view.tryGetConst<world::RegionStableIdentity>(entity);
        if(!tag) {
            auto& destination=*ecs.resource<world::RegionEntityLedger>();
            if(!world::RegionEcsBridge::bindNew(
                    ecs,destination,entity,stable,grid.cell(*pos),position))
                throw std::logic_error("regionOwnership: cannot bind new actor");
        }else if(tag->id!=stable) {
            throw std::logic_error("regionOwnership: changed stable actor ID");
        }else{
            const auto* entry=ledger->find(stable);
            if(!entry || !entry->live)
                throw std::logic_error("regionOwnership: invalid live actor ID");
        }
        present.insert(stable);
    }

    // Reject orphaned car backlinks before migrating anyone. World-space
    // character seat references use Vehicle.id, not Entity::index.
    std::set<uint32_t> carIds;
    for(Entity car:vehicleEntities(sim)) {
        const auto* v=view.tryGetConst<Vehicle>(car);
        if(!v || !v->id || !carIds.insert(v->id).second)
            throw std::logic_error("regionOwnership: invalid or duplicate car id");
    }
    view.eachExistingConst<CharacterBody>(
        [&](Entity,const CharacterBody& body) {
            if(body.vehicle && !carIds.contains(body.vehicle))
                throw std::logic_error("regionOwnership: orphaned vehicle backlink");
        });

    // Occupied vehicles are one atomic ownership graph. Legacy arcade seats
    // use CharacterBody.vehicle + Vehicle.driver; typed vehicles use the one
    // shared VehicleDrive/VehicleOccupant model and the existing seat adapter.
    std::set<uint64_t> handled;
    for(Entity car:vehicleEntities(sim)) {
        const auto* carState=view.tryGetConst<Vehicle>(car);
        if(!carState)continue;
        std::vector<Entity> group;
        if(view.has<vehicles::VehicleDrive>(car)) {
            world::VehicleRegionGroup gathered;
            std::string error;
            if(!vehicleRegionGroup(sim,car,gathered,&error))
                throw std::logic_error("regionOwnership: invalid car seats: "+error);
            group=std::move(gathered.members);
        }else{
            group.push_back(car);
            view.eachExistingConst<CharacterBody>(
                [&](Entity passenger,const CharacterBody& body) {
                    if(view.alive(passenger) && carState->id &&
                       body.vehicle==carState->id)
                        group.push_back(passenger);
                });
            if(carState->driver) {
                if(carState->driver>characterCount(sim))
                    throw std::logic_error("regionOwnership: bad driver slot");
                const Entity driver=characterEntity(sim,carState->driver);
                if(!view.alive(driver))
                    throw std::logic_error("regionOwnership: missing driver");
                group.push_back(driver);
            }
            std::sort(group.begin(),group.end(),[](Entity a,Entity b) {
                return a.packed()<b.packed();
            });
            group.erase(std::unique(group.begin(),group.end()),group.end());
        }

        const auto destination=grid.cell(carState->pos);
        bool changed=false;
        for(Entity member:group) {
            const auto* tag=view.tryGetConst<world::RegionStableIdentity>(member);
            if(!tag || !tag->id || !present.contains(tag->id))
                throw std::logic_error("regionOwnership: vehicle group missing stable actor");
            const auto* entry=ledger->find(tag->id);
            if(!entry || !entry->live)
                throw std::logic_error("regionOwnership: missing vehicle group owner");
            if(entry->owner!=destination)changed=true;
            if(!handled.insert(member.packed()).second)
                throw std::logic_error("regionOwnership: duplicate car occupant");
        }
        if(!changed)continue;
        if(streamManifest_.getBool("async")) {
            const world::Bounds required{
                carState->pos-Vec3{2_fx,2_fx,2_fx},
                carState->pos+Vec3{2_fx,2_fx,2_fx}};
            if(!streamedCollision_ || !streamedCollision_->covers(required))
                throw std::logic_error("regionOwnership: car destination collision unready");
        }
        std::string error;
        if(!world::RegionEcsBridge::transferGroupMixed(
                view,*ecs.resource<world::RegionEntityLedger>(),group,
                destination,position,&error))
            throw std::logic_error("regionOwnership: car transfer: "+error);
    }

    // On-foot characters and loose durable objects move independently.
    for(Entity entity:tracked) {
        if(handled.contains(entity.packed()))continue;
        const auto* tag=view.tryGetConst<world::RegionStableIdentity>(entity);
        const auto* entry=ledger->find(tag->id);
        if(!entry || !entry->live)
            throw std::logic_error("regionOwnership: missing solo owner");
        const auto destination=grid.cell(*position(view,entity));
        if(destination==entry->owner)continue;
        if(view.has<world::RegionDormant>(entity))
            throw std::logic_error("regionOwnership: dormant actor crossing");
        if(!ecs.resource<world::RegionEntityLedger>()->transfer(
                tag->id,entry->owner,destination))
            throw std::logic_error("regionOwnership: solo transfer failed");
    }

    // Destroyed entities retain tombstones: recycled ECS indices have
    // distinct generation bits, so stable IDs cannot be reused.
    for(const auto& [id,entry]:ledger->records()) {
        if(!entry.live || present.contains(id))continue;
        if(!ecs.resource<world::RegionEntityLedger>()->despawn(id,entry.owner))
            throw std::logic_error("regionOwnership: tombstone failed");
    }
}
} // namespace aiverse::actor
