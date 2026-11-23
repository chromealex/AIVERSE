// Deterministic region ownership for durable dynamic ECS identities.
// This is simulation state, NOT the renderer/asset ChunkStreamer residency list.
#pragma once

#include "engine/core/Archive.h"
#include "engine/world/SpatialWorld.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <span>
#include <utility>
#include <vector>

namespace aiverse::world {

// Never use a transient array index for persistent identity. The host allocates a
// stable nonzero id and stores its mapping to an ECS Entity separately.
using RegionEntityId = uint64_t;

struct RegionEntityRecord {
    Cell owner{};
    uint64_t revision=0;
    uint8_t live=0; // 0 is a durable tombstone; cannot be resurrected by stale work.
    bool operator==(const RegionEntityRecord&) const = default;
};
inline void io(Archive& a,RegionEntityRecord& value) {
    a(value.owner.x,value.owner.y,value.owner.z,value.revision,value.live);
}

// Persistent ownership ledger. This does NOT deactivate, create or destroy
// components by itself: a host must own ECS activation + collision/navmesh fences.
// Unlike view streaming decisions, every mutation must occur on a simulation tick.
class RegionEntityLedger {
public:
    static constexpr size_t kMaxEntities=1000000;
    static constexpr uint32_t kSchema=1;

    bool spawn(RegionEntityId id,Cell owner) {
        if(!id || records_.contains(id) || records_.size()>=kMaxEntities ||
           lastRevision_==std::numeric_limits<uint64_t>::max())return false;
        records_.emplace(id,RegionEntityRecord{owner,++lastRevision_,1});
        return true;
    }
    // Two-phase validation prevents half-moving an occupied car and its passengers.
    // IDs are sorted internally so the resulting revision order is input-order independent.
    bool transfer(std::span<const RegionEntityId> ids,Cell expected,Cell destination) {
        if(ids.empty() || ids.size()>kMaxEntities ||
           lastRevision_>std::numeric_limits<uint64_t>::max()-ids.size())return false;
        std::vector<RegionEntityId> sorted(ids.begin(),ids.end());
        std::sort(sorted.begin(),sorted.end());
        if(std::adjacent_find(sorted.begin(),sorted.end())!=sorted.end())return false;
        for(auto id:sorted) {
            auto it=records_.find(id);
            if(it==records_.end() || !it->second.live || it->second.owner!=expected)return false;
        }
        if(destination==expected)return true;
        for(auto id:sorted) {
            auto& entry=records_.at(id);
            entry.owner=destination;
            entry.revision=++lastRevision_;
        }
        return true;
    }
    bool transfer(RegionEntityId id,Cell expected,Cell destination) {
        return transfer(std::span<const RegionEntityId>(&id,1),expected,destination);
    }
    // Atomic relocation of one graph whose members may come from different
    // cells (e.g. a driver boards a car at the border). Verify ALL expected
    // owners before changing a single entry, in stable id order.
    bool transferGroup(std::span<const std::pair<RegionEntityId,Cell>> members,
                       Cell destination) {
        if(members.empty() || members.size()>kMaxEntities)return false;
        std::vector<std::pair<RegionEntityId,Cell>> sorted(members.begin(),members.end());
        std::sort(sorted.begin(),sorted.end(),[](const auto& a,const auto& b){
            return a.first<b.first;
        });
        size_t moving=0;
        for(size_t i=0;i<sorted.size();++i) {
            const auto [id,expected]=sorted[i];
            if(!id || (i && sorted[i-1].first==id))return false;
            const auto it=records_.find(id);
            if(it==records_.end() || !it->second.live || it->second.owner!=expected)
                return false;
            if(expected!=destination)++moving;
        }
        if(moving>std::numeric_limits<uint64_t>::max()-lastRevision_)return false;
        for(const auto& [id,expected]:sorted) {
            if(expected==destination)continue;
            auto& entry=records_.at(id);
            entry.owner=destination;
            entry.revision=++lastRevision_;
        }
        return true;
    }
    const std::map<RegionEntityId,RegionEntityRecord>& records()const{
        return records_;
    }

    bool despawn(RegionEntityId id,Cell expected) {
        const auto it=records_.find(id);
        if(it==records_.end() || !it->second.live || it->second.owner!=expected ||
           lastRevision_==std::numeric_limits<uint64_t>::max())return false;
        it->second.live=0;
        it->second.revision=++lastRevision_;
        return true;
    }
    const RegionEntityRecord* find(RegionEntityId id) const {
        const auto it=records_.find(id);
        return it==records_.end()?nullptr:&it->second;
    }
    uint64_t lastRevision() const {return lastRevision_;}
    size_t size() const {return records_.size();}
    // Iteration order is stable across platforms, including after save/restore.
    std::vector<RegionEntityId> activeIn(Cell cell) const {
        std::vector<RegionEntityId> result;
        for(const auto& [id,record]:records_)
            if(record.live && record.owner==cell)result.push_back(id);
        return result;
    }
    // Convergent latest-state changes: tombstones and moved-out records remain.
    // Caller persists this whole ledger, not only records active in a visible cell.
    std::vector<std::pair<RegionEntityId,RegionEntityRecord>> since(uint64_t revision) const {
        std::vector<std::pair<RegionEntityId,RegionEntityRecord>> result;
        for(const auto& [id,record]:records_)
            if(record.revision>revision)result.emplace_back(id,record);
        std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){
            return a.second.revision==b.second.revision
                ? a.first<b.first : a.second.revision<b.second.revision;
        });
        return result;
    }

private:
    uint64_t lastRevision_=0;
    std::map<RegionEntityId,RegionEntityRecord> records_;
    friend void io(Archive&,RegionEntityLedger&);
};

// Transactional decode: an invalid/truncated archive must not mutate an
// existing resource. Archive is already portable and used by native ECS v10.
inline void io(Archive& a,RegionEntityLedger& ledger) {
    uint32_t schema=RegionEntityLedger::kSchema;
    a(schema);
    if(!a.ok() || schema!=RegionEntityLedger::kSchema){a.fail();return;}
    uint64_t latest=ledger.lastRevision_;
    a(latest);
    const uint32_t n=a.count(a.writing()?ledger.records_.size():0,29);
    if(!a.ok() || n>RegionEntityLedger::kMaxEntities){a.fail();return;}
    std::map<RegionEntityId,RegionEntityRecord> decoded;
    std::set<uint64_t> revisions;
    if(a.writing()) {
        for(const auto& [id,record]:ledger.records_) {
            RegionEntityId copyId=id;
            RegionEntityRecord copy=record;
            a(copyId,copy);
        }
        return;
    }
    for(uint32_t i=0;i<n && a.ok();++i) {
        RegionEntityId id=0;
        RegionEntityRecord record;
        a(id,record);
        if(!a.ok())break;
        if(id==0 || record.live>1 || record.revision==0 ||
           record.revision>latest || !revisions.insert(record.revision).second ||
           !decoded.emplace(id,record).second){a.fail();break;}
    }
    if(a.ok()) {
        ledger.lastRevision_=latest;
        ledger.records_=std::move(decoded);
    }
}

// Call during Simulation::setup, then create the resource in the ECS World.
// It is serialized/hashed automatically by persistent ECS v10 when enabled.
template<class SimulationT>
void registerRegionEntityLedger(SimulationT& sim) {
    sim.template registerPersistentResource<RegionEntityLedger>(
        "aiverse.world.region_entity_ledger",RegionEntityLedger::kSchema);
}

} // namespace aiverse::world
