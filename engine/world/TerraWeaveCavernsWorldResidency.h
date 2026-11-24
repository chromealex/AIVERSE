#pragma once
// TerraWeave Caverns Stage36: world-owned cave residency intent.
// No GPU/Box3D/NavMesh mutation here. Build/retire tickets remain live
// until the actual native owner confirms publication/retirement.
// Dirty edits, worker jobs and gameplay readers are held by explicit leases.
#include "engine/world/TerraWeaveCavernsStreaming.h"
#include "engine/world/TerraWeaveCavernsDistanceLod.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

namespace aiverse::terraweave::caverns {
// World-to-chunk conversion for gameplay camera/player positions in cm.
// Use the same resolution and spacing as the active authoritative cave world.
inline ChunkKey3 caveStreamingChunkAt(PointCm worldCm,
                                     uint32_t resolution,int32_t spacingCm){
    if(!resolution||resolution>64||spacingCm<=0||spacingCm>10000)
        throw std::invalid_argument("Caverns streaming chunk dimensions");
    validatePoint(worldCm);
    const int64_t span=int64_t(resolution)*spacingCm;
    return {floorDiv(worldCm.x,span),floorDiv(worldCm.y,span),
            floorDiv(worldCm.z,span)};
}
enum class CaveResidencyAction : uint8_t { Build, Retire };
enum class CaveLeasePurpose : uint8_t { Edit, Worker, Reader, Publication };
struct CaveResidencyTicket {
    CaveResidencyAction action=CaveResidencyAction::Build;
    ChunkKey3 key{};
    uint64_t generation=0;
    uint64_t sourceRevision=0;
    uint32_t lod=0;
};
struct CaveResidencyStats {
    size_t tracked=0,ready=0,buildsInFlight=0,retirePending=0,pinned=0;
    uint64_t sourceRevision=0;
};

class CaveWorldResidency {
public:
    explicit CaveWorldResidency(StreamSettings settings={}):settings_(settings){
        if(settings_.radiusXZ>16 || settings_.radiusY>8 ||
           !settings_.maxResident || settings_.maxResident>65536 ||
           !settings_.maxBuildsPerPump || settings_.maxBuildsPerPump>1024 ||
           !settings_.maxRetiresPerPump || settings_.maxRetiresPerPump>1024)
            throw std::invalid_argument("Caverns residency settings");
    }
    void center(ChunkKey3 key){
        if(key.x< -kLimitCm||key.x>kLimitCm||
           key.y< -kLimitCm||key.y>kLimitCm||
           key.z< -kLimitCm||key.z>kLimitCm)
            throw std::out_of_range("Caverns streaming center outside world");
        center_=key;
    }
    void sourceRevision(uint64_t revision){
        if(!revision)throw std::invalid_argument("Caverns residency revision");
        if(revision==revision_)return;
        revision_=revision;
        for(auto& [key,item]:items_){
            (void)key;
            item.generation=nextGeneration();
            item.building=false;
            item.retiring=false;
            // Old visuals remain owned until the new revision is committed.
            item.ready=false;
        }
    }
    void enableDistanceLod(CaveDistanceLodSettings settings={}) {
        validateCaveDistanceLod(settings);
        distanceLod_=settings;
        adaptiveLod_=true;
    }
    void disableDistanceLod(){adaptiveLod_=false;}
    bool adaptiveLodEnabled()const noexcept{return adaptiveLod_;}
    void visualLod(uint32_t lod){
        if(lod>8)throw std::invalid_argument("Caverns stream LOD");
        if(lod_==lod)return;
        lod_=lod;
        for(auto& [key,item]:items_){
            (void)key;
            item.generation=nextGeneration();
            item.building=false;
            item.retiring=false;
            item.ready=false;
        }
    }

