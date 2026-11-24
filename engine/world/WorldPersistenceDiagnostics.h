#pragma once
#include "RegionEntityLedger.h"
#include "PersistentEntityReferences.h"
#include "engine/data/Json.h"

namespace aiverse::world {
// Shared read-only projection for operator tools. Never use JSON numbers for
// 64-bit identity/revision values (JavaScript cannot represent them exactly).
inline Json worldCounter(uint64_t n){return Json::string(std::to_string(n));}
inline Json worldCell(Cell c){
    Json a=Json::array();a.push(Json::number(int64_t(c.x)));a.push(Json::number(int64_t(c.y)));a.push(Json::number(int64_t(c.z)));return a;
}
inline Json ledgerStatus(const RegionEntityLedger& ledger,const World& w,size_t regionLimit=256){
    Json j=Json::object();
    j.set("revision",worldCounter(ledger.lastRevision()));j.set("historyFloor",worldCounter(ledger.historyFloor()));
    j.set("retiredFloor",worldCounter(ledger.retiredFloor()));j.set("nextIdentity",worldCounter(ledger.nextIdentity()));
    j.set("live",Json::number(int64_t(ledger.liveCount())));j.set("tombstones",Json::number(int64_t(ledger.tombstoneCount())));
    j.set("capacity",Json::number(int64_t(RegionEntityLedger::kMaxEntities)));
    // Exact encoded ledger size, not an allocator/RSS memory estimate.
    j.set("ledgerArchiveBytes",worldCounter(32+29*uint64_t(ledger.size())));
    PersistentEntityReferences refs;
    if(!refs.rebuild(w,ledger)){j.set("error",Json::string("invalid stable identity bindings"));return j;}
    j.set("materialized",Json::number(int64_t(refs.size())));
    struct Counts{size_t live=0,materialized=0;};
    std::map<Cell,Counts> regions;
    for(auto id:ledger.liveIds()){
        auto& count=regions[ledger.find(id)->owner];++count.live;
        Entity e;if(refs.resolve(w,id,e))++count.materialized;
    }
    Json rows=Json::array();
    for(const auto& [cell,count]:regions){
        if(rows.size()>=regionLimit)break;
        Json row=Json::object();row.set("cell",worldCell(cell));
        row.set("live",Json::number(int64_t(count.live)));row.set("materialized",Json::number(int64_t(count.materialized)));rows.push(std::move(row));
    }
    j.set("regionCount",Json::number(int64_t(regions.size())));j.set("regionsTruncated",Json::fromBool(rows.size()<regions.size()));
    j.set("regions",std::move(rows));return j;
}
// Bootstrap pages use stable ID order and a fixed revision supplied by the
// owner. A caller observing a changing world must restart on mismatch.
inline Json ledgerPage(const RegionEntityLedger& ledger,uint64_t after,size_t limit,
    bool bootstrap=false,uint64_t expectedRevision=0){
    Json j=Json::object(),rows=Json::array();limit=std::clamp<size_t>(limit,1,1000);
    j.set("revision",worldCounter(ledger.lastRevision()));
    j.set("historyFloor",worldCounter(ledger.historyFloor()));
    auto append=[&](RegionEntityId id,const RegionEntityRecord& r){
        Json row=Json::object();row.set("id",worldCounter(id));row.set("revision",worldCounter(r.revision));
        row.set("cell",worldCell(r.owner));row.set("live",Json::fromBool(bool(r.live)));rows.push(std::move(row));
    };
    bool reset=false,more=false;uint64_t next=after;
    if(bootstrap){
        reset=expectedRevision!=ledger.lastRevision();
        if(!reset){
            auto it=ledger.liveIds().upper_bound(after);
            for(;it!=ledger.liveIds().end()&&rows.size()<limit;++it){next=*it;append(*it,*ledger.find(*it));}
            more=it!=ledger.liveIds().end();
        }
    }else{
        const auto page=ledger.pageSince(after,limit);
        reset=page.resetRequired;more=page.more;next=page.nextRevision;
        if(!reset)for(const auto& [id,r]:page.changes)append(id,r);
    }
    j.set("bootstrap",Json::fromBool(bootstrap));j.set("resetRequired",Json::fromBool(reset));
    j.set("more",Json::fromBool(more));j.set("next",worldCounter(next));j.set("records",std::move(rows));return j;
}
}
