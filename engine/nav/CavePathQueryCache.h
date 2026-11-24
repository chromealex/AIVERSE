#pragma once
// AIVERSE Stage43: genre-independent deterministic path result memoization.
// Derived data ONLY. Never hashed/serialized as authoritative ECS state.
// Exact immutable source+environment epochs and bounded LRU prevent unbounded
// flow/HPA/native-NavMesh re-computation for repeated queries.
#include "engine/world/TerraWeaveCavernsPathRouter.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

namespace aiverse::nav {
struct CavePathCacheSettings {
    size_t maxEntries=256;
    size_t maxWaypointBytes=8u*1024u*1024u;
};
struct CavePathCacheStats {
    uint64_t hits=0,misses=0,evictions=0,invalidations=0;
    size_t entries=0,waypointBytes=0;
};
class CavePathQueryCache {
    using Point=terraweave::caverns::PointCm;
    using Profile=terraweave::caverns::CaveAgentProfile;
    using Result=terraweave::caverns::CavePathResult;
    using Query=terraweave::caverns::CavePathQuery;
    struct Key {
        uint64_t sdfRevision=0,environmentRevision=0;
        Point start{},goal{};
        std::string backend;
        Profile profile{};
        uint32_t maxVisits=0;
    };
    struct Less {
        bool operator()(const Key& a,const Key& b)const {
            return std::tie(a.sdfRevision,a.environmentRevision,a.backend,
                a.start.x,a.start.y,a.start.z,a.goal.x,a.goal.y,a.goal.z,
                a.profile.traversal,a.profile.radiusCm,a.profile.heightCm,
                a.profile.maxStepCm,a.profile.groundProbeCm,
                a.profile.sampleCm,a.profile.areaMask,a.maxVisits) <
                std::tie(b.sdfRevision,b.environmentRevision,b.backend,
                b.start.x,b.start.y,b.start.z,b.goal.x,b.goal.y,b.goal.z,
                b.profile.traversal,b.profile.radiusCm,b.profile.heightCm,
                b.profile.maxStepCm,b.profile.groundProbeCm,
                b.profile.sampleCm,b.profile.areaMask,b.maxVisits);
        }
    };
    struct Entry {Result result;uint64_t lastUse=0;size_t bytes=0;};
public:
    explicit CavePathQueryCache(CavePathCacheSettings settings={})
        :limits_(settings) {
        if(!limits_.maxEntries||limits_.maxEntries>65536||
           !limits_.maxWaypointBytes||limits_.maxWaypointBytes>512u*1024u*1024u)
            throw std::invalid_argument("Caverns route cache bounds");
    }
    void invalidate() {
        entries_.clear();
        bytes_=0;
        ++stats_.invalidations;
    }
    // Always call after any authoring/streaming/environment update. This
    // clears cached routes even if the SDF revision is unchanged (water,
    // PHYSIS hazard changes, dynamic route block, etc.).
    template<class Resolver>
    Result get(const std::string& backend,const Query& query,
               uint64_t environmentRevision,Resolver&& resolve) {
        const Key key{query.sourceRevision,environmentRevision,
                      query.start,query.goal,backend,query.agent,
                      query.maxVisits};
        if(const auto it=entries_.find(key);it!=entries_.end()) {
            ++stats_.hits;it->second.lastUse=++clock_;
            return it->second.result;
        }
        ++stats_.misses;
        auto result=resolve();
        if(result.sourceRevision!=query.sourceRevision||
           !result.reached)return result;
        if(result.waypoints.size()>limits_.maxWaypointBytes/sizeof(Point))
            return result;
        const size_t bytes=result.waypoints.size()*sizeof(Point);
        if(bytes>limits_.maxWaypointBytes)return result;
        while(!entries_.empty()&&
              (entries_.size()>=limits_.maxEntries ||
               bytes_>limits_.maxWaypointBytes-bytes))
            evictOne();
        entries_.emplace(key,Entry{result,++clock_,bytes});
        bytes_+=bytes;
        return result;
    }
    CavePathCacheStats stats()const noexcept {
        auto s=stats_;s.entries=entries_.size();s.waypointBytes=bytes_;
        return s;
    }
private:
    void evictOne() {
        auto victim=entries_.begin();
        for(auto it=entries_.begin();it!=entries_.end();++it)
            if(it->second.lastUse<victim->second.lastUse)
                victim=it;
        bytes_-=victim->second.bytes;
        entries_.erase(victim);
        ++stats_.evictions;
    }
    CavePathCacheSettings limits_;
    std::map<Key,Entry,Less> entries_;
    size_t bytes_=0;
    uint64_t clock_=0;
    CavePathCacheStats stats_{};
};
} // namespace aiverse::nav