    // A lease is a numbered owner reference. It cannot silently disappear
    // on camera movement; unpin must match both key and exact lease ID.
    uint64_t pin(ChunkKey3 key,CaveLeasePurpose purpose){
        if(key.x< -kLimitCm||key.x>kLimitCm||
           key.y< -kLimitCm||key.y>kLimitCm||
           key.z< -kLimitCm||key.z>kLimitCm)
            throw std::out_of_range("Caverns residency pin bounds");
        if(!revision_)throw std::logic_error("Caverns pin before revision");
        if(nextLease_==UINT64_MAX)
            throw std::overflow_error("Caverns lease identity");
        if(!items_.contains(key)&&items_.size()>=settings_.maxResident)
            throw std::length_error("Caverns pinned residency capacity exhausted");
        auto& item=ensure(key);
        const uint64_t id=++nextLease_;
        item.leases.emplace(id,purpose);
        if(item.retiring) {
            // Cancel an obsolete retire ticket; the old resident is still
            // owned because retire ACK has not happened.
            item.generation=nextGeneration();
            item.retiring=false;
        }
        return id;
    }
    bool unpin(ChunkKey3 key,uint64_t leaseId){
        const auto it=items_.find(key);
        if(it==items_.end())return false;
        return it->second.leases.erase(leaseId)!=0;
    }
    bool pinned(ChunkKey3 key)const{
        const auto it=items_.find(key);
        return it!=items_.end()&&!it->second.leases.empty();
    }
    // Bounded closest-first desired set. Pinned entries outside camera range
    // remain desired without consuming unrelated resident selection slots.
    std::vector<CaveResidencyTicket> pump(){
        if(!revision_)return {};
        std::vector<CaveResidencyTicket> requests;
        std::set<ChunkKey3,KeyLess> desired;
        std::vector<ChunkKey3> buildOrder;
        struct Candidate{ChunkKey3 key{};uint64_t distance=0;};
        std::vector<Candidate> nearby;
        for(int64_t z=-int64_t(settings_.radiusXZ);z<=int64_t(settings_.radiusXZ);++z)
        for(int64_t y=-int64_t(settings_.radiusY);y<=int64_t(settings_.radiusY);++y)
        for(int64_t x=-int64_t(settings_.radiusXZ);x<=int64_t(settings_.radiusXZ);++x){
            const ChunkKey3 k{center_.x+x,center_.y+y,center_.z+z};
            if(k.x< -kLimitCm||k.x>kLimitCm||
               k.y< -kLimitCm||k.y>kLimitCm||
               k.z< -kLimitCm||k.z>kLimitCm)continue;
            nearby.push_back({k,uint64_t(x*x+y*y+z*z)});
        }
        std::sort(nearby.begin(),nearby.end(),[](const auto& a,const auto& b){
            return a.distance==b.distance?KeyLess{}(a.key,b.key):
                                          a.distance<b.distance;
        });
        // Leases have priority over camera demand and cannot be discarded
        // even if the resulting desired set exceeds the camera budget.
        for(const auto& [key,item]:items_)
            if(!item.leases.empty()){
                desired.insert(key);
                buildOrder.push_back(key);
            }
        if(desired.size()>settings_.maxResident)
            throw std::length_error("Caverns pinned residency budget");
        for(const auto& candidate:nearby){
            if(desired.size()>=settings_.maxResident)break;
            if(desired.insert(candidate.key).second)
                buildOrder.push_back(candidate.key);
        }
        size_t retires=0;
        for(auto& [key,item]:items_){
            if(desired.contains(key)){
                if(item.retiring){
                    item.generation=nextGeneration();
                    item.retiring=false;
                }
                continue;
            }
            if(!item.leases.empty()||item.retiring||
               retires>=settings_.maxRetiresPerPump)continue;
            item.generation=nextGeneration();
            item.building=false;
            item.retiring=true;
            requests.push_back({CaveResidencyAction::Retire,key,
                                item.generation,revision_,item.lod});
            ++retires;
        }
        size_t builds=0;
        for(const auto& key:buildOrder){
            // Wait until native retirement is acknowledged before occupying
            // its scarce resident slot with another distant cave chunk.
            if(!items_.contains(key)&&items_.size()>=settings_.maxResident)
                continue;
            auto& item=ensure(key);
            const uint64_t dx=uint64_t(key.x>=center_.x?
                key.x-center_.x:center_.x-key.x);
            const uint64_t dy=uint64_t(key.y>=center_.y?
                key.y-center_.y:center_.y-key.y);
            const uint64_t dz=uint64_t(key.z>=center_.z?
                key.z-center_.z:center_.z-key.z);
            const uint64_t distance=std::max({dx,dy,dz});
            const uint32_t desiredLod=adaptiveLod_
                ?caveDistanceLod(distance,item.lod,distanceLod_):lod_;
            if(item.lod!=desiredLod){
                item.generation=nextGeneration();
                item.building=false;
                item.retiring=false;
                item.ready=false;
                item.lod=desiredLod;
            }
            if(item.ready||item.building||item.retiring||
               builds>=settings_.maxBuildsPerPump)continue;
            item.building=true;
            item.generation=nextGeneration();
            requests.push_back({CaveResidencyAction::Build,key,
                                item.generation,revision_,item.lod});
            ++builds;
        }
        return requests;
    }

