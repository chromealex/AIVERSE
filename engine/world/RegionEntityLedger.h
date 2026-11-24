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
    static constexpr uint32_t kSchema=2;

    // Explicit transaction wrapper; callers must use its mutation methods.
    class Transaction;

    // Host-owned, thread-confined observation of actual mutation order.
    // No state is embedded in the ledger, copied into ECS snapshots or archived.
    // The caller must hold a quiescent tick boundary and verify the resulting
    // stream against the final ledger (replacement/COW/other lanes can bypass it).
    using Mutation=std::pair<RegionEntityId,RegionEntityRecord>;
    class MutationCapture {
        const RegionEntityLedger* owner_;
        uint64_t initial_;
        size_t limit_;
        bool failed_=false;
        std::vector<Mutation> records_;
        inline static thread_local MutationCapture* active_=nullptr;
        friend class RegionEntityLedger;
        void append(RegionEntityId id,const RegionEntityRecord& record)noexcept{
            if(failed_)return;
            if(records_.size()>=limit_){failed_=true;return;}
            try{records_.emplace_back(id,record);}catch(...){failed_=true;}
        }
        void truncate(uint64_t revision)noexcept{
            while(!records_.empty()&&records_.back().second.revision>revision)records_.pop_back();
        }
    public:
        static constexpr size_t MaxRecords=(16u*1024u*1024u-16)/29;
        explicit MutationCapture(const RegionEntityLedger& owner,size_t limit=MaxRecords)
            :owner_(&owner),initial_(owner.lastRevision()),limit_(std::min(limit,MaxRecords)){
            if(active_){failed_=true;return;}
            active_=this;
        }
        MutationCapture(const MutationCapture&)=delete;
        MutationCapture& operator=(const MutationCapture&)=delete;
        ~MutationCapture(){stop();}
        void stop()noexcept{if(active_==this)active_=nullptr;}
        bool ok()const{return !failed_;}
        uint64_t initialRevision()const{return initial_;}
        const std::vector<Mutation>& records()const{return records_;}
    };

    bool spawn(RegionEntityId id,Cell owner) {
        if(!id || id<=retiredFloor_ || records_.contains(id) || records_.size()>=kMaxEntities ||
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
        recordMutation(id,entry->second);
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
            recordMutation(id,entry);
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
            recordMutation(id,entry);
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
        recordMutation(id,it->second);
        return true;
    }
    const RegionEntityRecord* find(RegionEntityId id) const {
        const auto it=records_.find(id);
        return it==records_.end()?nullptr:&it->second;
    }
    // A compacted prefix is sealed permanently. Missing IDs at or below
    // the floor cannot be allocated again, including previously unused gaps.
    RegionEntityId retiredFloor()const noexcept{return retiredFloor_;}
    uint64_t historyFloor()const noexcept{return historyFloor_;}
    bool isRetired(RegionEntityId id)const{
        const auto* record=find(id);
        return record?!record->live:(id!=0&&id<=retiredFloor_);
    }
    RegionEntityId nextIdentity()const noexcept{
        const auto high=records_.empty()?retiredFloor_:std::max(retiredFloor_,records_.rbegin()->first);
        return high==UINT64_MAX?0:high+1;
    }
    bool sameState(const RegionEntityLedger& other)const{
        return lastRevision_==other.lastRevision_&&retiredFloor_==other.retiredFloor_&&
            historyFloor_==other.historyFloor_&&records_==other.records_;
    }
    size_t tombstoneCount()const{return records_.size()-liveIds_.size();}
    // Exclusive checkpoint barrier on a private candidate. This is NOT a
    // gameplay mutation or a WAL operation. Do not call inside Transaction,
    // MutationCapture, or while observers reference this ledger. Publish
    // only after the compacted checkpoint is durable.
    size_t compactRetired(uint64_t through,size_t limit=kMaxEntities){
        if(through>lastRevision_||!limit||activeTransactions_)return 0;
        auto* capture=MutationCapture::active_;
        if(capture&&capture->owner_==this)return 0;
        size_t removed=0;
        for(auto it=revisionIndex_.begin();it!=revisionIndex_.end()&&it->first<=through&&removed<limit;){
            auto row=records_.find(it->second);
            if(row->second.live){++it;continue;}
            retiredFloor_=std::max(retiredFloor_,row->first);
            historyFloor_=std::max(historyFloor_,it->first);
            records_.erase(row);it=revisionIndex_.erase(it);++removed;
        }
        return removed;
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
        for(auto id:liveIds_)
            if(records_.at(id).owner==cell)result.push_back(id);
        return result;
    }
    // Convergent latest-state changes: tombstones and moved-out records remain.
    // Caller persists this whole ledger, not only records active in a visible cell.
    std::vector<std::pair<RegionEntityId,RegionEntityRecord>> since(uint64_t revision) const {
        if(revision<historyFloor_)throw std::logic_error("region history compacted: bootstrap required");
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
        bool resetRequired=false;
    };
    ChangePage pageSince(uint64_t after,size_t limit)const {
        ChangePage page;
        page.nextRevision=after;
        if(after<historyFloor_||after>lastRevision_){page.resetRequired=true;return page;}
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
    void recordMutation(RegionEntityId id,const RegionEntityRecord& record)noexcept{
        auto* capture=MutationCapture::active_;
        if(capture&&capture->owner_==this)capture->append(id,record);
    }
    void rollbackCapture(uint64_t revision)noexcept{
        auto* capture=MutationCapture::active_;
        if(capture&&capture->owner_==this)capture->truncate(revision);
    }
    inline static thread_local size_t activeTransactions_=0;
    uint64_t lastRevision_=0;
    RegionEntityId retiredFloor_=0;
    uint64_t historyFloor_=0;
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
        constexpr size_t maxCaptured=(16*1024*1024-16)/29;
        if(changes.size()>maxCaptured-mutations_.size())
            throw std::length_error("region WAL transaction exceeds frame budget");
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
        :ledger_(ledger),initialRevision_(ledger.lastRevision_),capture_(capture) {++activeTransactions_;}
    Transaction(const Transaction&)=delete;
    Transaction& operator=(const Transaction&)=delete;
    ~Transaction() noexcept {
        --activeTransactions_;
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
        ledger_.rollbackCapture(initialRevision_);
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
    uint64_t last_=0;
public:
    explicit RegionChangeCursor(uint64_t after=0):last_(after){}
    uint64_t lastRevision()const{return last_;}
    void reset(uint64_t after=0){last_=after;}
    bool requiresBootstrap(const RegionEntityLedger& ledger)const{
        return last_<ledger.historyFloor()||last_>ledger.lastRevision();
    }

    // Returns true once caught up; false for a refused callback, zero budget
    // or a remaining page. A load/rollback owner must call reset() because a
    // fresh World can have the same revision number with different records.
    template<class Deliver>
    bool consume(const RegionEntityLedger& ledger,size_t budget,Deliver&& deliver) {
        if(!budget)return false;
        if(last_>ledger.lastRevision())return false;
        const auto page=ledger.pageSince(last_,budget);
        if(page.resetRequired)return false;
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
    if(!a.ok() || (schema!=1&&schema!=RegionEntityLedger::kSchema)){a.fail();return;}
    uint64_t latest=ledger.lastRevision_;
    a(latest);
    uint64_t retired=0,history=0;
    if(schema>=2){
        if(a.writing()){retired=ledger.retiredFloor_;history=ledger.historyFloor_;}
        a(retired,history);
    }
    if(!a.ok()||history>latest||bool(retired)!=bool(history)){a.fail();return;}
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
        ledger.retiredFloor_=retired;ledger.historyFloor_=history;
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
    sim.template registerPersistentResourceMigration<RegionEntityLedger>(
        "aiverse.world.region_entity_ledger",1,[](Archive& a,RegionEntityLedger& ledger){io(a,ledger);});
}

} // namespace aiverse::world
