// Authoritative pre-tick destination requests for large-world movement.
// Requests persist in native ECS, but asset publication remains host-derived.
#pragma once
#include "engine/core/Archive.h"
#include "engine/sim/CommonComponents.h"
#include <cstdint>
#include <map>

namespace aiverse::world {

struct RegionMoveRequests {
    static constexpr uint32_t kSchema=1;
    static constexpr size_t kMaxRequests=64;
    std::map<uint64_t,Vec3> destinations; // packed Entity -> destination
    uint64_t applied=0;
};

inline void io(Archive& a,RegionMoveRequests& q) {
    uint32_t schema=RegionMoveRequests::kSchema;
    a(schema);
    if(!a.ok() || schema!=RegionMoveRequests::kSchema){a.fail();return;}
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
    // Save/load is an atomic staged-World operation. Reject a request that
    // targets a destroyed or recycled ECS handle, before the running
    // simulation is overwritten by the decoded state.
    sim.addPersistentLoadValidator("aiverse.world.region_move_requests",
        [](const World& world,std::string& error)->bool {
            const auto* moves=world.resourceConst<RegionMoveRequests>();
            if(!moves)return true;
            for(const auto& [id,destination]:moves->destinations) {
                (void)destination;
                if(!id || !world.alive(Entity::fromPacked(id-1))) {
                    error="staged region move references a dead entity";
                    return false;
                }
            }
            return true;
        });
}

} // namespace aiverse::world