    bool current(CaveResidencyTicket ticket)const {
        const auto it=items_.find(ticket.key);
        if(it==items_.end()||ticket.sourceRevision!=revision_||
           ticket.generation!=it->second.generation||
           ticket.lod!=it->second.lod)return false;
        return ticket.action==CaveResidencyAction::Build
            ? it->second.building : it->second.retiring;
    }
    // ACK only after native graphics/collider/nav resources for this ticket
    // have successfully been published or fully retired at an owner barrier.
    bool acknowledge(CaveResidencyTicket ticket){
        const auto it=items_.find(ticket.key);
        if(it==items_.end()||ticket.sourceRevision!=revision_||
           ticket.generation!=it->second.generation||
           ticket.lod!=it->second.lod)return false;
        auto& item=it->second;
        if(ticket.action==CaveResidencyAction::Build){
            if(!item.building)return false;
            item.building=false;item.ready=true;
            return true;
        }
        if(!item.retiring||!item.leases.empty())return false;
        items_.erase(it);
        return true;
    }
    // Failed or stale completion remains retryable; a new generation fences
    // off any old asynchronously completing result.
    bool fail(CaveResidencyTicket ticket){
        const auto it=items_.find(ticket.key);
        if(it==items_.end()||ticket.sourceRevision!=revision_||
           ticket.generation!=it->second.generation)return false;
        auto& item=it->second;
        if(ticket.action==CaveResidencyAction::Build&&!item.building)return false;
        if(ticket.action==CaveResidencyAction::Retire&&!item.retiring)return false;
        item.generation=nextGeneration();
        item.building=false;item.retiring=false;return true;
    }
    CaveResidencyStats stats()const{
        CaveResidencyStats out;out.tracked=items_.size();out.sourceRevision=revision_;
        for(const auto& [key,item]:items_){
            (void)key;
            out.ready+=item.ready;
            out.buildsInFlight+=item.building;
            out.retirePending+=item.retiring;
            out.pinned+=!item.leases.empty();
        }
        return out;
    }
    bool tracked(ChunkKey3 key)const{return items_.contains(key);}
    bool ready(ChunkKey3 key)const {
        const auto it=items_.find(key);
        return it!=items_.end()&&it->second.ready;
    }
private:
    struct Entry {
        uint64_t generation=0;
        bool ready=false,building=false,retiring=false;
        uint32_t lod=0;
        std::map<uint64_t,CaveLeasePurpose> leases;
    };
    uint64_t nextGeneration(){
        if(nextGeneration_==UINT64_MAX)
            throw std::overflow_error("Caverns stream generation overflow");
        return ++nextGeneration_;
    }
    Entry& ensure(ChunkKey3 key) {
        auto [it,inserted]=items_.try_emplace(key);
        if(inserted){
            it->second.lod=lod_;
            it->second.generation=nextGeneration();
        }
        return it->second;
    }
    StreamSettings settings_;
    ChunkKey3 center_{};
    uint64_t revision_=0,nextGeneration_=0,nextLease_=0;
    uint32_t lod_=0;
    CaveDistanceLodSettings distanceLod_{};
    bool adaptiveLod_=false;
    std::map<ChunkKey3,Entry,KeyLess> items_;
};
} // namespace aiverse::terraweave::caverns
