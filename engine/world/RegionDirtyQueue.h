// W1.15: explicitly reported ECS world-entity lifecycle changes.
// Authoritative, persisted, deterministic; independent of observer/camera IO.
#pragma once
#include "engine/core/Archive.h"
#include "engine/sim/Simulation.h"
#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <vector>

namespace aiverse::world {

enum class RegionDirtyKind : uint8_t {
    Spawn=1, Move=2, Retire=4
};

struct RegionDirtyQueue {
    static constexpr uint32_t kSchema=1;
    static constexpr size_t kMaxEntries=65536;
    // Stable packed Entity + 1 -> bitmask of pending lifecycle observations.
    // A single ID can appear many times during a tick; flags are coalesced.
    std::map<uint64_t,uint8_t> entries;
    uint64_t acknowledged=0;

    bool mark(Entity entity,RegionDirtyKind kind) {
        if(!entity.valid() || entity.packed()==UINT64_MAX)return false;
        const uint64_t key=entity.packed()+1;
        auto it=entries.find(key);
        if(it==entries.end() && entries.size()>=kMaxEntries)return false;
        if(it==entries.end())entries.emplace(key,uint8_t(kind));
        else it->second|=uint8_t(kind);
        return true;
    }
    // Validate a whole car/occupant batch before committing any flag.
    // Ordinary same-tick repeats consume no additional queue slots.
    bool canMarkMany(std::span<const Entity> entities)const {
        std::set<uint64_t> missing;
        for(const Entity e:entities) {
            if(!e.valid() || e.packed()==UINT64_MAX)return false;
            const uint64_t key=e.packed()+1;
            if(!entries.contains(key))missing.insert(key);
        }
        return missing.size()<=kMaxEntries-entries.size();
    }
    bool markMany(std::span<const Entity> entities,RegionDirtyKind kind) {
        if(!canMarkMany(entities))return false;
        // All logical checks happened above. Build a complete next map to
        // make allocation failure atomic as well, then swap it into place.
        auto staged=entries;
        for(const Entity e:entities)
            staged[e.packed()+1]|=uint8_t(kind);
        entries.swap(staged);
        return true;
    }
    bool marked(Entity entity)const {
        return entity.valid() && entity.packed()!=UINT64_MAX &&
               entries.contains(entity.packed()+1);
    }
};

inline void io(Archive& a,RegionDirtyQueue& q) {
    uint32_t schema=RegionDirtyQueue::kSchema;
    a(schema);
    if(!a.ok() || schema!=RegionDirtyQueue::kSchema){a.fail();return;}
    uint64_t ack=q.acknowledged;
    a(ack);
    uint32_t count=a.count(a.writing()?q.entries.size():0,17);
    if(!a.ok() || count>RegionDirtyQueue::kMaxEntries){a.fail();return;}
    if(a.writing()) {
        for(const auto& [id,flags]:q.entries) {
            uint64_t key=id;
            uint8_t bits=flags;
            if(!key || !bits || (bits&~uint8_t(7))){a.fail();return;}
            a(key,bits);
            if(!a.ok())return;
        }
        return;
    }
    std::map<uint64_t,uint8_t> staged;
    for(uint32_t i=0;i<count && a.ok();++i) {
        uint64_t id=0;
        uint8_t flags=0;
        a(id,flags);
        if(!a.ok() || !id || !flags || (flags&~uint8_t(7)) ||
           !staged.emplace(id,flags).second){a.fail();return;}
    }
    if(a.ok()) {
        q.acknowledged=ack;
        q.entries.swap(staged);
    }
}

template<class SimulationT>
void registerRegionDirtyQueue(SimulationT& sim) {
    sim.template registerPersistentResource<RegionDirtyQueue>(
        "aiverse.world.region_dirty_queue",RegionDirtyQueue::kSchema);
}

} // namespace aiverse::world
