// Authoritative pre-tick destination requests for large-world movement.
// Requests persist in native ECS, but asset publication remains host-derived.
#pragma once
#include "engine/core/Archive.h"
#include "engine/sim/CommonComponents.h"
#include "PersistentEntityReferences.h"
#include <cstdint>
#include <map>

namespace aiverse::world {

struct RegionMoveRequests {
    static constexpr uint32_t kSchema=2;
    static constexpr size_t kMaxRequests=64;
    std::map<RegionEntityId,Vec3> destinations; // persistent identity -> destination
    uint64_t applied=0;
};

inline void io(Archive& a,RegionMoveRequests& q) {
    uint32_t schema=RegionMoveRequests::kSchema;
    a(schema);
    if(!a.ok() || (schema!=RegionMoveRequests::kSchema && (a.writing()||schema!=1))){a.fail();return;}
    uint64_t applied=q.applied;
    a(applied);
    uint32_t count=0;
    if(a.writing()) {
        if(q.destinations.size()>RegionMoveRequests::kMaxRequests){a.fail();return;}
        count=uint32_t(q.destinations.size());
    }
    a(count);
    if(!a.ok() || count>RegionMoveRequests::kMaxRequests){a.fail();return;}
    if(a.writing()) {
        for(const auto& [id,pos]:q.destinations) {
            uint64_t key=id;
            Vec3 location=pos;
            a(key,location);
            if(!a.ok() || !key){a.fail();return;}
        }
    } else {
        std::map<uint64_t,Vec3> decoded;
        for(uint32_t i=0;i<count && a.ok();++i) {
            uint64_t key=0;
            Vec3 location{};
            a(key,location);
            if(!a.ok() || !key || !decoded.emplace(key,location).second) {
                a.fail();return;
            }
        }
        if(a.ok()) {
            q.applied=applied;
            q.destinations.swap(decoded);
        }
    }
}

template<class SimulationT>
void registerRegionMoveRequests(SimulationT& sim) {
    sim.template registerPersistentResource<RegionMoveRequests>(
        "aiverse.world.region_move_requests",RegionMoveRequests::kSchema);
    // v1 keys were packed()+1, which was also the stored stable tag in
    // v1 ActorHost saves. Preserve the numeric tag and validate the binding.
    sim.template registerPersistentResourceMigration<RegionMoveRequests>(
        "aiverse.world.region_move_requests",1,[](Archive& a,RegionMoveRequests& q){io(a,q);});
    // Save/load must reject unresolved or retired identities.
    sim.addPersistentLoadValidator("aiverse.world.region_move_requests",
        [](const World& world,std::string& error)->bool {
            const auto* moves=world.resourceConst<RegionMoveRequests>();
            if(!moves)return true;
            if(moves->destinations.empty())return true;
            const auto* ledger=world.resourceConst<RegionEntityLedger>();
            PersistentEntityReferences refs;
            if(!ledger||!refs.rebuild(world,*ledger)){
                error="staged region move identity index unavailable";
                return false;
            }
            for(const auto& [id,destination]:moves->destinations) {
                (void)destination;
                Entity entity;
                if(!id || !refs.resolve(world,id,entity)) {
                    error="staged region move references an unresolved identity";
                    return false;
                }
            }
            return true;
        });
}

} // namespace aiverse::world
