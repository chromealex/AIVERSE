// Deterministic region ownership for durable dynamic ECS identities.
// This is simulation state, NOT the renderer/asset ChunkStreamer residency list.
#pragma once

#include "engine/core/Archive.h"
#include "engine/world/SpatialWorld.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
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

    // Explicit transaction wrapper; callers must use its mutation methods.
    class Transaction;

    bool spawn(RegionEntityId id,Cell owner) {
        if(!id || records_.contains(id) || records_.size()>=kMaxEntities ||
           lastRevision_==std::numeric_limits<uint64_t>::max())return false;
        const auto next=lastRevision_+1;
        // New IDs allocate three map/set entries. On allocation failure,
        // leave no partially published ledger record or derived index.
        const auto entry=records_.emplace(id,RegionEntityRecord{owner,next,1}).first;
        try {
            liveIds_.insert(id);
            revisionIndex_.emplace(next,id);
        }catch(...) {
            liveIds_.erase(id);
            records_.erase(entry);
            throw;
        }
        lastRevision_=next;
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
            // Re-key the existing node in place: no new allocation during
            // a validated multi-actor ownership transfer.
            auto node=revisionIndex_.extract(entry.revision);
            entry.owner=destination;
            entry.revision=++lastRevision_;
            node.key()=entry.revision;
            revisionIndex_.insert(std::move(node));
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
            auto node=revisionIndex_.extract(entry.revision);
            entry.owner=destination;
            entry.revision=++lastRevision_;
            node.key()=entry.revision;
            revisionIndex_.insert(std::move(node));
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
        auto node=revisionIndex_.extract(it->second.revision);
        it->second.live=0;
        it->second.revision=++lastRevision_;
        liveIds_.erase(id);
        node.key()=it->second.revision;
        revisionIndex_.insert(std::move(node));
        return true;
    }
    const RegionEntityRecord* find(RegionEntityId id) const {
        const auto it=records_.find(id);
        return it==records_.end()?nullptr:&it->second;
    }
    uint64_t lastRevision() const {return lastRevision_;}
    size_t size() const {return records_.size();}
    // Derived active-ID index: tombstones are retained for archives/history,
    // but a gameplay tick never needs to scan them to identify removals.
    // Stable, ordered, copy-on-write safe; rebuilt on native ECS disk load.
    const std::set<RegionEntityId>& liveIds() const {return liveIds_;}
    size_t liveCount() const {return liveIds_.size();}
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
        const auto first=revision==std::numeric_limits<uint64_t>::max()
            ? revisionIndex_.end():revisionIndex_.upper_bound(revision);
        for(auto it=first;it!=revisionIndex_.end();++it) {
            const auto row=records_.find(it->second);
            if(row!=records_.end())result.emplace_back(row->first,row->second);
        }
        return result;
    }
    // Bounded latest-state change feed for streaming observers and disk
    // snapshot deltas. Only each ID's current record is represented; this
    // is NOT a full audit of intermediate moves or a crash-safe disk journal.
    struct ChangePage {
        std::vector<std::pair<RegionEntityId,RegionEntityRecord>> changes;
        uint64_t nextRevision=0;
        bool more=false;
    };
    ChangePage pageSince(uint64_t after,size_t limit)const {
        ChangePage page;
        page.nextRevision=after;
        if(!limit || after==std::numeric_limits<uint64_t>::max())return page;
        auto it=revisionIndex_.upper_bound(after);
        for(;it!=revisionIndex_.end() && page.changes.size()<limit;++it) {
            auto record=records_.find(it->second);
            if(record==records_.end())continue;
            page.changes.emplace_back(record->first,record->second);
            page.nextRevision=record->second.revision;
        }
        page.more=it!=revisionIndex_.end();
        return page;
    }

private:
    uint64_t lastRevision_=0;
    std::map<RegionEntityId,RegionEntityRecord> records_;
    std::set<RegionEntityId> liveIds_; // never separately saved/hashed
    std::map<uint64_t,RegionEntityId> revisionIndex_; // current revision -> id
    friend void io(Archive&,RegionEntityLedger&);
};

