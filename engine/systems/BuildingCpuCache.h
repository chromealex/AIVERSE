#pragma once
// CPU-only cache, independent of ECS/GPU ownership. Leases are immutable
// shared pointers; evicting an entry never invalidates a caller's lease.
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <cstdint>
#include <algorithm>
#include <utility>
#include <bit>
#include <type_traits>

namespace aiverse::lib::systems {
struct BuildingCacheStats {size_t entries=0,estimatedBytes=0;uint64_t hits=0,misses=0,evictions=0,bypasses=0;};
class BuildingCacheKey {
public:
    void number(uint64_t v){for(int i=0;i<8;++i)bytes_.push_back(char((v>>(i*8))&255u));}
    void real(float v){number(std::bit_cast<uint32_t>(v));}
    void text(const std::string& s){number(s.size());bytes_.append(s);}
    std::string take(){return std::move(bytes_);}
private:std::string bytes_;
};
// Owner-thread registry mutation must be serialized with lookups.
// The cache itself serializes its producer, avoiding duplicate generation
// on concurrent identical requests. Producers must never reenter this cache.
template<class T>
class BuildingCpuCache {
public:
    using Lease=std::shared_ptr<const T>;
    explicit BuildingCpuCache(size_t budget=64u*1024u*1024u,size_t entries=128)
        :budget_(budget),maxEntries_(entries){}
    template<class Produce,class Valid,class Estimate>
    Lease get(std::string key,Produce produce,Valid valid,Estimate estimate){
        std::lock_guard lock(mutex_);
        if(auto it=entries_.find(key);it!=entries_.end()){
            ++stats_.hits;it->second.used=++clock_;return it->second.value;
        }
        ++stats_.misses;
        auto value=std::make_shared<const T>(produce());
        if(!valid(*value))return value; // errors never become cache hits
        const size_t bytes=estimate(*value)+key.capacity()+sizeof(Entry)+128;
        if(!budget_||!maxEntries_||bytes>budget_){++stats_.bypasses;return value;}
        while(!entries_.empty()&&(stats_.estimatedBytes>budget_-bytes||entries_.size()>=maxEntries_)){
            auto victim=std::min_element(entries_.begin(),entries_.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});
            stats_.estimatedBytes-=victim->second.bytes;entries_.erase(victim);++stats_.evictions;
        }
        entries_.emplace(std::move(key),Entry{value,bytes,++clock_});stats_.estimatedBytes+=bytes;
        return value;
    }
    void clear(){std::lock_guard lock(mutex_);entries_.clear();stats_.estimatedBytes=0;clock_=0;}
    BuildingCacheStats stats()const{std::lock_guard lock(mutex_);auto s=stats_;s.entries=entries_.size();return s;}
private:
    struct Entry{Lease value;size_t bytes;uint64_t used;};
    mutable std::mutex mutex_;std::map<std::string,Entry> entries_;
    size_t budget_,maxEntries_;uint64_t clock_=0;BuildingCacheStats stats_;
};
} // namespace aiverse::lib::systems
