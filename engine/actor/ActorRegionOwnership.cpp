// Opt-in authoritative region ownership for world-space actor ECS.
// Runs after gameplay movement; never reads render-camera observers.
#include "ActorModule.h"
#include <algorithm>
#include <set>
#include <map>
#include <stdexcept>

namespace aiverse::actor {

bool ActorHost::reportRegionChange(Simulation& sim,Entity entity,
                                    world::RegionDirtyKind kind) {
    auto& ecs=sim.world();
    const auto* queue=ecs.resourceConst<world::RegionDirtyQueue>();
    if(!queue)return true;
    if(!entity.valid() || entity.packed()==UINT64_MAX)return false;
    const uint64_t id=entity.packed()+1;
    if(!queue->entries.contains(id) &&
       queue->entries.size()>=world::RegionDirtyQueue::kMaxEntries)return false;
    return ecs.resource<world::RegionDirtyQueue>()->mark(entity,kind);
}

bool ActorHost::reportRegionMoveIfCrossed(Simulation& sim,Entity entity) {
    const World& view=sim.world();
    const auto* ledger=view.resourceConst<world::RegionEntityLedger>();
    if(!ledger)return true; // no W1 opt-in
    if(!view.alive(entity))return false;
    const auto* tag=view.tryGetConst<world::RegionStableIdentity>(entity);
    if(!tag || !tag->id)return false;
    const auto* record=ledger->find(tag->id);
    if(!record || !record->live)return false;
    const auto pos=regionPositionReader()(view,entity);
    if(!pos)return false;
    if(world::SpatialWorld(32).cell(*pos)==record->owner)return true;
    return reportRegionChange(sim,entity,world::RegionDirtyKind::Move);
}

void ActorHost::synchronizeRegionOwnership(Simulation& sim) {
    auto& ecs=sim.world();
    const World& view=ecs;
    const auto* ledger=view.resourceConst<world::RegionEntityLedger>();
    if(!ledger)return;
    const auto position=regionPositionReader();
    const world::SpatialWorld grid(32);
    const auto* dirty=view.resourceConst<world::RegionDirtyQueue>();
    // Process known structural changes before the compatibility scan.
    // Position changes remain handled by the full scan until every
    // authoritative canonical-position writer emits a dirty event.
    if(dirty) {
        for(const auto& [id,flags]:dirty->entries) {
            ledger=view.resourceConst<world::RegionEntityLedger>();
            (void)flags;
            const Entity entity=Entity::fromPacked(id-1);
            const bool tracked=view.alive(entity) &&
                (view.has<CharacterBody>(entity) || view.has<Vehicle>(entity) ||
                 view.has<Loot>(entity));
            const auto* record=ledger->find(id);
            if(!tracked) {
                if(record && record->live &&
                   !ecs.resource<world::RegionEntityLedger>()->despawn(id,record->owner))
                    throw std::logic_error("regionOwnership: dirty retire failed");
                continue;
            }
            const auto location=position(view,entity);
            if(!location)
                throw std::logic_error("regionOwnership: dirty actor lacks position");
            if(!record) {
                if(!world::RegionEcsBridge::bindNew(
                       ecs,*ecs.resource<world::RegionEntityLedger>(),
                       entity,id,grid.cell(*location),position))
                    throw std::logic_error("regionOwnership: dirty spawn failed");
            }else if(!record->live) {
                throw std::logic_error("regionOwnership: tombstoned dirty ID");
            }
            // resourceConst may have been detached by a COW write.
            ledger=view.resourceConst<world::RegionEntityLedger>();
        }
    }
    ledger=view.resourceConst<world::RegionEntityLedger>();

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
        ledger=view.resourceConst<world::RegionEntityLedger>();
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
    const auto cars=vehicleEntities(sim);
    for(Entity car:cars) {
        const auto* v=view.tryGetConst<Vehicle>(car);
        if(!v || !v->id || !carIds.insert(v->id).second)
            throw std::logic_error("regionOwnership: invalid or duplicate car id");
    }
    // Index legacy backlinks once, rather than scanning every character per car.
    std::map<uint32_t,std::vector<Entity>> passengersByCar;
    view.eachExistingConst<CharacterBody>(
        [&](Entity passenger,const CharacterBody& body) {
            if(!body.vehicle)return;
            if(!carIds.contains(body.vehicle))
                throw std::logic_error("regionOwnership: orphaned vehicle backlink");
            if(view.alive(passenger))passengersByCar[body.vehicle].push_back(passenger);
        });

    // Occupied vehicles are one atomic ownership graph. Legacy arcade seats
    // use CharacterBody.vehicle + Vehicle.driver; typed vehicles use the one
    // shared VehicleDrive/VehicleOccupant model and the existing seat adapter.
    std::set<uint64_t> handled;
    for(Entity car:cars) {
        ledger=view.resourceConst<world::RegionEntityLedger>();
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
            if(const auto it=passengersByCar.find(carState->id);
               it!=passengersByCar.end())
                group.insert(group.end(),it->second.begin(),it->second.end());
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
        ledger=view.resourceConst<world::RegionEntityLedger>();
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
    // Iterate ONLY live IDs; historical tombstones may outnumber the hot
    // population by orders of magnitude in a long-running city.
    ledger=view.resourceConst<world::RegionEntityLedger>();
    const std::vector<world::RegionEntityId> live(
        ledger->liveIds().begin(),ledger->liveIds().end());
    for(const auto id:live) {
        ledger=view.resourceConst<world::RegionEntityLedger>();
        if(present.contains(id))continue;
        const auto* entry=ledger->find(id);
        if(!entry || !entry->live ||
           !ecs.resource<world::RegionEntityLedger>()->despawn(id,entry->owner))
            throw std::logic_error("regionOwnership: tombstone failed");
    }
    // ACK after successful authoritative reconciliation; failures keep
    // pending observations for retry/rollback.
    if(dirty && !dirty->entries.empty()) {
        const size_t count=dirty->entries.size();
        if(count>UINT64_MAX-dirty->acknowledged)
            throw std::logic_error("regionOwnership: dirty ACK overflow");
        auto* queue=ecs.resource<world::RegionDirtyQueue>();
        queue->acknowledged+=count;
        queue->entries.clear();
    }
}
} // namespace aiverse::actor
