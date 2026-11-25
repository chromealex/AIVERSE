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
    // Runtime packed Entity + 1 -> bitmask of pending lifecycle observations.
    // These are transient observation keys, NOT RegionStableIdentity IDs.
    // Native snapshots retain the allocator; portable restart must drain this queue.
    // A single handle can appear many times during a tick; flags are coalesced.
    std::map<uint64_t,uint8_t> entries;
    uint64_t acknowledged=0;

    bool mark(Entity entity,RegionDirtyKind kind) {
        if(kind!=RegionDirtyKind::Spawn && kind!=RegionDirtyKind::Move &&
           kind!=RegionDirtyKind::Retire)return false;
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
        if(kind!=RegionDirtyKind::Spawn && kind!=RegionDirtyKind::Move &&
           kind!=RegionDirtyKind::Retire)return false;
        if(!canMarkMany(entities))return false;
        // Reserve only the missing group keys, NOT a copy of every queued
        // entity: work stays proportional to the moved occupant group.
        // Existing flags are modified only after every allocation succeeds.
        std::set<uint64_t> missing;
        for(const Entity e:entities) {
            const uint64_t key=e.packed()+1;
            if(!entries.contains(key))missing.insert(key);
        }
        std::vector<uint64_t> inserted;
        inserted.reserve(missing.size());
        try {
            for(const uint64_t key:missing) {
                entries.emplace(key,uint8_t(0));
                inserted.push_back(key);
            }
        }catch(...) {
            for(const uint64_t key:inserted)entries.erase(key);
            throw;
        }
        for(const Entity e:entities)
            entries.find(e.packed()+1)->second|=uint8_t(kind);
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
    // Each entry is uint64_t key (8 bytes) + uint8_t flags (1 byte).
    // Using 17 overestimated minimum payload size, wrongly rejecting
    // legitimate archives with three or more pending notifications.
    uint32_t count=a.count(a.writing()?q.entries.size():0,9);
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
