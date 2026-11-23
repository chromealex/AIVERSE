// Opt-in authoritative region ownership for world-space actor ECS.
// Runs after gameplay movement; never reads render-camera observers.
#include "ActorModule.h"
#include <algorithm>
#include <set>
#include <map>
#include <stdexcept>

namespace aiverse::actor {
namespace {
// Derived World resource: follows rollback snapshots, is absent from native
// disk schemas, and is rebuilt by the compatibility pass after disk load.
struct RegionSeatIndex {
    std::map<uint64_t,Entity> carByMember;
    std::map<uint64_t,std::set<uint64_t>> membersByCar;
    void eraseCar(uint64_t car) {
        auto it=membersByCar.find(car);
        if(it==membersByCar.end())return;
        for(auto member:it->second)carByMember.erase(member);
        membersByCar.erase(it);
    }
    void eraseMember(uint64_t member) {
        auto it=carByMember.find(member);
        if(it==carByMember.end())return;
        auto group=membersByCar.find(it->second.packed());
        if(group!=membersByCar.end()) {
            group->second.erase(member);
            if(group->second.empty())membersByCar.erase(group);
        }
        carByMember.erase(it);
    }
    void setMember(uint64_t member,Entity car) {
        eraseMember(member);
        carByMember.emplace(member,car);
        membersByCar[car.packed()].insert(member);
    }
};
}

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

void ActorHost::invalidateRegionOwnershipCaches(Simulation& sim) {
    auto& ecs=sim.world();
    ecs.eraseResource<RegionSeatIndex>();
    ecs.resetComponentChanges<CharacterBody>();
    ecs.resetComponentChanges<Vehicle>();
    ecs.resetComponentChanges<Loot>();
}

void ActorHost::synchronizeRegionOwnership(Simulation& sim) {
    auto& ecs=sim.world();
    const World& view=ecs;
    const auto* ledger=view.resourceConst<world::RegionEntityLedger>();
    if(!ledger)return;
    // Enable lazily, including after native restore. The compatibility scan
    // below bootstraps ownership even when no journal history exists.
    const bool journalReady=view.componentChangesTracked<CharacterBody>() &&
        view.componentChangesTracked<Vehicle>() && view.componentChangesTracked<Loot>();
    ecs.trackComponentChanges<CharacterBody>();
    ecs.trackComponentChanges<Vehicle>();
    ecs.trackComponentChanges<Loot>();
    const auto position=regionPositionReader();
    const world::SpatialWorld grid(32);
    const auto* dirty=view.resourceConst<world::RegionDirtyQueue>();
    const auto bodyChanges=view.peekComponentChanges<CharacterBody>();
    const auto vehicleChanges=view.peekComponentChanges<Vehicle>();
    const auto lootChanges=view.peekComponentChanges<Loot>();
    const bool journalOverflow=bodyChanges.overflow || vehicleChanges.overflow || lootChanges.overflow;
    size_t uncoveredCrossings=0, uncoveredGroups=0, uncoveredLifecycle=0;
    auto notified=[&](Entity e) {
        return bodyChanges.entities.contains(e.packed()) ||
               vehicleChanges.entities.contains(e.packed()) ||
               lootChanges.entities.contains(e.packed()) ||
               (dirty && dirty->marked(e));
    };
    // Exclusive simulation-thread transaction; only changed records are saved.
    auto* target=ecs.resource<world::RegionEntityLedger>();
    world::RegionEntityLedger::Transaction stagedLedger(*target);
    ledger=target;
    struct NewIdentityRollback {
        World& ecs;
        std::vector<Entity> entities;
        bool committed=false;
        ~NewIdentityRollback() {
            if(!committed)for(auto it=entities.rbegin();it!=entities.rend();++it)
                ecs.remove<world::RegionStableIdentity>(*it);
        }
    } rollback{ecs};
    auto bind=[&](Entity e,uint64_t id,world::Cell owner) {
        if(view.has<world::RegionStableIdentity>(e))return false;
        rollback.entities.push_back(e);
        if(world::RegionEcsBridge::bindNew(ecs,stagedLedger,e,id,owner,position))return true;
        rollback.entities.pop_back();
        return false;
    };
    // Keep transfers in canonical order after group validation, even for
    // explicitly notified loot.
    // Process known structural changes before the compatibility scan.
    // Position changes remain handled by the full scan until every
    // authoritative canonical-position writer emits a dirty event.
    if(dirty) {
        for(const auto& [id,flags]:dirty->entries) {
            ledger=target;
            (void)flags;
            const Entity entity=Entity::fromPacked(id-1);
            const bool tracked=view.alive(entity) &&
                (view.has<CharacterBody>(entity) || view.has<Vehicle>(entity) ||
                 view.has<Loot>(entity));
            const auto* record=ledger->find(id);
            if(!tracked) {
                if(record && record->live &&
                   !stagedLedger.despawn(id,record->owner))
                    throw std::logic_error("regionOwnership: dirty retire failed");
                continue;
            }
            const auto location=position(view,entity);
            if(!location)
                throw std::logic_error("regionOwnership: dirty actor lacks position");
            if(!record) {
                if(!bind(entity,id,grid.cell(*location)))
                    throw std::logic_error("regionOwnership: dirty spawn failed");
            }else if(!record->live) {
                throw std::logic_error("regionOwnership: tombstoned dirty ID");
            }

        }
    }
    ledger=target;

    // Explicit opt-in contract: all canonical writes must use tracked World APIs
    // or report dirty events. Never enable for vehicle graphs yet.
    const bool soloCharacters=streamManifest_.getBool("regionDirtyOnlySolo");
    const bool lootOnly=journalReady && !journalOverflow && dirty &&
        (streamManifest_.getBool("regionDirtyOnlyLoot") || soloCharacters) &&
        !streamManifest_.getBool("strictRegionJournalAudit") &&
        (soloCharacters || (view.countExisting<CharacterBody>()==0 && bodyChanges.entities.empty())) &&
        view.countExisting<Vehicle>()==0 && vehicleChanges.entities.empty() &&
        view.resourceConst<RegionSeatIndex>() &&
        view.resourceConst<RegionSeatIndex>()->carByMember.empty();
    // Snapshot before adding ECS tags. Stable order ignores sparse pool order.
    std::vector<Entity> tracked;
    if(lootOnly) {
        std::set<uint64_t> candidates=lootChanges.entities;
        if(soloCharacters)candidates.insert(bodyChanges.entities.begin(),bodyChanges.entities.end());
        for(const auto& [id,flags]:dirty->entries) { (void)flags; candidates.insert(id-1); }
        for(auto packed:candidates) {
            const auto e=Entity::fromPacked(packed);
            if(view.alive(e) && (view.has<Loot>(e) || (soloCharacters && view.has<CharacterBody>(e))))tracked.push_back(e);
            else if(const auto* record=ledger->find(packed+1);record && record->live) {
                if(!stagedLedger.despawn(packed+1,record->owner))
                    throw std::logic_error("regionOwnership: candidate retire failed");
            }
        }
    } else {
    view.eachExistingConst<CharacterBody>([&](Entity e,const CharacterBody&) {
        if(view.alive(e))tracked.push_back(e);
    });
    view.eachExistingConst<Vehicle>([&](Entity e,const Vehicle&) {
        if(view.alive(e))tracked.push_back(e);
    });
    view.eachExistingConst<Loot>([&](Entity e,const Loot&) {
        if(view.alive(e))tracked.push_back(e);
    });
    }
    std::sort(tracked.begin(),tracked.end(),[](Entity a,Entity b) {
        return a.packed()<b.packed();
    });
    tracked.erase(std::unique(tracked.begin(),tracked.end()),tracked.end());

    std::set<world::RegionEntityId> present;
    for(Entity entity:tracked) {
        ledger=target;
        if(entity.packed()==UINT64_MAX)
            throw std::logic_error("regionOwnership: stable ID exhausted");
        const auto stable=entity.packed()+1;
        const auto pos=position(view,entity);
        if(!pos)throw std::logic_error("regionOwnership: missing actor position");
        const auto* tag=view.tryGetConst<world::RegionStableIdentity>(entity);
        if(!tag) {
            if(!notified(entity))++uncoveredLifecycle;
            if(!bind(entity,stable,grid.cell(*pos)))
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
    struct PendingGroupTransfer {
        std::vector<Entity> members;
        decltype(grid.cell(Vec3{})) destination;
    };
    std::vector<PendingGroupTransfer> pendingGroups;
    // Derived reverse membership index for this reconciliation. Never persist
    // it or reuse it across restore without rebuilding from canonical seats.
    std::map<uint64_t,Entity> carByMember;
    std::set<uint64_t> candidateCars;
    std::set<uint64_t> crossingCars;
    const auto* previousSeats=view.resourceConst<RegionSeatIndex>();
    std::set<uint64_t> handled;
    for(Entity car:cars) {
        ledger=target;
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
            carByMember.emplace(member.packed(),car);
            if(notified(member))candidateCars.insert(car.packed());
        }
        if(!changed)continue;
        crossingCars.insert(car.packed());
        if(streamManifest_.getBool("async")) {
            const world::Bounds required{
                carState->pos-Vec3{2_fx,2_fx,2_fx},
                carState->pos+Vec3{2_fx,2_fx,2_fx}};
            if(!streamedCollision_ || !streamedCollision_->covers(required))
                throw std::logic_error("regionOwnership: car destination collision unready");
        }
        pendingGroups.push_back({std::move(group),destination});
    }
    // Resolve notified handles through the canonical reverse index. This is
    // a candidate set, not a completeness proof: retain the full scan.
    auto expandCandidates=[&](const auto& entries) {
        for(auto packed:entries) {
            const auto it=carByMember.find(packed);
            if(it!=carByMember.end())candidateCars.insert(it->second.packed());
        }
    };
    expandCandidates(bodyChanges.entities);
    expandCandidates(vehicleChanges.entities);
    expandCandidates(lootChanges.entities);
    // A changed backlink can affect both the former and current vehicle.
    // Compare canonical membership before publishing the next derived index.
    if(previousSeats) {
        for(const auto& [member,oldCar]:previousSeats->carByMember) {
            const auto it=carByMember.find(member);
            if(it==carByMember.end() || !(it->second==oldCar)) {
                candidateCars.insert(oldCar.packed());
                if(it!=carByMember.end())candidateCars.insert(it->second.packed());
            }
        }
        for(const auto& [member,newCar]:carByMember)
            if(!previousSeats->carByMember.contains(member))
                candidateCars.insert(newCar.packed());
    }
    // Experimental vehicle candidate mode retains canonical seat validation.
    // A missed crossing falls back to the complete migration list rather than
    // silently losing ownership. No traversal reduction is claimed yet.
    const bool vehicleCandidateMode=journalReady && !journalOverflow &&
        streamManifest_.getBool("regionDirtyOnlyVehicles") &&
        !streamManifest_.getBool("strictRegionJournalAudit") && previousSeats;
    const bool completeGroupCoverage=std::all_of(crossingCars.begin(),crossingCars.end(),
        [&](uint64_t car){return candidateCars.contains(car);});
    // Evaluate coverage after expanding both old and new seat membership.
    for(auto car:crossingCars)
        if(!candidateCars.contains(car))++uncoveredGroups;
    // Apply migrations under the exclusive undo transaction.
    // Validate all seat graphs and collision admission before any group moves.
    for(const auto& pending:pendingGroups) {
        if(vehicleCandidateMode && completeGroupCoverage &&
           std::none_of(pending.members.begin(),pending.members.end(),[&](Entity member){
               const auto it=carByMember.find(member.packed());
               return it!=carByMember.end() && candidateCars.contains(it->second.packed());
           }))continue;
        std::string error;
        if(!world::RegionEcsBridge::transferGroupMixed(
                view,stagedLedger,pending.members,
                pending.destination,position,&error))
            throw std::logic_error("regionOwnership: car transfer: "+error);
    }

    // On-foot characters and loose durable objects move independently.
    // Audit candidate coverage without changing authoritative revision order.
    for(Entity entity:tracked) {
        if(handled.contains(entity.packed()))continue;
        const auto* tag=view.tryGetConst<world::RegionStableIdentity>(entity);
        const auto* entry=stagedLedger.find(tag->id);
        if(!entry || !entry->live)
            throw std::logic_error("regionOwnership: missing solo owner");
        const auto destination=grid.cell(*position(view,entity));
        if(destination==entry->owner)continue;
        if(!journalOverflow && !notified(entity))++uncoveredCrossings;
        if(view.has<world::RegionDormant>(entity))
            throw std::logic_error("regionOwnership: dormant actor crossing");
        if(!stagedLedger.transfer(tag->id,entry->owner,destination))
            throw std::logic_error("regionOwnership: solo transfer failed");
    }


    // Destroyed entities retain tombstones: recycled ECS indices have
    // distinct generation bits, so stable IDs cannot be reused.
    // Iterate ONLY live IDs; historical tombstones may outnumber the hot
    // population by orders of magnitude in a long-running city.
    ledger=target;
    const auto live=lootOnly?std::vector<world::RegionEntityId>{}:
        std::vector<world::RegionEntityId>(ledger->liveIds().begin(),ledger->liveIds().end());
    for(const auto id:live) {
        ledger=target;
        if(present.contains(id))continue;
        if(!notified(Entity::fromPacked(id-1)))++uncoveredLifecycle;
        const auto* entry=ledger->find(id);
        if(!entry || !entry->live ||
           !stagedLedger.despawn(id,entry->owner))
            throw std::logic_error("regionOwnership: tombstone failed");
    }
    // Optional strict audit is a development gate, never a default fast path.
    if(journalReady && !journalOverflow && streamManifest_.getBool("strictRegionJournalAudit")) {
        if(uncoveredCrossings)throw std::logic_error("regionOwnership: unreported solo crossing");
        if(uncoveredGroups)throw std::logic_error("regionOwnership: unreported group crossing");
        if(uncoveredLifecycle)throw std::logic_error("regionOwnership: unreported lifecycle change");
    }
    // The full reconciliation covered all canonical writers, including raw
    // pools and overflow. Drain only after successful reconciliation.
    // Do not convert conservative mutable accesses into persisted dirty flags:
    // that would change checksums based on non-authoritative access patterns.
    // ACK after successful authoritative reconciliation; failures keep
    // pending observations for retry/rollback.
    if(dirty && !dirty->entries.empty()) {
        const size_t count=dirty->entries.size();
        if(count>UINT64_MAX-dirty->acknowledged)
            throw std::logic_error("regionOwnership: dirty ACK overflow");
        auto* queue=ecs.resource<world::RegionDirtyQueue>();
        stagedLedger.commit();
        rollback.committed=true;
        queue->acknowledged+=count;
        queue->entries.clear();
    } else {
        stagedLedger.commit();
        rollback.committed=true;
    }
    // Avoid replacing the derived resource on ticks with unchanged membership.
    if(!previousSeats || previousSeats->carByMember!=carByMember) {
        RegionSeatIndex next=previousSeats?*previousSeats:RegionSeatIndex{};
        // Replace only candidate groups using the validated canonical graph.
        // Compare with the full scan before publishing; missed events trigger
        // a complete rebuild rather than leaving a stale reverse index.
        if(previousSeats && journalReady && !journalOverflow) {
            for(auto car:candidateCars)next.eraseCar(car);
            for(const auto& [member,car]:carByMember)
                if(candidateCars.contains(car.packed()))next.setMember(member,car);
        }
        if(next.carByMember!=carByMember) {
            next=RegionSeatIndex{};
            for(const auto& [member,car]:carByMember)next.setMember(member,car);
        }
        ecs.setResource<RegionSeatIndex>(std::move(next));
    }
    (void)ecs.drainComponentChanges<CharacterBody>();
    (void)ecs.drainComponentChanges<Vehicle>();
    (void)ecs.drainComponentChanges<Loot>();
}
} // namespace aiverse::actor