// Exclusive ledger transaction. No snapshots, observers or external mutations
// may run while active. Rollback reuses revision nodes and preallocated live nodes.
class RegionEntityLedger::Transaction {
    RegionEntityLedger& ledger_;
    uint64_t initialRevision_;
    struct Before {
        bool existed=false;
        RegionEntityRecord record{};
    };
    std::map<RegionEntityId,Before> before_;
    std::set<RegionEntityId> restoreLive_;
    bool committed_=false;
    bool capture_=false;
    std::vector<std::pair<RegionEntityId,RegionEntityRecord>> mutations_;
    void captureSince(uint64_t revision) {
        if(!capture_)return;
        auto changes=ledger_.since(revision);
        mutations_.insert(mutations_.end(),changes.begin(),changes.end());
    }
    void remember(RegionEntityId id) {
        if(before_.contains(id))return;
        const auto* old=ledger_.find(id);
        if(old && old->live)restoreLive_.insert(id);
        before_.emplace(id,old?Before{true,*old}:Before{});
    }
public:
    explicit Transaction(RegionEntityLedger& ledger,bool capture=false)
        :ledger_(ledger),initialRevision_(ledger.lastRevision_),capture_(capture) {}
    Transaction(const Transaction&)=delete;
    Transaction& operator=(const Transaction&)=delete;
    ~Transaction() noexcept {
        if(committed_)return;
        for(const auto& [id,old]:before_) {
            auto it=ledger_.records_.find(id);
            if(!old.existed) {
                if(it!=ledger_.records_.end()) {
                    ledger_.revisionIndex_.erase(it->second.revision);
                    ledger_.liveIds_.erase(id);
                    ledger_.records_.erase(it);
                }
                continue;
            }
            auto node=ledger_.revisionIndex_.extract(it->second.revision);
            it->second=old.record;
            node.key()=old.record.revision;
            ledger_.revisionIndex_.insert(std::move(node));
            if(old.record.live && !ledger_.liveIds_.contains(id))
                ledger_.liveIds_.insert(restoreLive_.extract(id));
            if(!old.record.live)ledger_.liveIds_.erase(id);
        }
        ledger_.lastRevision_=initialRevision_;
    }
    bool spawn(RegionEntityId id,Cell owner) {
        remember(id);const auto rev=ledger_.lastRevision();
        const bool ok=ledger_.spawn(id,owner);if(ok)captureSince(rev);return ok;
    }
    bool despawn(RegionEntityId id,Cell expected) {
        remember(id);const auto rev=ledger_.lastRevision();
        const bool ok=ledger_.despawn(id,expected);if(ok)captureSince(rev);return ok;
    }
    bool transfer(RegionEntityId id,Cell expected,Cell destination) {
        remember(id);const auto rev=ledger_.lastRevision();
        const bool ok=ledger_.transfer(id,expected,destination);if(ok)captureSince(rev);return ok;
    }
    bool transferGroup(std::span<const std::pair<RegionEntityId,Cell>> members,
                       Cell destination) {
        for(const auto& [id,owner]:members)if(owner!=destination)remember(id);
        const auto rev=ledger_.lastRevision();
        const bool ok=ledger_.transferGroup(members,destination);if(ok)captureSince(rev);return ok;
    }
    const RegionEntityRecord* find(RegionEntityId id)const{return ledger_.find(id);}
    const RegionEntityLedger& view()const{return ledger_;}
    uint64_t initialRevision()const{return initialRevision_;}
    const auto& mutations()const{return mutations_;}
    bool capturing()const{return capture_;}
    void commit() noexcept {committed_=true;}
};

// Host-side incremental observer. Use it to dispatch the ledger's latest
// state changes to region/collision/streaming adapters without a whole-ledger
// scan. The cursor is derived observer state, NOT gameplay authority.
// Advance only after the callback acknowledges an entry. A callback may fail
// or throw; earlier acknowledged entries are never replayed accidentally.
class RegionChangeCursor {
public:
    explicit RegionChangeCursor(uint64_t after=0):last_(after){}
    uint64_t lastRevision()const{return last_;}
    void reset(uint64_t after=0){last_=after;}

    // Returns true once caught up; false for a refused callback, zero budget
    // or a remaining page. A load/rollback owner must call reset() because a
    // fresh World can have the same revision number with different records.
    template<class Deliver>
    bool consume(const RegionEntityLedger& ledger,size_t budget,Deliver&& deliver) {
        if(!budget)return false;
        if(last_>ledger.lastRevision())return false;
        const auto page=ledger.pageSince(last_,budget);
        for(const auto& [id,record]:page.changes) {
            if(!deliver(id,record))return false;
            last_=record.revision;
        }
        return !page.more;
    }
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
    std::set<RegionEntityId> live;
    std::map<uint64_t,RegionEntityId> ordered;
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
        if(record.live)live.insert(id);
        ordered.emplace(record.revision,id);
    }
    if(a.ok()) {
        ledger.lastRevision_=latest;
        ledger.records_=std::move(decoded);
        ledger.liveIds_=std::move(live);
        ledger.revisionIndex_=std::move(ordered);
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
