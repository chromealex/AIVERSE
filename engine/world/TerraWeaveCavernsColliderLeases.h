#pragma once
// Bounded, owner-thread lease allocator for Caverns SpatialWorld collider IDs.
// Tracks ONLY Caverns reservations; callers must reserve a dedicated global
// ID namespace disjoint from existing terrain, actors and external systems.
#include "engine/world/TerraWeaveCavernsStreaming.h"
#include <cstdint>
#include <map>
#include <stdexcept>
#include <optional>
#include <limits>
namespace aiverse::terraweave::caverns {
struct ColliderLease {uint32_t first=0,count=0;uint64_t token=0;};
class CaveColliderLeasePool {
public:
    CaveColliderLeasePool(uint32_t first,uint32_t count):first_(first),count_(count) {
        if(first==0||count==0||uint64_t(first)+count>uint64_t(UINT32_MAX)+1)
            throw std::invalid_argument("Caverns collider pool extent");
    }
    // A pool never silently overlaps a previously reserved live lease.
    std::optional<ColliderLease> reserve(uint32_t count){
        if(count==0||count>count_||nextToken_==UINT64_MAX)return {};
        uint64_t cursor=first_,end=uint64_t(first_)+count_;
        for(const auto& [start,lease]:live_) {
            if(uint64_t(start)>=cursor+count)break;
            cursor=uint64_t(start)+lease.count;
        }
        if(cursor+count>end)return {};
        ColliderLease out{uint32_t(cursor),count,++nextToken_};
        live_.emplace(out.first,out);
        return out;
    }
    bool release(ColliderLease lease) {
        const auto it=live_.find(lease.first);
        if(it==live_.end()||it->second.token!=lease.token||
           it->second.count!=lease.count)return false;
        live_.erase(it);return true;
    }
    bool holds(ColliderLease lease)const {
        auto it=live_.find(lease.first);
        return it!=live_.end()&&it->second.token==lease.token&&
            it->second.count==lease.count;
    }
    size_t allocations()const{return live_.size();}
private:
    uint32_t first_=0,count_=0;
    uint64_t nextToken_=0;
    std::map<uint32_t,ColliderLease> live_;
};
} // namespace aiverse::terraweave::caverns
